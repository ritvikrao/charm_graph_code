# Fixed-layout scaling check (1/2/4 nodes, Anvil)

2026-09-21, recorded before submission. This follows the
[attribution result](ipdps27-r1-attribution-results.md): road's work growth
tracks global worker concurrency. The check measures the best fixed layout
at each node count. That layout is the baseline any concurrency-limiting
intervention must beat. It is a training experiment: no new mechanism, no C6
acceptance, and no default change.

## Runtime update first

At the author's request, Reconverse was updated before this check.

- **Runtime:** `main` `33b8c36` → `1233130` (ten commits). These include:
  - spanning-tree broadcast on by default;
  - collectives fanning out once per destination process;
  - a message header growing from 20 to 24 bytes;
  - affinity-flag parsing (the Linux path is unchanged).
- **Build:** a fresh `~/charm_reconverse/reconverse-linux-x86_64-mpicxx-v0921-shm`.
  - It is configured from the old build's CMake cache; the options are
    identical and LCI is the same revision, `ca88ce2c`.
  - `~/charm_reconverse/{bin,lib,include}` now point to it. The old v0916 build is kept.
- **Rebuilt against it:** htram, charm_graph_code `sssp_smp`, unionfind and
  prefixLib all build.
  - paratreet does not build: `Partition.h:606` registers a
    `pair<int,int>(uint64_t)` location function where unionfind expects
    `pair<int,uint64_t>`. This is an API mismatch between paratreet and
    unionfind, not a Reconverse issue, and it is outside this work.
- **Campaign binaries:** the batch-8 candidate is rebuilt from `48ca9c0` with
  htram `7db9c0a` on the new runtime.
  - `acic_r1_batch` sha256 `2e2a5a6a…`, `acic_r1_batch_diag` `eabcc460…`.
  - The previous Anvil binary is kept as `acic_r1_batch_rc0916` (`bd07ed61…`)
    for a runtime-change control. It links Reconverse statically, but it
    resolves `liblci.so` through the moved `lib` link, so it now loads the
    rebuilt copy of the same LCI revision.

## Design

Every arm uses batch 8 with the same flags as the attribution experiment,
on mesh26-z and road-usa-z. It uses the same two training sources, batched
per launch, with one warmup and three timed repetitions and rotating arm
order. There is one job per node count, each in two independent allocations.

| Job | Arms |
|---|---|
| 1 node | `n1_8x15`, `n1_16x7`, `n1_8x7`, `n1_16x7_control`, `n1_16x7_rc0916` |
| 2 nodes | `n2_8x15`, `n2_16x7`, `n2_8x7`, `n2_8x7_control`, `n2_8x15_rc0916` |
| 4 nodes | `n4_8x15`, `n4_16x7`, `n4_8x7`, `n4_8x7_control` |

Layouts are given per node:
- 8 × 15 is 120 workers per node;
- 16 × 7 is 112 workers per node, in twice as many processes;
- 8 × 7 is 56 workers per node.

Each control repeats the layout expected to be best on road. The `rc0916`
arms repeat cells of the attribution experiment on the old runtime.
Production ledger attempts and times are primary. There are no diagnostic
arms; queue cost was already characterized.

## Predictions

1. **Road.** 8 × 7 is at least as fast as 8 × 15 at 2 and 4 nodes, and
   road's work at 8 × 15 keeps rising with nodes. The best road time at
   4 nodes improves on the best at 2 nodes by less than 1.15×, which would
   show that fixed layouts give road no scaling path. If instead 4 nodes
   improve on 2 nodes by at least 1.15× at some fixed layout, that layout
   family is the one to extend to 8 nodes.
2. **Mesh.** 8 × 15 is best at 2 and 4 nodes, and time keeps falling.
3. **One node.** 16 × 7 remains fastest, reproducing the 8–20% advantage
   over 8 × 15.
4. **Runtime change.** The `rc0916`/new ratios lie within the control noise
   floor. A larger difference is reported, and all comparisons in this
   check are within the new runtime.

For each graph, the per-node-count best layout, chosen on these training
sources, becomes the fixed baseline. Choosing the layout is tuning, not a
mechanism or paper claim.

## Scope: these parameters do not transfer to RMAT

Only high-diameter graphs are measured here, so worker-count and layout
conclusions apply to them only.

- On the RMAT/scale-free regression graphs, `--process-share auto` resolves
  off, so the shared queues and batch setting studied here are inactive.
- Their wide frontiers support far more parallelism than road's; GAPBS's
  own tuning used 128 threads on mesh26 but 64 on road.
- Their costs are dominated by hub vertices and aggregation.

A layout or concurrency limit that helps road could therefore be neutral or
harmful on RMAT, and 16 × 7 processes change htram's per-process
aggregation. Any layout or limit proposed from this check must pass the
five-graph regression suite (`rmat25`, `orkut`, `uniform25`, `rmat26`,
`rmat27`) before being adopted. A per-graph-class rule must be keyed on
metadata available when the graph is read, not on graph names.

## Gate, jobs and budget

- **Gate:** a two-node correctness job on the new runtime, the unchanged
  224-solve `onenode_queue_batch_verify.sbatch`. It must pass first.
- **Performance jobs:** six jobs (`scripts/anvil/layout_check.sbatch`),
  1/2/4 nodes × two allocations, each depending on the gate `afterok`, with
  a frozen harness copy.
- **Reservation caps:** 1-node 25.6 SU, 2-node 51.2 SU and 4-node 102.4 SU
  per allocation, 358 SU for both, plus 42.7 SU for the gate. Expected actual
  use is about 150 SU. About 290 SU of the R0–R2 2,000-SU cap has been used.

Campaign root: `/anvil/scratch/x-rrao/acic/ipdps27-layout`.
