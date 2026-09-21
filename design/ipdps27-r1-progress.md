# R1 implementation and queued comparison

2026-09-21. The [decision recorded before implementation](ipdps27-r1-decision.md)
selects process-wide queue priority as the one R1 intervention. The opt-in
implementation and frozen experiment are ready; **solver verification and
performance results are still pending**. R1 has not passed its performance
gate. R2 remains conditional, and R3 still requires the author's decision.

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
disabled. Six existing report-parser tests pass. Shell syntax, the embedded
verification Python, build-source equivalence and frozen-harness equivalence
were checked.

Compute-node verification **22276922** is pending for priority. It checks
88 solves against serial distances across local/nearest, sharing on/off,
reader tiling on/off, four successive sources including an isolated and a
repeated source, dense auto-mode inactivity, and a narrow-width
range-extension case. Diagnostic runs additionally require queue identities,
direct-versus-production edge accounting and offered-update topology sums.
These 88 solves are planned checks, not completed results.

## Queued experiment

| Job | Role | Limit | Status at submission |
|---|---|---|---|
| 22276922 | Correctness, 16 cores | 5 minutes | Pending, priority |
| 22276940 | One node A | 12 minutes | Pending, after successful verification |
| 22276941 | One node B | 12 minutes | Pending, after successful verification |
| 22276942 | Eight nodes A | 6 minutes | Pending, after successful verification |
| 22276945 | Eight nodes B | 6 minutes | Pending, after successful verification |

The four performance jobs have `afterok:22276922` and
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

The four performance reservation limits total **256 SU**, plus **1.333 SU**
for correctness, inside the combined 2,000-SU R0–R2 cap. A timeout or missing
cell is a failed/incomplete comparison, not a license to discard a slow arm.
Interpret the resulting reductions using the prediction and disconfirmation
criteria in the decision document before proceeding with R2.

Frozen harness: `/u/rao1/.tmp/ipdps27-onenode/build/r1-harness`, with its own
source inventory. Submitted configuration: `configs/r1-priority.json` in
the same campaign. The binaries and their full source inventories are
retained under the campaign's `bin` and `build` directories:

| Binary | SHA-256 |
|---|---|
| acic_r1_priority | e012ff455779d49969b9e698e4615c59705520fbda0463f92ccef68200a37493 |
| acic_r1_priority_diag | 3f2c71cd83d205749700a5f24c1503edf9c2d07bc4dee4dfbfe19f02ffef46fb |

Builds snapshot the implementation on top of decision commit `a86bb63`;
the recorded file hashes identify the uncommitted source at build time.
The application, queue header, Charm interface and unit-test source were
compared byte-for-byte to the working tree after the builds completed.
