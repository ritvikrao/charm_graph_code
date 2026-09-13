# Step 7.6c — local execution and deployment

Item 3 of the 7.5 next work. This note covers the parts that need no
allocation: the setup-timing repair the item names as a prerequisite, the audit
of the LCI UCX registration-cache warning, the representation-width question,
and two findings about the runtime configuration that turned up while auditing
the first two.

Nothing here is a performance claim. The profiling against GAPBS, and the
separation of process count, affinity, transport endpoints and starvation
policy, are the campaign this note prepares for.

## Setup timing was not merely unassigned, it was undefined

The 7.5 note says ACIC's printed `Read time` "is unassigned for binary file
mode and must not be used." That understates it. `read_time` was a bare
`double` member with no initializer, written only by the `MODE_CSV` path and by
the guard `generate_mode == 1 || generate_mode == 2`. So **`MODE_RMAT` and
`MODE_GAPBS` -- every graph in the benchmark campaign -- printed whatever the
allocation happened to hold.** It printed 0.0, which is why the field looked
merely unset rather than undefined.

The repair (commit 3b575c7):

* Every timer starts at -1, so an unmeasured phase says so instead of
  reporting a plausible zero.
* `setup_time` is assigned in `begin()`, which is reached in every mode once
  each chare holds its partition. That is the one boundary that means the same
  thing everywhere: the solver could start now.
* Modes that generate rather than read -- uniform, mesh, rmat -- set
  `read_time` to `setup_time`, because input and build are one phase there.
* `MODE_GAPBS` reports `index_time` instead: the PE-0 header and offsets read
  that decides the partition, separately from the edge rows each PE then reads
  for itself.
* `done()` prints three non-overlapping phases that add up to `Total time`:
  Setup, Compute, Stats.

Measured, and they sum:

| mode | setup | compute | stats | total |
|---|---:|---:|---:|---:|
| mesh (2) | 0.017393 | 0.009033 | 0.000038 | 0.026465 |
| rmat (3) | 0.048810 | 0.010988 | 0.000014 | 0.059812 |
| gapbs (4), mesh512.wsg | 0.034932 | 0.089079 | 0.000021 | 0.124031 |

The last row is why this had to come first: **setup is 28% of that run's
total**, and it was previously folded into a number reported as if it were
solve time. Any cold-start comparison written before this repair was comparing
something nobody had measured.

`index_time` for that run is 0.005481 -- 16% of setup is one PE reading the
header and offsets array before any other PE does anything.

## The UCX registration-cache warning

The recorded warning, three times per process per run:

```
<lci:warn:reg_cache> UCX registration cache is not available (status=-22).
Continuing without caching. You can either disable the registration cache or
try preloading liblci-ucx.so before other allocators such as tcmalloc to
enable UCX memory hooks.
```

The item says to audit it and **not to assume it explains the observed gap**.
It does not, and the reason is narrower than it first looks.

**The warning's own suggested cause does not apply here.** `ldd` on `sssp_smp`
shows `liblci.so` and `liblci-ucx.so` and **no tcmalloc or jemalloc at all**.
There is no competing allocator to preload around. Whatever prevents the UCM
memory hooks from installing, it is not allocator ordering, so the remedy the
message offers is a false lead on this system.

**What the status means.** `status=-22` is `UCS_ERR_UNSUPPORTED` returned by
`LCII_ucs_rcache_create` in `reg_cache.cpp:129`. LCI sets `rcache_ = nullptr`
and continues. In `device_inline.hpp`, `register_memory` then falls through to
`register_memory_impl` on every call, so each registration is a real one rather
than a cache hit.

**When that can possibly matter.** Registration only happens on the
zero-copy rendezvous path. `communicate.cpp` picks `rdv_zcopy` when the message
exceeds `max_bcopy_size`, which derives from `packet_size` -- built as 8192.
Below that, messages travel in the packet pool, which is registered once at
init and never consults the cache. So the missing cache is on the hot path only
for messages over roughly 8 KB.

Mean bytes per TRAM message, from the wave 1 and 2 campaign records:

| graph | 1 node | 2 nodes |
|---|---:|---:|
| mesh20 | 4,815 | 19,699 (3,042-38,311) |
| rmat22 | 48,839 | 48,304 |
| road-ny | 17,963 | 12,882 |
| youtube | 40,780 | 29,054 |

So rmat22, youtube and road-ny sit well above the threshold and mesh20 at one
node sits below it. On size grounds the cache is plausibly live for three of
five graphs.

**But the gap it was invoked to explain is a one-node mesh22 gap.** The 7.5
layout table reports GAPBS/ACIC of 0.10 on mesh22 at one node -- 0.0917 s
against 0.9287 s. A registration cache changes the cost of registering memory
for network transfers. It cannot account for a factor of ten on a single node
on the graph whose messages are the *smallest* in the table. The warning is
real, it is worth fixing, and it is not the explanation.

