# R1 next: attributing distributed road work (fixed-total-layout experiment)

2026-09-21, recorded before submission. The author resumed experiments at
"R1 next" of the [revised plan](ipdps27-onenode-gap.md) (steps 2–3), on Anvil.
This is a training-source attribution experiment, not R3 acceptance, not a
tuned performance baseline and not a batch-size search.

## Question

On Delta, batch 8 at two nodes (8 x 15 per node) performs 1.65–1.72 times the
one-node edge attempts on road-usa-z and 1.22–1.23 times on mesh26-z, with
per-attempt cost roughly unchanged. Going from one to two nodes changes three
things at once: the number of process-local priority domains and partitions
(8 → 16), the physical spread of those domains (one node → two), and the
number of workers draining each domain's shared queues (15 per process, so
global concurrency doubles). This experiment separates them.

## Layout chain

All arms use the same batch-8 candidate as the Delta experiments: the
`acic_r1_batch` binaries from commit `48ca9c0` with htram `7db9c0a`, rebuilt on
Anvil with the unchanged `build_onenode.sh`, and the flags `--process-share auto
--reader-tile auto --slack-control off --process-queue nearest
--process-queue-batch 8`. All four layouts run interleaved, with rotating
order, inside each two-node allocation. One-node arms leave the second node idle.

| Label | Nodes | Processes x workers per node | Total processes / workers |
|---|---:|---|---:|
| `n1_8x15` | 1 | 8 x 15 | 8 / 120 |
| `n1_16x7` | 1 | 16 x 7 | 16 / 112 |
| `n2_8x7` | 2 | 8 x 7 | 16 / 112 |
| `n2_8x15` | 2 | 8 x 15 | 16 / 240 |

Paired source ratios of production edge attempts (and time) define:

- **D = n1_16x7 / n1_8x15:** doubles the domains and partitions at nearly
  constant worker count, all on one node.
- **P = n2_8x7 / n1_16x7:** physical placement only. The processes, workers,
  partition and reader tiles are identical. `CkNumNodes()` counts processes,
  so vertex ownership does not depend on the physical node count; the audit
  checks the resolved layout line. Memory/NUMA locality also changes: 16 x 7
  puts two processes in each NUMA domain.
- **W = n2_8x15 / n2_8x7:** more than doubles the workers draining each of the
  16 domains, with placement fixed.
- **T = n2_8x15 / n1_8x15 = D · P · W** for each source. This is the Delta
  scaling step reproduced on Anvil.

Each allocation also runs a repeated `n1_8x15_control` production arm and a
work-cost diagnostic arm at each layout (the `ACIC_WORK_COST` build). The
diagnostic arms give queue cost per attempt, expansions per vertex, stale pops,
idle share, and offered intra-process, intra-node and inter-node attempts.
Production times and ledger attempts drive the decision.

Design: mesh26-z and road-usa-z, the same two training sources as on Delta
(mesh 22442342, 41856222; road 5620086, 1294456), batched in one launch per
arm. Each arm runs one validated warmup and then three timed repetitions.
That gives 9 arms, 72 solves per graph and 144 solves per allocation, 64 of
them diagnostic. Every solve is digest-checked against the reference.

## Predictions and decision rule

Road is the primary graph; mesh is the contrast. Let the noise floor *f* be
the larger of the per-source |log| control/candidate attempt ratio and its
time ratio, in that allocation. A factor is **attributed** when:

- its road attempt ratio deviates from 1 by more than max(3f, 5%);
- in the same direction for both training sources;
- in both allocations.

The dominant factor is the one with the largest share of log T.

| Outcome | Interpretation | Next step (at most one intervention) |
|---|---|---|
| P attributed and dominant | Physical separation, i.e. remote-update delay and admission/controller timing, drives rework | Target inter-node update timing or admission, with a recorded prediction |
| D attributed and dominant | More priority domains and partitions, not physical distance, drive rework | Target cross-domain priority coordination or partition granularity |
| W attributed and dominant | Concurrency within a domain (more workers each removing 8 entries) loosens priority | Target breadth within a domain at a strong fixed setting before any live controller |
| No factor attributed, or T ≤ 1.15 on Anvil | Diffuse or machine-dependent growth | No intervention; report and reconsider the scaling pilot |

The Delta result was mixed: extra idle share, 0.16% offered inter-node
attempts and similar per-attempt cost. From that, the prior is that W and D
matter more than P, but no factor is established. A small P with a large
T would argue against a pure network-latency explanation. It would not rule
out a memory-locality effect in D.

Secondary checks:

- T on Anvil within about ±0.15 of Delta's 1.65–1.72 (road) and 1.22 (mesh).
  A large difference is reported as machine dependence.
- Per-attempt queue and solver cost at each layout. If cost rather than work
  changes along the chain, report it separately.
- Time chain ratios, reported alongside work. A work factor that costs no
  time is not a priority for intervention.

## Machine caveat

Anvil intermittently enters a degraded mode lasting minutes, in which
messaging costs about 100 times more, independent of configuration. Rotating
arm order and repeated controls expose it. Every repetition is kept, and any
repetition more than twice its source median is flagged in the report, not
dropped. Attempt counts can also shift in such a window, since asynchronous
rework depends on latency, so both allocations must agree.

## Gate, jobs and budget

A two-node correctness job (`scripts/delta/onenode_queue_batch_verify.sbatch`
with `ACIC_SRUN_MPI=pmi2`) must pass first. It runs the 224 solves with serial
verification and requires inter-node work in 90 connected diagnostic solves.
The two performance allocations (`scripts/anvil/r1_attrib.sbatch`) depend on it
`afterok` and use a frozen harness copy.

Reservation caps: verification at 2 nodes x 10 min, 42.7 SU; performance at
2 x (2 nodes x 20 min), 170.7 SU. That is 213 SU in total, within the R0–R2
cap of 2,000 SU (about 190 SU used so far on Delta).

Campaign root: `/anvil/scratch/x-rrao/acic/ipdps27-onenode`.
Configuration: [r1-attrib-variants.json](../benchmarks/r1-attrib-variants.json).
