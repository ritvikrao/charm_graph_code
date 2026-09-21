# R1 implementation and queued comparison

2026-09-21. The [decision recorded before implementation](ipdps27-r1-decision.md)
selects process-wide queue priority as the one R1 intervention. The first
verification failed a diagnostic destination-accounting check after 80
correct solves, cancelling all four dependent performance jobs before they
ran. The diagnostic bug is fixed: replacement verification passed all 88
solves and both one-node jobs completed with 160 valid performance solves.
The [one-node audit](ipdps27-r1-one-node-check.md) finds much less repeated
work but substantial queue cost and no reproducible road speedup. Eight-node
jobs remain queued. R1 has not passed its performance gate. R2 remains
conditional, and R3 still requires the author's decision.

## Implementation

`--process-queue local|nearest` defaults to `local`. `ProcessWork` retains
the existing per-producer queues and original-bucket ordering. In nearest
mode, each bin publishes its current head distance under its existing lock
into a separate cache-line-aligned atomic hint. A worker scans those hints,
preferring its own bin on equal keys, then tries the smallest advertised
head. The actual head and the existing admission predicate are checked under
the bin lock. Failed selection falls back to the existing bounded scan of
the remaining bins.

This is approximate priority within one process. A concurrent push/pop can
change a hint during selection; the hint never authorizes retirement or
expansion. Distance CAS, stale-entry rejection, generation/clamp admission,
overflow accounting, controller thresholds and the 100-item yield remain
as before. Emptying a bin publishes the empty sentinel, including when a
source finishes. `local` allocates no hints. The policy is inactive when
process sharing is off; requested and effective state are reported.

The extra hint scan and cache traffic are explicit costs of the intervention.
R0's sampled queue timer encloses them; `queue_probes` still counts attempted
bin accesses rather than individual hint loads. No adaptive policy or new
default is introduced. The same-binary local arm and frozen R0 arm measure
any implementation overhead even when nearest is disabled.

## Checks completed

Both immutable production and work-cost builds succeeded. Unit checks cover
nearest selection versus historical own-bin-first selection, unchanged
original-bucket ordering, an ineligible minimum with eligible work elsewhere,
head improvements, reuse after draining, and concurrent pushes/pops with
eight workers. All 80,000 items drain exactly once for each policy.

The concurrent checks also pass with AddressSanitizer and
UndefinedBehaviorSanitizer. LeakSanitizer cannot run under this tool's
ptrace environment; that first attempt failed in LeakSanitizer startup, and
the address/undefined checks were rerun successfully with leak detection
disabled. Seven report-parser tests now pass, including a regression from
the failed dense-graph run. Shell syntax, the embedded
verification Python, build-source equivalence and frozen-harness equivalence
were checked.

Compute-node verification **22276922 failed**, exit 1, after 30 seconds on
cn033 (0.133 allocated CPU-hours). All 80 solves it reached matched serial
distances; all 36 diagnostic solves passed queue and edge-retirement
accounting. The four dense diagnostic solves failed destination accounting:
classification ran after the sender filter, so it omitted filtered attempts.
For dense source 0, 5,504 edges were scanned but only 728 classified;
the missing 4,776 exactly equal the sender-filter count. The final eight
planned solves did not run. [Raw-log audit](onenode-data/r1-verification-22276922.json).

The fix moves destination classification before filtering under
`ACIC_WORK_COST`, retaining the original production binary and queue policy.
It also excludes BFS-only scans to match the SSSP edge-attempt definition.
Both the verification script and performance report now use the same strict
three-way accounting check, with explicit values in failure messages.
All 96 ACIC R0 diagnostic solves were rechecked: they had zero sender-filter
drops, and their topology, edge and retirement counts already agree. This
bug does not alter the R0 decision-driving work/cost results.

Replacement verification **22281603 passed in 39 seconds** with a two-minute
limit. It checked all 88 solves against serial distances across local/nearest, sharing on/off,
reader tiling on/off, four successive sources including an isolated and a
repeated source, dense auto-mode inactivity, and a narrow-width
range-extension case. Diagnostic runs additionally require queue identities,
direct-versus-production edge accounting and offered-update topology sums.
Raw revalidation confirms all 88 serial/parallel digests and all 44 diagnostic
accounting records, including the eight dense sender-filter cases.

## Queued experiment

| Job | Role | Limit | Current status |
|---|---|---|---|
| 22281603 | Correctness, 16 cores | 2 minutes | Completed, cn007, 39 s |
| 22281605 | One node A | 12 minutes | Completed, cn130, 284 s |
| 22281606 | One node B | 12 minutes | Completed, cn133, 284 s |
| 22281608 | Eight nodes A | 6 minutes | Pending |
| 22281609 | Eight nodes B | 6 minutes | Pending |

Original performance jobs 22276940/22276941/22276942/22276945 were cancelled
by the failed dependency, consuming zero allocated CPU time. No slow arm
or performance outcome was discarded.

The four replacement performance jobs have `afterok:22281603` and
`--kill-on-invalid-dep=yes`: a failed verification cancels their eligibility
to run. They use the fixed two training sources on mesh26-z and road-usa-z,
one warmup plus three timed repetitions, with identical batched-source
launches across all arms. Eight processes of fifteen workers per node.
All arms retain reader tiles, process sharing auto and slack off.

Five arms: frozen R0 production, new production local, new production nearest,
repeated new production local control, and new diagnostic nearest. The
configuration is [r1-priority-variants.json](../benchmarks/r1-priority-variants.json).
Every solve is digest-checked; diagnostic accounting is checked at the end
of each graph. Primary outcomes are production time and edge attempts.

The four replacement performance reservation limits total **256 SU**, plus
**0.533 SU** for replacement correctness; the failed attempt used 0.133
CPU-hours. This stays inside the combined 2,000-SU R0–R2 cap. A timeout or missing
cell is a failed/incomplete comparison, not a license to discard a slow arm.
Interpret the resulting reductions using the prediction and disconfirmation
criteria in the decision document before proceeding with R2.

Frozen replacement harness: `/u/rao1/.tmp/ipdps27-onenode/build/r1-harness-v2`,
with its own source inventory. Submitted configuration: `configs/r1-priority-v2.json` in
the same campaign. The binaries and their full source inventories are
retained under the campaign's `bin` and `build` directories:

| Binary | SHA-256 |
|---|---|
| acic_r1_priority | e012ff455779d49969b9e698e4615c59705520fbda0463f92ccef68200a37493 |
| acic_r1_priority_diag_v2 | c0471ef70d1345c1a078ee43058c06674c4f3c53d76f1f7ea254670781732382 |

The production binary remains the exact implementation built on decision
commit `a86bb63`; its SHA-256 was rechecked. Diagnostic v2 snapshots the
counter fix on top of `6428c79`, with file hashes identifying the source at
build time. Original `acic_r1_priority_diag`, hash
`3f2c71cd83d205749700a5f24c1503edf9c2d07bc4dee4dfbfe19f02ffef46fb`,
and the original harness/configuration are retained for the failure audit.