Two things would make this testable rather than argued, and both are listed
below as configuration axes rather than settled results.

## Two configuration findings that came out of the audit

Neither was being looked for, and each is a matched comparison item 3 can run.

### The linked build has LCI's shared-memory backend off

`ldd` resolves `liblci.so` to `reconverse-linux-x86_64-shmem`, and
`/u/rao1/charm_reconverse/bin` symlinks into that tree. Its build config says:

```
reconverse-linux-x86_64-shmem/.../lci_config.hpp:  #define LCI_WITH_SHM 0
reconverse-linux-x86_64-shm/.../lci_config.hpp:    #define LCI_WITH_SHM 1
```

**The tree named `-shmem` is the one with shared memory disabled**, and a
complete `-shm` tree that enables it sits beside it, unused. The `-shmem` tree
is the newer of the two (Sep 10 against Sep 9), so this may well be deliberate
and the naming merely unfortunate -- but it is exactly the "transport
endpoints" axis item 3 asks to separate, it bears directly on the 8 x 15 layout
where eight processes share one node, and it is a one-line change to test.

This is a lead, not a diagnosis. LCI's own SHM backend being off does not by
itself mean same-node traffic crosses the NIC: the underlying provider may
still carry it over its own shared-memory transport. Establishing which is a
measurement, not an inference.

### The packet pool is about half a gigabyte per process, and unpriced

`LCI_PACKET_SIZE_DEFAULT` 8192 x `LCI_PACKET_NUM_DEFAULT` 65536 is 512 MB of
registered packets per process, allocated regardless of graph size. Measured
peak RSS on `mesh512.wsg`, a graph whose edges occupy about 16 MB:

| `LCI_ATTR_NPACKETS` | peak RSS | compute time |
|---:|---:|---:|
| 65536 (default) | 567 MB | 0.0876 |
| 8192 | 110 MB | 0.0975 |
| 2048 | 80 MB | 0.0885 |

**About 490 MB of a 567 MB process is packet pool**, and on this workload
shrinking it 32-fold costs nothing measurable. That is one process on one node
with no network traffic, so it does not license a default change -- in-flight
message counts at 16 nodes are the case that would justify the reservation. But
it does settle the accounting question the 7.5 note left open, that "ACIC graph
bytes omit runtime reservations, [and] these cannot be compared as aggregate
peak memory". The reservation is now a number, and it dwarfs the graph.

Raising `packet_size` above the message sizes in the table would also push the
large-message graphs off the rendezvous path entirely, which is the direct test
of whether the missing registration cache costs anything. It cannot be done
without lowering `npackets` to match: 64 KB x 65536 would be 4 GB per process.

## Representation width

ACIC's `Edge` is `{ long end; cost distance; }` with `typedef long cost`, so
**16 bytes**, and `Update` is `{ long dest_vertex; cost distance; }`, also 16.
GAPBS uses 32-bit fields for these inputs, so 8 bytes. The width therefore
applies to every update in flight as well as to stored edges, which makes it a
communication-volume question and not only a footprint one -- the TRAM byte
counts in the table above are all carrying it.

The item requires that a packing change retain a valid distance range. Maximum
distances actually observed, against the graphs in the campaign:

| graph | max distance |
|---|---:|
| mesh20 | 246,154 |
| mesh22 | 506,463 |
| rmat22 | 3,161 |
| road-ny | 1,290,145 |
| youtube | 7,285 |

The largest is 1.29e6, about 1,600x inside a signed 32-bit range, and vertex
counts (4.2M at scale 22) are three orders inside it too. **The distances are
not what blocks a narrowing.**

What does is the sentinel and the bound. `lmax` is
`std::numeric_limits<cost>::max()`, and `begin()` broadcasts `max_sum`, the sum
of every vertex's maximum out-edge. Weights are uniform over [1, 1000]
(`graphlib/weights.h`), so on mesh22 that sum is roughly 4.19e6 vertices x
several hundred, which is order 1e9 to 4e9 and straddles the signed 32-bit
maximum of 2.147e9. A naive `typedef int cost` would overflow the path bound on
the largest mesh while every real distance still fit comfortably.

So the packing change is available, but it has to narrow the stored and
transmitted fields while keeping the bound and the sentinel wide. That is a
different and smaller change than retyping `cost`.

## What this note does not do

No GAPBS profile, no queue/cache/aggregation accounting, no layout separation,
and no Wasp comparison -- the item marks Wasp as additional rather than a
prerequisite in any case. Those need allocations, and they need the setup
timing repair above to have landed first, which it now has.
