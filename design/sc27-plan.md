# ACIC → SC27: research and engineering plan

*Working plan, drafted 2026-09-10; revised after step 7 on 2026-09-13.
Code references to `htram_group.*` are in the
companion repo at github.com/UIUC-PPL/htram.*

## Current decision after step 7

**Run a focused SSSP comparison and adaptivity study before the generalization
refactors.** Add steps **7.5** (validity, current baselines, real inputs, modest
scaling) and **7.6** (fixed-versus-adaptive controls and one bounded optimization
cycle). Then extract interfaces around a second kernel. The detailed rationale,
pilot matrix, controls, and decision gates are in
[post-step7-review.md](post-step7-review.md).

The [completed comparison pilot](step75-comparisons.md) measured the external
gap, real inputs, held-out sources, scaling through 16 nodes, and process-layout
sensitivity, and exposed ACIC progress failures on a two-node mesh and full-node
RMAT. Those progress paths are repaired and gated (7.6a). Combining remains off
and is not a promised contribution.

**Revision, 2026-09-14: process geometry is a confound in every measurement
taken before 7.6c, and the pilot's external comparison is one of them.** Item 3
of the 7.5 follow-up separated the deployment variables and found that process
count dominates everything else by 7x to 20x at fixed worker occupancy: on one
node at 120 workers, eight processes of fifteen beat one process of 120 by 7.4x
on mesh22, 20.6x on rmat22 and 15.5x on road-ny, monotone in rank count, while
no other axis moved more than 2x. **ACIC ran at one process per node in every
7.5 comparison, while the RIKEN baseline was allowed to tune its rank count and
chose two or four.** The pilot therefore compared a tuned baseline against an
untuned ACIC on the one axis that matters most, and no ratio in it — against
GAPBS, RIKEN or Gluon — can be quoted until it is re-taken.

The same confound reaches inside the project. 7.6d demoted the initial-width
rule after measuring it at 1.4x to 2.9x slower on four graphs; re-taken at the
deployment geometry, the same comparison has it winning or tying on all nine
and **not one graph regressing**. The 7.6d table was a property of the process
layout, not of the graphs. The pilot does not yet establish adaptive advantage
or satisfy Gate A, and the first requirement for Gate A is now a re-take of the
external comparison at a deployable layout.

## Context

The IA³@SC24 paper (`~/Downloads/acic_2024paper.pdf`) introduced **ACIC** — Asynchronous
Continuous Introspection and Control — a fully asynchronous distributed SSSP in Charm++
(`~/charm_graph_code/sssp_smp.cpp`) over a bespoke aggregation library (`~/htram`). A
self-perpetuating cycle of reductions and broadcasts builds a global histogram of *active
updates* bucketed by tentative distance; percentile thresholds cut from that histogram gate
which updates enter a PE's priority queue (`heap_threshold`) and which go on the wire
(`tram_threshold`). The intended benefit is fewer speculative relaxations while
computation proceeds between nonblocking controller collectives.

The original plan identified three gaps:

1. **Scale-free graphs lose.** RIKEN Δ-stepping is **2.8–3.3× faster** on RMAT. The paper's
   own headline weakness, and the graph class reviewers care most about.
2. **One algorithm, two synthetic graph types, 16 nodes.** No real inputs, **no weighted
   input reader at all** (every weight is `rand()`), no correctness validation of any kind.
3. **The adaptivity claim is half-true.** The abstract advertises "an adaptive aggregation
   library"; htram has *zero* self-tuning. Every knob is a compile-time constant or a fixed
   constructor argument.

Steps 1–7 have substantially addressed the engineering and validation gaps and
introduced adaptive policies. Real-input performance, current external
competitiveness, and adaptation against strong fixed settings remain open.
The RMAT ratio above describes the 2024 paper, not a fresh comparison.

