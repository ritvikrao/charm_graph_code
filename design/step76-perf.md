# Step 7.6j: what bounds ACIC's per-edge cost, and what moved it

*2026-09-16. Follows [7.6i](step76-traces.md), which found ACIC spending 318 ns of
hardware time per graph edge on rmat25 at two nodes against RIKEN's 49, and
could not see inside `process_heap`. All runs below: Anvil, two nodes, the
7.6f1 layout ACIC chose, one source per graph, variants interleaved within one
allocation (`scripts/anvil/ab_compare.sbatch`), every digest checked against
the reference. Allocations differ by up to 20% on the same binary, so only
same-allocation ratios are quoted as speedups.*

## Result

rmat25, 8 x 15, source 23077392. ns/edge = PEs x solve seconds / edges.

| build | median solve | ns/edge | vs baseline |
|---|---:|---:|---:|
| baseline (7.6f1 binary) | 1.29-1.35 s | ~300 | 1.00x |
| + no divisions, one destination lookup, key comparator | 1.34 s | ~306 | none |
| + compact wire items, 2048 per message | 1.16 s | 267 | 1.16x |
| + 6144 items per message | 0.75 s | 172 | 1.79x |
| + `--send-filter-bits 17` | 0.53 s | 122 | 2.5x |
| + prefetch (same allocation: 0.635 s -> 0.529 s) | 0.53 s | 121 | 2.5x |

orkut, 8 x 15 (default 2048 items): 0.643 s -> 0.426 s with compact items
(1.51x), 0.219 s with the send filter as well (2.93x).

The per-edge gap to RIKEN on rmat25 is now 2.5x (121 against 49 ns), from 6.5x.
ACIC is below GAPBS's 145 ns/edge, which uses half the hardware.

**High-diameter graphs do not share the win**, and the best buffer size is
opposite (16 x 7, source 14369093 / 3508545):

| items per message | mesh24 | road-usa |
|---|---:|---:|
| baseline (wide, 2048) | 1.37 s | 3.26 s |
| compact, 1024 | **1.15 s** (1.19x) | **2.28 s** (1.43x) |
| compact, 2048 | 1.44 s | 3.35 s |
| compact, 6144 | 2.88 s | 6.72 s |

At 6144 the solver delivered 2.7x more updates on mesh24: a larger buffer
waits longer to fill, and an asynchronous relaxation does more wasted work
behind it.
The send filter also slowed both graphs at 6144 (untested at 1024). The
default is therefore 2048, the one size that regressed neither class;
`--bufsize` and `--send-filter-bits` stay per-run choices.

## How the bound was found

1. **Counters.** `sssp_smp_papi` (`acic_prof.h`) counts cycles, instructions,
   branch and L2 misses and divider cycles per PE, and samples addresses on
   cycle overflow; `benchmarks/papi_profile_report.py` symbolizes them with
   inline chains and groups them into pipeline stages. Anvil's
   `papi/6.0.0.1` predates Zen 3 and needs `LIBPFM_FORCE_PMU=amd64_fam17h_zen2`.
   On one process the divider held a quarter of all cycles, and the
   destination lookup, bucket computation and heap comparator were the hot
   code. Removing all of it changed nothing on two nodes.
2. **Counters perturb this runtime.** With any per-thread counter active the
   PEs made ten times as many voluntary context switches and solved two to
   three times slower; user-mode cycles per update were unchanged, so the
   profile's shape is usable, its wall time is not. A CPU-time timer instead
   of the PMU (`ACIC_PROF_TIMER`) also perturbed and saw only a quarter of
   the PE time.
3. **The send path.** An instrumented `libreconverse.so`, preloaded, counted
   what each PE did inside the LCI backend: 45% of PE time was inside
   `issueAm`, which spins on `post_am` + `progress()` until the send queue
   has room (`retry_nomem`, then `retry_backlog`); 59% of `progress()` calls
   found the device lock held by another of the 15 PEs sharing 4 devices.
4. **Bytes, not messages.** Removing the spin (15 devices, 1024 queued sends)
   made the solve *slower*, so the spinning PEs were waiting on the network.
   Doubling the bytes per item at a fixed message count cost 1.77x; 3.4x the
   messages at fixed bytes cost 1.08x. The Charm++ build has no shared-memory
   path (`CMK_USE_SHMEM 0`), so 15 of every 16 destinations, same node or
   not, go through the HCA.

## What changed

- **Compact wire items** (`htram`, `HTRAM_COMPACT_WIRE`, default in the
  Makefile as `WIRE=compact`): an item is 8 bytes (31-bit vertex plus the
  overflow flag, 32-bit distance) instead of 24; the receiving process
  recomputes each item's destination PE through the client's lookup. Values
  that do not fit abort the run. htram's `BUFSIZE` cap is now 16384.
- **`--send-filter-bits N`**: a per-PE direct-mapped table of the last
  distance created for a vertex; an update no shorter is dropped before it is
  charged. The kept one is still delivered, so the result cannot change.
  2^17 8-byte entries was best on rmat25 (22% of edges dropped); 2^20 dropped
  half but lost on cache misses.
- **Prefetch** of the filter slot and of `distances[]` eight items ahead.
- **Exact CPU work removed** (no measured effect on two nodes, kept because
  it is free): bucket division by reciprocal, one bucket computation per
  arrival, one destination lookup per edge handed to htram, `nodeOf[]` and a
  cached release level in htram, a single-key heap comparator.

Tried and rejected: `+LBOff`, jemalloc, glibc trim/top-pad tunables, more LCI
devices, more queued sends, 64 KB eager packets.

## Not settled

- **Buffer size per graph class.** A fixed default leaves 1.3-2x on the table
  on one class or the other; the controller already knows when a round is
  starved, which is where an adaptive size would start.
- **The send spin.** Now 4% of PE time on rmat25, but it is CPU burnt while
  the network is busy; LCI's backlog (`allow_retry=false`) would free it.
- **Shared memory between processes.** Same-node traffic still crosses the
  HCA. LCI's own shared memory (`LCI_WITH_SHM`) carries only small messages
  (about 96 bytes), and 7.6c measured that build 2x slower. The lever is
  Charm++'s `--enable-shmem`, which Reconverse implements (`cmishmem.cpp`):
  a Charm++ point-to-point send to another process on the same node goes
  through a POSIX shared-memory pool when it fits the cutoff (8 MB pool per
  process and a 256 KB cutoff by default; `++ipcpoolsize`, `++ipccutoff`).
  ACIC's 16-49 KB messages fit; whether 15 senders fill one 8 MB pool at this
  rate is the first thing to check.
- **The next CPU costs** on rmat25 (cycle profile, job 20766187): edge scan
  including the filter lookup 15%, TRAM insert 14%, scheduler 13%, apply 12%,
  network progress 11%.

Tools: `acic_prof.h`, `benchmarks/papi_profile_report.py`,
`benchmarks/lci_stats_report.py`, `scripts/anvil/papi_profile.sbatch`,
`scripts/anvil/ab_compare.sbatch`. Output under `campaign/ab/` and
`campaign/papi/`. The instrumented backend source is
`/anvil/scratch/$USER/acic/rcv/instr.cpp` (not in the Reconverse tree).
