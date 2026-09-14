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

## The campaign: one axis at a time, one node

Job 22061524, `--mode deployment`, `mesh22,rmat22,road-ny`, 120 workers, 324
timed runs, all valid, 16 minutes. Every configuration differs from the 8 x 15
candidate by one thing, and every number below is the median of twelve runs
paired against `layout-8x15` on the same graph, source and repetition. What is
compared is the solver's own compute phase, so no `srun` launch is inside it;
setup is reported separately, which is what 7.6c made possible.

| configuration | mesh22 | rmat22 | road-ny |
|---|---:|---:|---:|
| `layout-8x15` (median compute) | 0.783 s | 0.264 s | 0.170 s |
| `layout-1x120` | 7.4x slower | 20.6x slower | 15.5x slower |
| `layout-2x60` | 2.8x slower | 2.2x slower | 4.7x slower |
| `layout-4x30` | 1.4x slower | 1.2x slower | 1.6x slower |
| `idle-flush-off` | 1.12x slower | 1.01x | 1.5x slower |
| `idle-flush-on` | 1.01x | 1.06x slower | 1.02x slower |
| `transport-shm` | 1.02x | **2.0x slower** | 1.02x |
| `packets-8192` | 1.01x | 1.02x slower | 1.02x |
| `packets-eager-64k` | 1.02x | 1.01x | 1.00x |

### Process count is the whole of it

The rpn sweep holds total occupancy at 120 workers per node and moves only how
many processes carry them, and it is monotone on all three graphs and enormous:
one process per node is **7.4 to 20.6 times slower** than eight. Nothing else in the
table moves more than a factor of two. The 8 x 15 candidate is confirmed, and
confirmed by a margin that makes the remaining axes second-order.

Setup scales with it too, and separately from the solve: mesh22's setup is
0.25 s at 8 x 15 and 1.27 s at 1 x 120, a 5x difference on a phase that is
mostly reading a file. So the geometry is buying parallel input as well as
parallel relaxation, which the 7.5 NUMA probe could not have separated because
it moved geometry, endpoints and the starvation threshold together.

This does not fully isolate process count, and the item said it would not: the
starvation threshold reads `CkNumNodes()`, so it moves with rpn. The
`idle-flush` arms are what separate that, by holding rpn at 8 and moving the
policy instead -- and they move almost nothing, which is the answer. The
default `starved` policy is right: forcing it off costs 1.5x on road-ny and
1.12x on mesh22, forcing it on is within noise everywhere. Whatever the rpn
sweep is measuring, it is not the starvation threshold.

### The shared-memory lead is closed, in the opposite direction

The `-shmem` tree the build links has `LCI_WITH_SHM 0` and the unused `-shm`
tree beside it has it on, which read like unfortunate naming and a one-line fix.
It is not: enabling LCI's shared-memory backend is **2x slower on rmat22** and
neutral on the other two. rmat22 is the graph with by far the most traffic, so
the axis is being exercised; the backend is simply worse for it than what the
provider does with the same traffic. The newer tree is the right default and
the naming is the only thing wrong.

### The registration cache does not explain anything

The earlier section of this note established that the UCX warning is mechanical
and that registration only happens on the rendezvous path, above
`max_bcopy_size`, derived from `packet_size`. `packets-eager-64k` raises
`packet_size` to 64 KB -- past the measured message sizes, so those graphs leave
the rendezvous path and registration entirely -- and lowers `npackets` to 4096
to keep the pool affordable. It reads 1.00x to 1.02x on all three graphs.
`packets-8192`, which shrinks the pool 8-fold at the default packet size, is
likewise 1.01x to 1.02x.

So the missing registration cache costs nothing measurable, and about 490 MB per
process of the reservation buys nothing measurable either. The item said not to
assume the warning explained the observed gap; it does not, and the pool can be
cut by a factor of eight for free on this workload.

**The caveat is real and it is the same one as before.** This is one node. All
eight processes are on the same host, so the wire is short and the in-flight
message counts are the smallest they will ever be. `transport-shm` moving 2x
shows there is a real transport under test rather than a loopback no-op, but a
registration cache and a packet pool are both things that should matter more at
sixteen nodes than at one. These arms say the warning is not the single-node
gap; they do not say it is nothing anywhere.

## What this note does not do

No GAPBS profile, no queue/cache/aggregation accounting, and no Wasp comparison
-- the item marks Wasp as additional rather than a prerequisite in any case. The
layout separation the item asked for is done, and its answer is that process
count dominates and the starvation policy does not; the remaining axes are
untested above one node.