**Target:** SC27 full paper. Use **early April 2027 provisionally**; the SC27
paper deadline is unconfirmed. The previous date was based on an incorrect
SC26 precedent: its paper deadline was **Apr 8**, abstracts Apr 1, and mandatory
AD appendix Apr 28. See the [official SC26 schedule](https://sc26.supercomputing.org/all-dates-deadlines/).
Re-anchor when the SC27 CFP appears, including the separate abstract deadline.

**Claim to defend:** *adaptive introspection co-designed across the algorithm and the
communication layer* — feedback steers the algorithm's work admission and the
aggregation library's buffering/flushing, generalizing across a stated class of
graph algorithms and graph structures. This remains a hypothesis: compare
shared feedback with independent policies and strong fixed configurations.

**Report time to solution and work together.** Work reduction explains a result;
it does not replace a runtime benefit or establish an energy advantage. Count
actual edge examinations, successful distance changes, messages, memory, and
control overhead separately. CPU/GPU advantages are empirical questions.

---

## What planning found: three defects that change how to read the 2024 results

Historical diagnosis, retained for provenance. These defects were addressed in
steps 2–5; references below describe the earlier code. The mechanism and
measurement corrections in the step notes supersede the original predictions.

### 1. A per-edge 2048-iteration scan that has probably been dominating every run

`HTram::tflush()`'s `agg == WPs` branch (`htram_group.C:596-600`)
sends the partial buffer and replaces it **without decrementing `updates_in_tram[i]`**. The
`WW` branch (`:604`) has the same gap. Every *other* send site decrements (`:315, :343,
:367, :438, :448, :630, :647`), and `changeThreshold` (`:216-224`) only applies threshold-
movement deltas — it never resets. So `updates_in_tram[d]` drifts monotonically upward,
permanently.

That counter drives the release trigger on the **per-outgoing-edge** path:

```cpp
// htram_group.C:281-282, called once per edge from generate_updates
if (updates_in_tram[dest_node] > selectivity * bufSize)   // selectivity=1.0, bufSize=2048
    insertBucketsByDest(tram_threshold, dest_node);
```

`insertBucketsByDest` (`:303-328`) loops `i = 0 .. tram_threshold` (up to 2047) over mostly-
empty `std::queue`s — the `break` that bounds its work sits *inside* the inner `while`, so
empty queues cost a full pass — then calls `tram_done` → `process_local_updates`. `tflush()`
is called from `sssp_smp.cpp:1521` on controller rounds, so this arms within the first second
of a run and stays armed.

**Why this reframes the paper, not just the code:** the 2024 parameter study concluded the
optimal `p_tram` is **0.999 — i.e. bypass `tram_hold` entirely and send everything
immediately**. That is exactly the setting that avoids this pathology. The tram-threshold
mechanism, one of ACIC's two core contributions, has plausibly never been evaluated on its
merits. Re-running that sweep after the fix is the highest-value single experiment available
and may recover a contribution the paper currently disclaims.

> **Corrected in step 3.** The leak penalized higher `p_tram` more, rather than
> favoring 0.999. The fixed laptop sweep was nearly flat. Its 2.5–4.1× gain is
> not a reproduction of the paper's cluster experiment. See
> [defect-fixes.md](defect-fixes.md#the-p_tram-sweep-re-run); a current multi-node
> sweep belongs in step 7.6.

### 2. `HTramMessage` is not a varsize message, so `bufSize` never reaches the wire

`htram_group.ci:3-6` declares `itemT *buffer`. That is **not** charmc's varsize syntax —
varsize is `Type name[]` (`tramNonSmp.ci:6` proves it). The generated allocator ignores the
`sizes` argument entirely:

```cpp
// htram_group.def.h:377-380
void* CMessage_HTramMessage::alloc(int msgnum, size_t sz, int *sizes, int pb, GroupDepNum g) {
  CkpvAccess(_offsets)[0] = ALIGN_DEFAULT(sz);      // `sizes` unused
  return CkAllocMsg(msgnum, CkpvAccess(_offsets)[0], pb, g);
}
```

Every `new HTramMessage()` allocates `8 + 2048×24 ≈ 48 KB`. `setUsersize` is called at only
four sites (`:560, :597, :602, :668`) — all on the timed/idle flush path — so the ~15
*size-triggered* sends (`:317, :319, :344, :372, :374, :386, :440, :443, :452, :504, :632,
:634, :670, :672`) ship the full 48 KB envelope regardless of the runtime `bufSize`.

Consequence: the 2024 buffer-size study (512/1024/2048, Fig. 6) varied *when* messages were
sent but not *how many bytes crossed the network*. Its conclusion is about flush latency, not
bandwidth, and at `bufSize=512` roughly 4× of the wire bytes were padding. Must be re-run.

> **Withdrawn (step 4).** The last two sentences are wrong. The `buffer_size` constructor
> argument was accepted and never read, so the only way to change the buffer size was to edit
> `BUFSIZE` and rebuild — which also resized the fixed array. Buffer size and message size
> moved together, and the 2024 study did vary wire bytes. The padding described here would
> have appeared only via `setBufferSize()`, which nothing called. See
> `design/varsize-messages.md` §2.

### 3. Memory, and a set of smaller bugs

`tram_hold` is indexed by `dest_node` everywhere (`:272, :276, :306, :621`) but allocated with
`CkNumPes()` rows (`:68-73`) — a 64× over-allocation at ppn 64. **The "~160 MB/PE" memory
pressure is an allocation bug, not a structural property.** One-line fix; motivate combining
by wire bytes and redundant relaxations instead, which is a stronger claim anyway.

| Location | Problem |
|---|---|
| `sssp_smp.cpp:825-828` | `dest_table = new int[V/M]` (floor) but the loop writes `ceil(V/M)-1` → 1-int heap overflow whenever `V % 1024 != 0`. Every graph in `graphs/` triggers it. `int i = j*M` also overflows above 2³¹ vertices |
| `sssp_smp.cpp:544` + `:1409` | All-zero window → `first_nonzero = -1` → `contribute_histogram(-2)` → reads `histogram[-1]`. Out of bounds, near the end of every run |
| `htram_group.h:53` | `NODE_COUNT 512` sizes `get_idx`/`done_count`/`local_idx`, all looped to `CkNumNodes()`. Silent corruption above 512 nodes — **inside the target scale** |
| `sssp_smp.cpp:1521` | `rand() % 5` gates the flush — process-global state; SMP contention point and reproducibility hazard |
| `sssp_smp.cpp:390` | 30 s `fast_exit` truncates long runs, then prints results *as if converged* |
| `sssp_smp.cpp:823-824`, `:1371` | `thisIndex` and `CkMyPe()` used interchangeably; correct only under 1-chare-per-PE round-robin placement |
| `sssp_smp.cpp:1202`, `:1595` | `processHeapShared` decrements the wrong PE's atomic; its `== 0` check is meaningless. Dead under the default build — delete (also reclaims 8 MB/PE) |
| `htram_group.C:260, :310, :623` | `get_dest_proc` called **2–3× per item**, not once |

---

## Strategy

**Order of work: establish the SSSP contribution, then test transfer.** Steps
7.5–7.6 have a bounded budget; do not wait for SSSP to be optimized completely.
External comparisons and fixed-policy sweeps determine whether the next action
is a targeted optimization, a second kernel, or a narrower claim. CombiningHold
already has a byte-oriented interface but its existence is not a reason to
generalize the whole library now.

**Min-relaxation and additive-residual algorithms need distinct contracts.**
SSSP/BFS can exploit monotonicity and idempotence. PageRank must conserve each
additive contribution, including buffered residual mass, and validate to a
specified tolerance: floating-point addition is neither exact nor associative.
Residual priorities and convergence need their own design. Do not promise that
every graph algorithm fits a `(key, value, combine)` interface.

**Conditional architecture option: type erasure for htram genericity.** Defer
this choice until a second payload/algorithm establishes the need. Templating the whole class works
(`tramNonSmp.h:74` is the proof) but kills the static archive, gives one `HTramNodeGrp` per
payload type, and forces `.ci` instantiation gymnastics on every application. Instead: a
byte-oriented `HTramCore` + a header-only `HTram<T>` façade + a POD `HTramOps` vtable,
matching the existing `set_func_ptr_retarr` style. Recover lost inlining with a `BuiltinOp`
enum (`MIN_I64`/`SUM_F64`/…) switched on *outside* the insert loop.

Possible later layout, not the next milestone:

```
htram/     htram_core.{h,C,ci}  htram_ops.h  htram.h (HTram<T> façade)
           htram_hold.{h,C}     htram_combine.{h,C}
acic/      acic.h  acic_controller.{h,C,ci}      # AcicController group + AcicMaster chare
graphlib/  graph_types.h  partition.h (Locator)  edge_source.{h,C}  csr_builder.{h,C,ci}
engine/    update_engine.h  algo_{sssp,bfs,cc,pagerank}.h  apps/
```

---

## Runtime controls

Keep the working Delta launch/build configuration from the completed port.
The saved A/B scripts use direct execution on one node and `srun` on multiple
nodes. Do not add a new launcher or CMake migration merely to match the original
plan's wording.

Record and control worker/rank layout, affinity, compiler/runtime revisions,
`+lci_ndevices`, and `LCI_ATTR_PACKET_SIZE`. Establish a fixed transport
configuration for the first comparisons; perform a small sensitivity check
before attributing a buffer-size optimum to the adaptive method. Promote
transport co-tuning to a contribution only if a measured interaction warrants it.

Previous Reconverse observations from other applications motivate checks, not
ACIC performance conclusions. Check trace-argument compatibility and tracing
overhead before collecting short traces. Use profiles to locate cost and traces
to identify exposed delay; apparent idleness alone does not identify a bottleneck.

---

## Work plan

Steps 1–7 are completed SSSP development. **The 7.5 comparison pilot is
complete; 7.6a–7.6c are closed, 7.6d is settled as a per-case mesh rule and 7.6e
is measured and rejected; item (1), progress repair, was reopened by a
deadlock in the shipped default and is closed again — cause found and repaired
(7.6g); 7.6h's hang and floor reporting is in place; the admission × delivery
study that Gate A turns on has started on Anvil**; steps 8–12 are
conditional and no longer a sequential refactor queue. Research gates produce
decision reports; implementation gates require correctness and appropriate
performance checks. Integer-distance refactors retain identical results, with
one- and two-node validation; PageRank uses a tolerance-based gate.

| # | Step | Gate | Effort |
|---|---|---|---|
| **1** | Runtime port and reproducible build/launch configuration | Completed development milestone; retain the working Delta configuration | complete |
| **2** | Deterministic graphs and serial Dijkstra/digest verification | [Verification harness](verify-harness.md); independent and large-run validation remain in 7.5 | complete |
| **3** | Control-path, memory, and correctness repairs | [Defect fixes](defect-fixes.md); laptop parameter sweep does not close the multi-node performance question | complete |
| **4** | Variable-size messages and envelope checks | [Message design](varsize-messages.md); two-node correctness gate passed; transport performance sweep remains separate | complete |
| **5** | Dead-code retirement, graphlib, flat CSR, binary inputs | [Graphlib](graphlib.md); 18 verification configurations including generated/file equivalence | complete |
| **6** | H1–H4 diagnosis on the recorded synthetic configurations | [Diagnosis](scale-free-diagnosis.md); interpretation qualified by later step 7 results below | complete |
| **7** | Gated flush cadence, combining/fold, bucket coarsening, idle flush | Mesh cadence gain 3.6–3.8×; combining/fold off; confirmed small-input bucketing gains; two-node RMAT idle-flush gain 1.12× with 12.6% fewer created updates. See the [review evidence table](post-step7-review.md#what-the-evidence-supports) for scope and limits | complete |
| **7.5 — pilot complete** | Matched weighted inputs, independent digest checks, fixed-policy tuning, RIKEN/GAPBS/Gluon comparisons, real road/social inputs, 1–16-node scaling, occupancy and process-layout probes | [Measurements and failures](step75-comparisons.md); small-component repair passed, but a separate progress failure keeps the correctness gate open | complete pilot; follow-up in 7.6 |
| **7.6a — complete** | Progress repair: the source update is counted, so the controller's empty-window rescue fires instead of pinning both thresholds at the window origin; bounded stall/conservation diagnostics; minimized regressions in the gate | [Progress repair](step76-progress.md); the reproducible RMAT failures are repaired and gated, the intermittent mesh one has a detector and an unconfirmed hypothesis | complete |
| **7.6b — complete** | Controller robustness as one bounded experiment: initial width, the PE-dependent two-tier eligibility rule, and clamping varied independently with process geometry frozen; why each round can or cannot coarsen recorded | [Controller](step76-controller.md); reduced rounds are not reduced work and were not read as such | complete |
| **7.6c — complete** | Deployment variables separated one at a time: process count, affinity, transport endpoints, packet pool, starvation policy | [Deployment](step76-deployment.md). **Process count dominates by 7–20×**; the LCI shared-memory backend is worse, not better; the UCX registration-cache warning explains nothing. One node only | complete at one node |
| **7.6d — closed** | Initial bucket width: `log(V)` versus the heaviest edge, with the clamp threshold frozen so conservation holds for every bucket | [Width repair](step76-width-repair.md). Settled on **two independent campaigns**, 1/2/8/16 nodes at eight processes of fifteen: it is a **mesh rule**, not a default. mesh20 and mesh22 win every paired run at every allocation (83/84 in the replication) with the margin growing with scale; every other graph regresses somewhere. `PER_GRAPH_WIDTH_RULE = {mesh20, mesh22}` | complete |
| **7.6e — measured, rejected** | Raise the clamp during a run so a graph whose distances exceed `2048 × width` is not scheduled from a single overflow bucket; creation-time flag keeps the histogram conserved | [Range](step76-range.md). The diagnosis stands — the three stuck graphs are out of range, not blocked on coarsening — but **the repair fails at scale on exactly those graphs**: 35 of 36 runs hang on mesh20/mesh22 at 8 and 16 nodes, road-ny is slower where it survives, and the arm is a no-op elsewhere. Stays in the tree defaulted off. The deferral guard it shipped with never fires | does not ship |
| **7.6g — closed** | The shipped default deadlocks. Repair the admission rule that cannot reach stranded work, and find the drain failure underneath it | [Default deadlock](step76-default-deadlock.md). **Cause found and repaired (2026-09-15, Anvil):** `process_heap()` stops at a heap top above the threshold, and at bucket scale 1 the in-range slice `[2047, 2048)` is flagged overflow for life, so once a coarsening lowers the threshold a flagged top hides admissible updates — the window pins at `floor(2047/scale)`. `--pq-overflow-last` (default on) sorts flagged updates last: mesh22 at the recorded configuration hung **12/80 off, 0/80 on**, every digest correct, no slowdown. The scale-skew explanation was tested and refuted (counter 0 in every run). Found on the way: new flags must be readonlies, and `verify.sh`'s stall checks never fired on large outputs (`pipefail` + `grep -q`) — both fixed; a repeated fixture now fails the gate without the repair. The rescue stays as a counted production guard | complete |
| **7.6h — complete** | Report the hang rate and the resolution floor beside every speedup, per allocation, in `benchmarks/report.py` and the campaign tables | `benchmarks/outcomes.py` classifies every run (ok / hung / wrong / crashed, rescued counted separately; legacy records by their timeout) and computes floors per allocation; `run.py` records `hung`, `outcome`, `stall_rescues`, `skew_top_arrivals`; `report.py` splits FAIL by kind and adds a graph/allocation floor column; `benchmarks/report_arms.py` renders any arm campaign with outcome counts, a failure table, and a verdict per cell (clears the allocation floor, own floor only, within floor, or `†` survivors only). Checked against the 7.5 archive | complete |
| **7.6f1 — not started** | Re-take the 7.5 external comparison at a deployable process geometry, with **every system allowed to tune its layout** — `configs()` needs an ACIC rpn axis chosen on training sources, the way RIKEN's rank count already is | The 7.5 pilot tuned RIKEN's ranks and left ACIC at one process per node, which 7.6c then measured at 7–20× — so that comparison measured the layout, not the systems. Not a result until this is re-taken | 1 wk |
| **7.6f2 — first wave measured, replication queued** | Item 4, the research claim: admission × delivery policies against one global fixed setting **and** per-case tuned references | **Gate A:** no unexplained stalls (7.6g is closed; every cell now carries its hang count); adaptive benefit over a frozen global fixed setting, proximity to per-case tuning, credible external runtime and resource results. Stopping rule from step 7.5 still applies: **stop after one or two supported optimizations** | 2–3 wk; `run.py --mode policy`: the 2×2 admission × delivery matrix, `global-fixed` chosen on mesh20/rmat20/uniform20 tuning sources, per-graph `tuned-fixed`, and a control, at 8×15 on 1/2/8 nodes (jobs 20744389/92/93): [first wave](step76-policy.md) — 1,512 runs, no failures; fixed delivery costs 1.4×→20× on road-ny and up to 3.4× on mesh as nodes grow, no coarsening costs 1.1×–1.9× on RMAT/youtube, no global fixed setting matches, the axes look independent rather than interacting, and per-case tuning beats adaptive at one node (up to 1.5×). Inputs are sub-second at every allocation; replication at 1/2/8/16 nodes is jobs 20744731-34 |
| **8 — conditional** | Minimal generic payload interface/type erasure only when a second algorithm requires it | SSSP results identical on one/two nodes; no unexplained runtime regression | up to 1 wk |
| **9 — split** | Fix PE/chare identity when introducing new mappings. Overdecomposition/hash placement/migration only if profiling justifies them | Correct on **all supported mappings**, not just K=1; performance benefit required for extra scheduling machinery | budget after diagnosis |
| **10 — with second kernel** | Extract `AcicController` around observed shared signals, actions, and progress contracts; may precede step 8 | SSSP validation and performance retained; second use exercises shared controller | ~1 wk |
| **11 — transfer gate** | BFS first; CC only for a specific additional hypothesis | **Gate B:** validated transfer with frozen policy constants and direction-optimizing BFS comparisons; narrow claim if transfer fails | 1–2 wk |
| **12 — broader claim only** | PageRank residual engine if claiming beyond min-relaxation/traversal; defer BC and k-core | Matched damping, dangling-node semantics and residual tolerance; no byte-identical floating-point requirement | ~3 wk |

### Retire (step 5)

Non-SMP: `sssp_nonsmp.{cpp,ci,sh}`, `weighted.{cpp,ci}`, `tramNonSmp.*` (both repos),
`ig_nonSmp.{C,ci}`, `build_nonsmp.sh`, `libtramnonsmp.a`. Two of these already fail to compile
against the current `weighted_node_struct.h`. Also `htram_group_unused.*` (39 KB of committed
dead library), `graph_serial*`, the `graph_parallel*` read-timing prototypes (they never parse
and both have a buffer overrun at `graph_parallel.cpp:136`), the app-side `tram_hold`
(`sssp_smp.cpp:697` — 2048 vectors × `reserve(4096)` per PE, passed to a function that ignores
it), and ~104 lines of commented-out logic including four entry methods whose bodies are
entirely comments but remain declared in the `.ci`.

*Keep `tramNonSmp.ci` until step 4 lands* — it is the working reference for varsize-message
syntax. Delete it with the rest afterward.

> **Done (step 5).** All of the above is gone, in both repos, along with `graph_ckio*` —
> the CkIO variant of the same read prototype — and the `NDMeshStreamer.h` include that the
> non-SMP build had forced into every translation unit touching a graph type. Three more
> things turned out to be live but inert and went with them: `local_updates` /
> `process_local_updates` (htram called back once per released batch to iterate a
> permanently empty vector), htram's unconditional `tram_done` call at three sites although
> the two-argument registration leaves it null, and a per-edge `get_dest_proc_fast` on a
> branch that cannot use its result. Mode 0, the CSV reader inside `Main`, deliberately
> survives until `graphs/*.csv` are migrated with `tools/graph_convert csv`. See
> `design/graphlib.md` §1.

### Combining — completed negative result

Step 7.2 implemented the byte-oriented source hold and batch-local fold; both
are correct and off by default. The detailed structures, accounting contract,
and measurements are in [step7-combining.md](step7-combining.md).

On the tested RMAT configurations, neither reduced relaxation work. Under WPs,
the delivered batch contains one source's message, so the fold did not provide
the intended cross-source combination. Reject/absorb counts did not predict
whether the insertion and holding costs paid for themselves.

Withdraw the absorb-rate controller and the claim that combining is the central
mechanism. Preserve the negative result. Revisit only if another scale or
network establishes a communication saving worth more than the measured cost.

### What remains of the step 6 hypotheses

The detailed historical A/Bs are in
[scale-free-diagnosis.md](scale-free-diagnosis.md) and the four H1–H4 notes.
Read “refuted” as a result for the measured configurations, not a statement
about every scale-free graph, weight distribution, source, and machine.

| Hypothesis | Recorded result | Current implication |
|---|---|---|
| H1: inadequate bucket resolution on RMAT | Resolution was adequate in step 6; coarsening helped small runs, with no resolved 2^22 benefit in step 7.3 | Measure controller overhead and fixed-policy quality at representative scales |
| H2: expensive hub redundancy | Rejects concentrated at hubs, but source hold and batch fold lost time without reducing RMAT relaxation work | Reject count/absorb rate is not a runtime cost model; combining stays off |
| H3: partition imbalance | Modest static RMAT imbalance had no measurable cost at up to 32 workers; mesh idleness was mainly temporal | Reassess active edge work and long hub handlers at larger scale before overdecomposition |
| H4: cadence-limited progress | Large mesh sensitivity; indiscriminate flushing hurt random graphs | Keep gated flushing; study its interaction with admission and its scale-dependent signal |

**Withdraw the universal claim that RMAT redundancy cannot depend on ordering.**
Step 6's percentile sweep found little effect under its tested conditions.
Step 7.4 later reduced two-node RMAT created updates by 12.6% through idle
flushing. Its timing explanation is plausible and should be tested directly.
Neither finding establishes a universal cause for the historical RIKEN gap.

The uniform generator's default partition jitter is a separate factor. Pin it
explicitly for causal A/Bs and record it in cross-system comparisons. All of
the completed mesh observations need a real road-graph check.

The idle-flush task is implemented; do not re-enable the old library stub as a
new optimization. The relevant next checks are buffer residence age, active
destination streams, exposed controller delay, and the cost of scanning every
destination on every idle pass. These belong to step 7.6.

---

## Algorithm portfolio

Use [GAPBS](https://github.com/sbeamer/gapbs) as a validation and comparison
resource, not a requirement to implement every kernel.

| Priority | Kernel | Purpose and gate |
|---|---|---|
| Current | SSSP | Establish adaptive benefit and current distributed competitiveness |
| After Gate A | BFS | Cheapest transfer test for the controller; compare with direction-optimizing BFS |
| If claiming beyond traversal/min-relaxation | PageRank | Test additive residuals, distinct priorities, and tolerance-based convergence |
| Optional | CC | Test a different progress structure; do not assume combining will pay |
| Deferred | BC, k-core, triangle counting | Additional contracts and communication requirements without a demonstrated need |

SSSP plus BFS supports a traversal/min-relaxation claim. A claim spanning
residual-based graph algorithms needs a successful PageRank transfer. Shared
infrastructure alone does not establish shared adaptive benefit. For CC,
validate the component partition independently of representative-label choices.
Do not promise five of six GAP kernels or classify k-core as a generic min
operation.

---

## Evaluation

### Baselines and positioning

Start a bounded SSSP comparison now; expand the baseline set when the kernel
portfolio is decided. Each baseline gets a documented tuning budget, sensible
rank/thread layout, and identical input semantics within equal node resources.

| Stage | System | Role |
|---|---|---|
| 7.5 | Repaired pre-step-7 ACIC; current ACIC; strong fixed settings; relaxed admission | Internal attribution and cumulative benefit |
| 7.5 | [RIKEN Graph500-SSSP](https://github.com/RIKEN-RCCS/Graph500-SSSP) | Historical distributed Δ-stepping comparator; local checkout exists, but audit its input compatibility and delta tuning |
| 7.5 | [GAPBS SSSP](https://github.com/sbeamer/gapbs) | Optimized one-node baseline and independent reference |
| Begin during 7.5 | [Gluon-Async/Gluon-Sync](https://iss.oden.utexas.edu/?p=projects/galois/analytics/dist-sssp) | Distributed asynchronous/synchronous comparison |
| Next, one node | [Wasp, SC25](https://research.chalmers.se/en/publication/549745) | Priority-aware work stealing; separate local scheduling efficiency from distributed effects |
| After scope gate | Kernel-specific CPU baseline and a bounded GPU comparison | For retained kernels and claims only |

Explicitly engage [Gluon-Async](https://roshandathathri.github.io/publication/2019-pact):
asynchronous distributed execution with bulk communication already exists on
CPUs and GPUs. The new claim must concern feedback and its measured benefit,
not asynchronous aggregation alone. Discuss distributed control and KLA when
defining the admission/ordering distinction.

Keep the implementation CPU-focused. GPU throughput, high-diameter behavior,
and memory-capacity benefits require experiments. Withdraw claims that road
graphs guarantee CPU wins or that asynchronous scalable GPU processing is
unoccupied. The [Atos scheduler paper is ICPP22](https://escholarship.org/uc/item/9f17k8gk);
its [repository](https://github.com/owensgroup/ATOS) also contains multi-GPU
BFS/PageRank and aggregation. Do not merge results from its different
publications into a single “SC22” comparison. Do not infer energy savings from
edge counts or count an out-of-memory failure as a measured slowdown.

### Graph inputs

The step 5 generators and GAPBS binary reader already exist. Preserve
PE-independent generation and record a canonical graph identity across systems;
matching a generator name and seed is insufficient.

- **Pilot:** mesh, RMAT, uniform, one real weighted road graph, and one social/web
  graph with documented weights. Keep existing small inputs for continuity,
  then add larger inputs that exercise communication and memory.
- **Sources:** several seeds and predeclared multiple sources, with reachability
  reported. Resolve the small-component termination defect before broad source
  selection; do not select sources based on measured speed.
- **Weight sensitivity:** use the existing unit/uniform/file-weight capabilities
  first. Lognormal and degree-correlated weights remain optional hypotheses;
  do not add them solely to enlarge the matrix.
- **Input fairness:** preserve directionality, duplicate handling, weights, and
  source-ID mappings across preprocessing. Validate conversions. Keep graph
  construction, conversion/reordering, solve, and validation times distinct.

Road networks are a test of the mesh hypothesis, not a promised headline win.
The reader is implemented; the missing evidence is real-input performance and
independent validation, not another input library.

### Validation, ablations, and adaptivity

Small-graph Dijkstra/digests, golden inputs, generator portability, and two-node
message-envelope gates exist. The outstanding gaps are termination for small
reachable components, independent cross-system input/result checks, and
validation of large runs. See [the review](post-step7-review.md) for the
shortest-path certificate requirements and measurement protocol.

Compare:

1. Repaired historical settings against the final current configuration directly.
2. One global fixed policy chosen on development inputs.
3. Best measured fixed settings per case, confirmed on separate runs.
4. Admission-only adaptation, communication-only/local adaptation, and joint
   feedback, including an admission × communication-policy interaction matrix.
5. Targeted mechanism ablations for implemented features, including negative
   combining results where they inform the explanation.

Freeze controller constants before held-out graphs, sources, scales, and a
second machine. Report adaptive time divided by the best measured fixed time,
its geometric mean and worst case, tuning cost, and uncertainty. Provisional
engineering targets are within 10% geometrically and 20% per held-out case;
these are not established results or acceptance criteria for SC.

**Fix the process geometry before comparing anything, and say what it is.**
7.6c measured process count at 7–20× at fixed worker occupancy, larger than
every other deployment axis combined and larger than any algorithmic effect
this project has recorded. Two things follow. Every system in a comparison must
be given the same opportunity to choose its layout: the 7.5 pilot tuned RIKEN's
rank count and left ACIC at one process per node, which is not a comparison.
And an A/B taken at one geometry does not transfer to another — 7.6d's width
table reversed on all nine graphs when 120 workers moved from one process into
eight — so a result is quoted with its layout or not at all.

**Report the hang rate beside the speedup, and size the floor at the allocation
being quoted.** Two failures of the campaign design showed up at once in
22071815-18. A configuration that stops sometimes and finishes fast otherwise
scores as a clean win under a median over valid runs: the 7.6e range arm reads
1.21x on mesh20 at one node out of a cell that also hung twice, because the
runs that finished are the runs that extended least. And the `control` arm's
paired ratio against its own unflagged self — the resolution floor — is not
constant across allocations. It is near 1.00x at one node and reaches
1.16x–1.28x at sixteen, so a 16-node cell and a one-node cell of the same value
are not the same claim. Every comparison table carries its failure count and
its floor, per allocation.

Use balanced/randomized variant order, repeated-baseline controls, paired
uncertainty estimates, and multiple allocation blocks. A ~5% bias observed in
the existing harness is a reason to improve the design, not a universal cutoff
for declaring all smaller effects noise. Diagnostic counters can perturb
scheduling; corroborate them with production counters and bounded traces.

Measure actual edge work, message count/size, off-node traffic where available,
memory, and exposed controller cost alongside solve time. Relaxing admission
thresholds still incurs the controller's bookkeeping; label that ablation
accurately. A separate controller-free experiment must preserve progress and
safe termination.

### Scaling and portability

First reproduce one/two-node results and increase through 4/8/16 nodes on Delta,
with larger problems and higher worker occupancy. Audit the starvation gate's
`P × nodes × buffer_size` scaling, the per-idle destination scan, hub handler
durations, and reduction latency. Whole-graph edge balance alone does not
establish balanced active work.

One question is already waiting on that allocation: rmat20 reads 1.59× and
1.55× for the `weight` width rule at sixteen nodes in two independent
campaigns, against resolution floors of 1.14× and 1.22×, while regressing at
two nodes in both. It is the only candidate for a third entry in
`PER_GRAPH_WIDTH_RULE` beyond mesh20 and mesh22, and more repeats at sixteen
nodes cannot settle it — the floor there is too close to the effect. Carry it
as an arm on the first campaign above sixteen nodes rather than as its own.

After Gate A, target 32–64 nodes, then 128–512 only where allocations and the
measured regime justify it. Produce strong and weak scaling; retain efficient
single-node comparisons. Start portability with one additional architecture or
interconnect and frozen policy constants. Frontier/Vista/Anvil are candidates,
not four mandatory campaigns. Confirm access early; node count alone is not a
research result.

---

## Schedule

Steps 1–7 are complete as development milestones. Do not retain calendar blocks
for repeating them. The following is a provisional schedule from 2026-09-13,
with a conservative early-April submission assumption and allocation-dependent
experiment dates.

| Window | Work | Decision/deliverable |
|---|---|---|
| Sep 13, completed | 7.5 comparison pilot: matched inputs, RIKEN/GAPBS/Gluon, 1–16 nodes, occupancy/layout and logging checks | [Performance map and failure ledger](step75-comparisons.md); correctness gate still open |
| Sep 13–14, completed | 7.6a–7.6c: progress repair and bounded diagnostics; controller robustness with geometry frozen; deployment variables separated one at a time | [Progress](step76-progress.md), [controller](step76-controller.md), [deployment](step76-deployment.md). Process count dominates by 7–20×, which reopens every earlier comparison |
| Sep 14–15, completed | 7.6d re-take and the 7.6e range arm, at 1/2/8/16 nodes, eight processes of fifteen, two campaigns | The width rule is a per-case mesh rule and ships as one; raising the clamp mid-run is rejected — it hangs on the graphs it was built for. The campaign also surfaced a deadlock in the default |
| Sep 15, completed | 7.6g: the default deadlock. Cause found on Anvil — heap order against the flagged top slice — and repaired (`--pq-overflow-last`, 12/80 → 0/80 hangs on mesh22); two instrument bugs fixed on the way | Item (1) closes for the stall that reopened it |
| Sep 15, completed | 7.6h: hang rate and resolution floor in every campaign table (`outcomes.py`, `report_arms.py`, `report.py`) | The instrument 7.6f's numbers depend on |
| Sep 16–23 | 7.6f1: external re-take with every system tuning its own layout | Whether the 7.5 comparison survives a fair geometry. Not a result until then |
| Sep 23–Oct 18 | 7.6f2: admission × delivery policies against one global fixed setting and per-case tuned references; one bounded optimization cycle, then stop | **Gate A:** adaptivity and external competitiveness; proceed, narrow, or revisit the mechanism |
| Oct 19–Nov 15 | Minimal controller extraction and BFS transfer; optional necessary payload work | **Gate B:** shared adaptive benefit transfers, or restrict the paper's scope |
| Nov 16–Dec 13 | PageRank if needed for the claim; otherwise strengthen SSSP/BFS evaluation | Freeze algorithm scope; do not add BC/k-core by default |
| Dec–Feb | Scaling, real graphs, selected baselines, ablations, one portability slice, conditional GPU comparison | **Gate C, Feb 15:** evidence sufficient for the chosen claim; no arbitrary 256-node requirement |
| Now–Mar | Outline now; write mechanism and negative results as evidence stabilizes; full draft by Feb 22 | **Gate D, Mar 1:** intended results freeze, leaving March for writing and necessary confirmations |
| Early Apr, provisional | Abstract, paper, AD at the official CFP's respective dates | Re-anchor as soon as SC27 dates are confirmed |
| After acceptance, if applicable | Artifact evaluation under the published requirements | Reproduction material already prepared |

Gate A does not require one RMAT ratio alone to decide the entire project. A
credible high-diameter result may support a narrower paper, but claims covering
scale-free graphs must report their actual performance. If one fixed policy
matches the adaptive method, stop generic refactoring until that result is
understood. One or two more optimization attempts are a bounded investigation,
not an indefinite prerequisite for writing.

The initial baseline setup is complete. Avoid making installation of
seven systems a condition for progress on the observed failures. Record immutable source
versions, build/runtime settings, raw results, and figure recipes as work
proceeds. AD/AE dates follow the official CFP; SC26's AD was due after the paper,
so the old assertion that they necessarily share a deadline is withdrawn.

Describe the repaired 2024 baseline and the new scientific contribution
separately. Bug fixes, input support, and mechanical refactors establish the
foundation; they do not by themselves demonstrate adaptive control.

---

## Verification

“Done” means the relevant evidence exists:

- **Integer-kernel refactors:** identical reference results on one and two nodes,
  including all supported mappings and flag combinations, plus no unexplained
  runtime regression. Hash equality is a regression check, not a substitute for
  independent validation and progress arguments.
- **7.5:** small-component termination and failure handling validated; input
  identities matched; independent result checks for measured runs; paired
  comparisons with allocation/source diversity.
- **7.6 / Gate A:** fixed-policy references and held-out adaptive evaluation;
  mechanism supported by minimally perturbing counters/traces; both runtime
  and work reported. Every comparison states its process geometry, and every
  system in it was allowed to choose one. Results recorded before 7.6c were
  taken at one process per node and are superseded rather than merged.
- **Gate B:** each retained kernel validates independently on at least three
  graph classes; PageRank uses matching semantics and tolerance, not hashes.
- **Campaign:** figures reproducible from recorded raw data and configurations;
  claim/evidence table names limitations and negative results.

Existing A/B design notes remain the experimental history. Where they make
universal claims from a small matrix, use the qualifications in
[post-step7-review.md](post-step7-review.md) when writing the paper.
