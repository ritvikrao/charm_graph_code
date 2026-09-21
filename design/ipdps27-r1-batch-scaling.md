# R1 batching: bounded two-node scaling experiment

2026-09-21, recorded before preparing/submitting the new harness. The author
explicitly requested the eight-node unbatched sanity check and authorized
continuing with the recommended batching scaling test. This is a training
experiment, not R3 acceptance or a new batch-size search.

The [one-node batching result](ipdps27-r1-batch-one-node-results.md) shows a
large real timing gain. The [eight-node unbatched result](ipdps27-r1-eight-node-check.md)
also shows a critical road scaling problem: roughly 8.7x as much work for eight
times as many workers, even with nearest priority. The next question is
whether batch 8 retains enough priority and reduces queue cost enough to
produce net speedup at two nodes.

## Fixed experiment

Reuse the exact existing `acic_r1_batch` and `acic_r1_batch_diag` binaries from
commit `48ca9c0`, with sharing auto, reader tiles auto and slack off. The main
candidate is **nearest priority, batch 8 on both graphs and node counts**.
Batch 32 is a fixed ablation, not a graph-specific setting or a new default.

Two separate **two-node** performance allocations, each with 8 processes x 15
workers per node (240 workers total), on mesh26-z and road-usa-z. Use the same
two training sources, one warmup and three timed repetitions, source batching
and rotating arm order as the completed one-node experiment. Eight arms:

1. Frozen R0 production, to retain a historical distributed reference.
2. New production nearest/1, to isolate batching within the same binary.
3. New production nearest/8, the selected candidate.
4. New production nearest/32, the fixed comparison.
5. A repeated nearest/8 production control, explicitly named `batch8_control`.
6. Diagnostic nearest/1, to measure queue cost at this node count.
7. Diagnostic nearest/8.
8. Diagnostic nearest/32.

Each allocation contains 128 solves, including 48 diagnostic solves. Compare
against **both** completed one-node allocations 22282647/22282652, matching
sources, flags, hashes and per-node layout. Record all source-level results
and all allocation combinations; do not select a favorable denominator.
Candidate and batch-32 diagnostic work/cost have matching one-node builds.
Nearest/1 has no same-build one-node diagnostic arm, so any older diagnostic
comparison must retain that limitation. Use the already measured one-node
GAPBS references for context; this experiment does not allocate nodes to
rerun GAPBS or count the reference twice.

A **two-node small-graph correctness job** precedes both performance jobs
through `afterok` with invalid-dependency cancellation. Reuse the 224-solve
matrix with two processes of four workers on each node: sparse/dense inputs,
batch sizes 1/8/32/64, local/nearest, tiling and sharing controls, repeated and
isolated sources, range extension and diagnostic conservation. Require actual
inter-node attempts on connected diagnostic solves. Preserve all raw logs,
binary hashes and the frozen harness/configuration.

Reservation cap: two 10-minute allocations at 256 cores, plus a five-minute
verification at 32 allocated cores, **88 SU total**, within R0–R2's existing
2,000-SU cap. No solver rebuild or larger-node reservation is needed now.

## Decision rule

Primary outcomes are source-paired two/one production time and edge attempts.
Measure candidate-control variation, instrumentation overhead, queue calls
per consumed entry, solver/queue cost per attempt, and idle/communication
measurements. Preserve warmups for validation, excluding them from medians.

Continue toward a larger-node batching comparison only if time improves
reproducibly beyond control variation on both graphs. Report every source
regression; do not retune batch size by graph or node count. If road work grows
enough to erase the two-node gain, investigate distributed work/order before
spending on larger allocations. A work increase near or above 2x is a warning
to examine cost and load balance, not by itself a correctness failure.

The main scaling baseline is optimized one-node batch 8, not frozen R0 or
unbatched nearest. A win against those slower baselines alone does not meet
this experiment's objective. C6 and default settings remain unchanged; R2
remains conditional on a credible route to final acceptance, and R3 still
requires the author's decision.
