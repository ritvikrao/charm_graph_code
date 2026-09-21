# One-node queue batching: implementation and submitted experiment

2026-09-21. Authorized follow-up to R1; the
[decision and predictions](ipdps27-r1-batch-decision.md) were committed as
`7dbefbf` before implementation. Implementation and experiment harness are
commit `48ca9c0`. Performance conclusions are pending; R2 remains conditional
and R3 still requires the author's decision.

## Implementation and local checks

`--process-queue-batch 1..64` defaults to 1. Both local and nearest policies
can remove a bounded batch from one producer bin after one selection and lock
acquisition. Admission and original bucket order are checked under the lock;
head hints are published once after removal. Expansion runs after unlocking.
The worker drains its private batch before yielding and keeps the existing
100-entry bound. It rechecks admission before consuming each entry and returns
newly ineligible work without double-charging it. Distance CAS and outstanding
histogram accounting retain their previous roles. These remain application
SSSP queues, not Reconverse scheduler queues.

Production and `ACIC_WORK_COST`/`ACIC_COMM_SHARE` builds both succeeded. Queue
tests passed at capacities 1, 8, 32 and 64 for both policies: admission stopping
mid-bucket, original bucket order, fallback to another producer, reuse after
draining, and 80,000 exactly-once removals with eight concurrent producers and
consumers per case. The same suite passed AddressSanitizer and UndefinedBehaviorSanitizer
with leak detection disabled because of the cluster's ptrace restriction.
Seven existing report tests, Python and shell syntax checks passed. The
extended audit still validates all 160 earlier R1 solves and 32 diagnostic
records. Full parallel SSSP correctness awaits the compute-node job below.

## Submitted jobs

| Job | Role | Reservation | Status at submission check |
|---|---|---|---|
| 22282643 | Serial-correctness comparison, 224 planned solves | 16 cores, 3 min | Pending, priority |
| 22282647 | One-node comparison A | 128 cores, 12 min | Pending, dependency |
| 22282652 | One-node comparison B | 128 cores, 12 min | Pending, dependency |

Both comparisons require `afterok:22282643` with `--kill-on-invalid-dep=yes`.
Verification covers capacities 1/8/32/64, local/nearest, reader tiling on/off,
sharing inactivity, sparse and dense graphs, repeated and isolated sources,
and narrow-width range extension. Diagnostic solves also check queue identities,
direct/production edge accounting and offered-update topology sums.

Each performance allocation runs mesh26-z and road-usa-z, two fixed training
sources, one warmup and three timed repetitions per source, eight processes
of fifteen workers. Nine interleaved arms: frozen R0, frozen R1 nearest,
new local/1, new nearest/1, nearest/8, nearest/32, repeated nearest/1, and
diagnostic nearest/8 and /32. The matched allocation also runs GAPBS production
and diagnostic references with their frozen thread counts and delta values,
one warmup and three timed repetitions. That is 144 ACIC and 32 GAPBS solves
per allocation. Source-paired production times and attempts are primary;
diagnostic cost explains the outcome and does not replace production timing.
The new batch diagnostic costs can be compared directly with each other;
comparison with the previous nearest/1 diagnostic costs spans allocations.

The reservation cap is **52 SU**, within the existing R0–R2 2,000-SU budget.
Existing eight-node jobs 22281608/22281609 remain pending; no new multi-node
jobs were submitted and one-node progress does not depend on them.

Frozen campaign root: `/u/rao1/.tmp/ipdps27-onenode`.
Configuration: `configs/r1-batch.json`, copied from
[r1-batch-variants.json](../benchmarks/r1-batch-variants.json).
Frozen harness: `build/r1-batch-harness`, with revision and file-hash inventory.
Each build has an isolated source snapshot, source inventory and manifest.

| Binary | SHA-256 |
|---|---|
| acic_r1_batch | 00579456983c3c0210a75a2b59ef508ce43db4c3357980d4c80f71f07935ffb1 |
| acic_r1_batch_diag | 66bba62a1c69011715edd93653e8aa978b83c84cfd0665eceda2ca1a02b16616 |

After completion, run `benchmarks/check_priority_runs.py` with the frozen
configuration and both job IDs. Preserve the per-allocation source medians,
repeated-control variation, raw digests and diagnostic conservation checks.
Audit the GAPBS reference matrix separately; warmups never enter time medians.

The paper outline now records the proposed division: credited local techniques
provide a strong execution baseline; a narrower high-diameter contribution
must demonstrate distributed scaling and explain which ACIC mechanism delivers
it. This experiment alone establishes neither a scaling result nor C6.
