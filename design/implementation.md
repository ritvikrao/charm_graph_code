# ACIC implementation overview

*Current architectural reference, 2026-09-23. Historical implementation notes
and defect reports are retained in Git history.*

## Execution model

ACIC is an asynchronous distributed weighted SSSP implementation built with
Charm++/Reconverse and htram aggregation. Vertices are partitioned by contiguous
ID ranges. Each process owns one or more worker PEs; workers relax local
vertices and htram aggregates remote updates by destination.

The controller periodically reduces a histogram of live distances, chooses an
admission threshold, updates communication policy and checks termination. The
frontier remains monotone. A solve terminates only after local queues, pending
aggregation state and in-flight work are collectively empty.

## Current data path

1. `graphlib` generates or reads the graph into flat CSR.
2. The distance vector remains separate from CSR because accepted relaxations
   write it frequently.
3. A worker removes admitted vertices from a local or process-shared priority
   structure and scans their outgoing edges.
4. Local relaxations update shared process state directly. Remote relaxations
   enter htram destination buffers.
5. The compact wire item sends an update in eight bytes and recomputes the
   destination PE on receipt.
6. Sender filtering, hold bitmaps, idle flushing and empty-delivery suppression
   reduce useless traffic and buffer scans.

## Current policy layers

| Layer | Current behavior |
|---|---|
| Dense scale-free graphs | Lazy heavy-edge relaxation and communication rules activate from degree/density/skew metadata. Sparse process-sharing mechanisms resolve inactive. |
| Sparse high-diameter graphs | Reader tiling and process-shared distances/queues activate through `auto` rules. The accepted mesh profile uses nearest priority, queue batch 8 and heap slice 8. |
| Road study | A fixed bucket width of 131072 makes the distance window representable. It is a diagnostic setting, not a general policy. |
| Runtime | Reconverse `1233130` runs with `+old-scheduler`; registered scheduling changes polling order and causes extra rework. |

Static graph metadata, training-selected constants and live feedback are
distinct. No current live-feedback rule causes the accepted mesh result.

## Correctness and reproducibility invariants

- Every paper solve checks an order-independent pair of 64-bit distance sums.
- `tools/graph_digest` supplies an independent serial Dijkstra path and pins
  graph identity separately from the distributed solver.
- `scripts/golden_digests.txt` protects the deterministic generators from
  silent changes.
- Multi-node gates also check process/PE layout, message envelopes, queue
  conservation, edge accounting and effective policy settings.
- A timeout is a failed run. Partial output is never accepted as convergence.
- Graph conversion preserves direction, weights and the defined handling of
  loops and parallel edges.

## Important repaired defects

The stable base includes fixes for:

- an out-of-bounds histogram read on an empty controller window;
- randomized aggregation flushing;
- timeout paths that once looked like successful termination;
- incorrect and quadratic process-shared state;
- leaked `updates_in_tram` accounting;
- per-destination structures incorrectly sized by PE count;
- fixed node-count assumptions in htram;
- message user-size/envelope mismatches;
- default-controller deadlocks, idle flush cadence and empty deliveries.

These are correctness or infrastructure repairs. They are not paper
contributions by themselves.

## Source map

| Area | Main files |
|---|---|
| Solver and controller | `sssp_smp.cpp`, `sssp_smp.ci`, `live_slack.h`, `process_work.h` |
| Graph representation and references | `graphlib/`, `weighted_node_struct.h`, `tools/graph_digest.cpp`, `tools/graph_convert.cpp` |
| Work and communication instrumentation | `work_cost.h`, `acic_prof.h`, diagnostic compile flags in `Makefile` |
| Experiment driver | `benchmarks/run.py`, `benchmarks/onenode_ab.py`, `benchmarks/machine.py` |
| Launch and affinity | `benchmarks/launch_acic.sh`, `scripts/anvil/`, `scripts/frontier/` |
| Result auditing | `benchmarks/check_priority_runs.py`, `benchmarks/onenode_accept.py`, reporting scripts under `benchmarks/` |

## Scale limits

The compact wire format currently requires vertex IDs below `2^31` and
distances below `2^32`. The common GAPBS file adapter also uses signed 32-bit
destinations. Graph construction and some experiment paths replicate more data
than an extreme-scale campaign can tolerate. Before scale 33+ work, extend the
formats, add overflow checks, define the certificate for unreachable vertices
and verify a memory model for every implementation.

## Change discipline

An implementation change enters the candidate only after an independent
correctness gate, a one-factor causal comparison, repeated allocations and a
regression check on dense graphs. Build names do not identify source or runtime;
use the tuple in [configurations.md](configurations.md).
