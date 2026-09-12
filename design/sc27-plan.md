# ACIC → SC27: research and engineering plan

*Working plan, drafted 2026-09-10. Code references to `htram_group.*` are in the
companion repo at github.com/UIUC-PPL/htram.*

## Context

The IA³@SC24 paper (`~/Downloads/acic_2024paper.pdf`) introduced **ACIC** — Asynchronous
Continuous Introspection and Control — a fully asynchronous distributed SSSP in Charm++
(`~/charm_graph_code/sssp_smp.cpp`) over a bespoke aggregation library (`~/htram`). A
self-perpetuating cycle of reductions and broadcasts builds a global histogram of *active
updates* bucketed by tentative distance; percentile thresholds cut from that histogram gate
which updates enter a PE's priority queue (`heap_threshold`) and which go on the wire
(`tram_threshold`). Result: fewer speculative relaxations than Δ-stepping with no global
synchronization.

Three gaps block an SC-class paper:

1. **Scale-free graphs lose.** RIKEN Δ-stepping is **2.8–3.3× faster** on RMAT. The paper's
   own headline weakness, and the graph class reviewers care most about.
2. **One algorithm, two synthetic graph types, 16 nodes.** No real inputs, **no weighted
   input reader at all** (every weight is `rand()`), no correctness validation of any kind.
3. **The adaptivity claim is half-true.** The abstract advertises "an adaptive aggregation
   library"; htram has *zero* self-tuning. Every knob is a compile-time constant or a fixed
   constructor argument.

**Target:** SC27 full paper. SC27 is Nov 14 2027 in Denver; SC26's paper deadline was
**Apr 27 2026**, so assume **~late April 2027** — roughly 33 weeks from 2026-09-10. Confirm
when the CFP drops and re-anchor the schedule then.

**Claim to defend:** *adaptive introspection co-designed across the algorithm and the
communication layer* — one histogram-driven controller steers both the algorithm's work
admission and the aggregation library's buffering, combining, and flushing — generalizing
across a class of graph algorithms and a range of graph structures.

**The axis the claim lives on is work-efficiency, not throughput.** See the GPU section
below: at the top of the throughput ranking, GPUs win by brute force while doing *more* work.
ACIC's differentiated claim is doing *less* work, plus the graph classes where GPU parallelism
cannot be saturated at all. Every headline result should be reported in both edge relaxations
and wall clock, never wall clock alone.

---

## What planning found: three defects that change how to read the 2024 results

All three verified directly in the code. The first is the most important finding here.

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

**Order of work: solidify SSSP completely, then generalize.** Every scale-free mechanism is
built, measured, and defended on SSSP alone before a second algorithm exists. This costs one
thing — the architecture argument that type erasure should precede combining, so the
combining structure isn't rewritten during the refactor. Reconcile it by writing
`CombiningHold` against a **byte-oriented interface** (`item_size` + an ops struct) from day
one, even while the surrounding library is still monomorphic. The later type erasure then
lifts it unchanged.

**The abstraction is two families, not one.** The `(key, value, ⊕)` combining contract holds
for every algorithm — and combining is safe for both families for *different* reasons worth
stating in the paper: min-family because relax is idempotent and monotone, PageRank because
`+` is exact and loses nothing. But the *controller* splits on idempotence and on
absolute-vs-delta payloads. For PageRank the histogrammed population changes from *in-flight
messages* to *vertex residual mass*, the reduction element becomes `double`, and buckets
become log-scale. So: **two engines sharing `htram`, `acic`, and `graphlib`** — not one class
with four template parameters.

**Type erasure, not full templating, for htram genericity.** Templating the whole class works
(`tramNonSmp.h:74` is the proof) but kills the static archive, gives one `HTramNodeGrp` per
payload type, and forces `.ci` instantiation gymnastics on every application. Instead: a
byte-oriented `HTramCore` + a header-only `HTram<T>` façade + a POD `HTramOps` vtable,
matching the existing `set_func_ptr_retarr` style. Recover lost inlining with a `BuiltinOp`
enum (`MIN_I64`/`SUM_F64`/…) switched on *outside* the insert loop.

Target layout:

```
htram/     htram_core.{h,C,ci}  htram_ops.h  htram.h (HTram<T> façade)
           htram_hold.{h,C}     htram_combine.{h,C}
acic/      acic.h  acic_controller.{h,C,ci}      # AcicController group + AcicMaster chare
graphlib/  graph_types.h  partition.h (Locator)  edge_source.{h,C}  csr_builder.{h,C,ci}
engine/    update_engine.h  algo_{sssp,bfs,cc,pagerank}.h  apps/
```

---

## Reconverse port

Mechanically a Charm++ build swap, but your own accumulated notes say the sharp edges are all
in the runtime configuration, and several bear directly on this paper. Do it in P0, before any
baseline numbers are recorded, so nothing has to be re-measured later.

- **Build:** pass runtime options via `--with-cmake-args`; a bare `-DFOO=BAR` silently becomes
  a compiler flag instead.
- **Launch:** `lcrun`, not `mpirun`.
- **Tracing is the real risk.** Trace flags leak into `argv` under Reconverse — `+traceroot`
  and `+logsize` broke NAMD outright. The plan leans on Projections for the scale-free
  diagnosis (P1), so resolve this first: either fix the argv leak upstream or set trace
  parameters by environment. Also keep traces short — long traces flush mid-run and inflate
  the measurement by ~30%.
- **`+lci_ndevices` is a real experimental factor, not a constant.** Its optimum is U-shaped
  (≈8 at 2 nodes, ≈32 at 16), and the RMR buffer caps devices — 64 B holds only 2, so
  `>=3` aborts unless that is raised. Pin it and sweep it as a controlled factor.
- **`LCI_ATTR_PACKET_SIZE` interacts directly with the paper's central knob.** Raising it to
  32768 closed a 2-node performance cliff elsewhere (12.0 → 7.2 ms/step). An aggregation
  buffer-size sweep run at the wrong packet size measures the wrong optimum — so this is
  simultaneously a **confound to control** and a **potential contribution**: co-tuning the
  aggregation layer against the transport's packet size is exactly the kind of cross-layer
  adaptivity this paper claims. Worth an explicit experiment.
- **Diagnose with traces, not profiles.** Prior Reconverse work found idle cycles are
  effectively free (removing 74%/20%/45% of cycles bought 0%), and that a multi-node gap that
  looked like idleness was 77% waiting on *same-node* message delivery. ACIC's tail (H4 below)
  has the same shape — don't conclude from a profile that the tail is idle-bound.

---

## Work plan

Steps 1–7 are SSSP-only. Steps 8–12 generalize. Every step ends in a runnable binary
producing a number; steps 1–4 and 8–9 gate on a **byte-identical `--verify` hash** so the
baseline never moves under you mid-refactor.

| # | Step | Gate | Effort |
|---|---|---|---|
| **1** | Reconverse port; `lcrun` job scripts; resolve the trace-flag argv leak; pin `+lci_ndevices` / `LCI_ATTR_PACKET_SIZE`. CMake replacing the two hardcoded `charmc` paths | 2024 configs run and reproduce on the new stack | 1 wk |
| **2** | `--verify`: reduce a 64-bit hash of all `(v, dist)`; serial Dijkstra reference ≤1M edges; CI on every commit. Deterministic weights via `hash(u,v,seed)` replacing `rand()` | Reference matches on mesh + small RMAT | 2 days |
| **3** | The defect list above: `updates_in_tram` leak, `tram_hold` rows → `num_dest`, `dest_table` overflow, `first_nonzero` OOB, `rand()` flush, `fast_exit` → flag, `NODE_COUNT` → runtime, delete `processHeapShared` | Verify passes. **Measure the §1 fix alone and re-run the `p_tram` sweep** | 3 days |
| **4** | Genuine varsize messages (`char buffer[]`); `setUsersize` on all send paths. Re-run the buffer-size sweep, now co-varied with `LCI_ATTR_PACKET_SIZE` | ~~Verify identical; wire bytes drop at `bufSize < 2048`~~ **Done.** Verify identical; second clause withdrawn — `bufSize` never reached the wire because the constructor argument was dead, not because of padding. Closed on 2 nodes by `scripts/verify_2node.sh`; see `design/varsize-messages.md` | 3 days |
| **5** | Retire all non-SMP and dead code (below). `graphlib` v1: in-memory Kronecker/RMAT/uniform/mesh generators + GAPBS `.sg`/`.wsg` binary reader; flat CSR replacing per-vertex `std::vector` | **Done.** Verify identical on all ten pre-existing configurations; the gate now runs 18, adding RMAT and files. A generated graph written to `.wsg` and read back solves to the same digest as the in-memory run. See `design/graphlib.md` | 2 wk |
| **6** | Scale-free diagnosis: H1–H4 below, each an A/B with everything else fixed | **Done.** Four notes written. **Three of the four hypotheses are refuted for RMAT and confirmed for the mesh** — they are real problems of the class ACIC already wins on. Only H2 survives as a scale-free explanation. See `design/scale-free-diagnosis.md` | 2 wk |
| **7** | Build the winners, **in the order step 6 established, which is not the order below**: (1) ~~adaptive flush cadence — 3.2× on the mesh from one existing constant~~ **Done: `--flush-policy adaptive`, default. 3.6× on the mesh on one node, 3.8× on two, no slower on RMAT or uniform. The ungated first attempt cost 7–12% on two nodes and is kept as `stale`; see `design/step7-flush-cadence.md`**; (2) ~~`CombiningHold` (byte-oriented from day one) + `on_absorb`, *before* the batch-local fold in `deliver`, whose reach shrinks with problem size~~ **Done, and both lose: `--combine hold` 1.17–1.39× slower on RMAT, `--batch-fold on` 1.09–1.14×; both off by default. Folding removes cheap receiver rejects, not relaxations, and under WPs a delivered batch is one source's message, so the fold and the hold catch the same same-source redundancy and the fold finds zero with the hold on; see `design/step7-combining.md`**; (3) ~~adaptive bucketing, worth 1.7× on the mesh and 9% on RMAT~~ **Done: `--bucket-policy adaptive --bucket-target 8`, default. 0.88× on the mesh and 0.90× on RMAT at 2^20, nothing resolvable at 2^22 or on the uniform graph. Step 6's 1.7× was the controller switching off on a cadence-bound run and did not survive 7.1. The harness has a ~5% position bias, now measured with a control variant; see `design/step7-bucketing.md`**; (4) idle flush. **Not** measurement-based load balancing | Verify identical flag on **and** off | 4 wk |
| **8** | Type erasure: `HTramCore` + `HTram<T>` + `HTramOps` + `BuiltinOp`. Strictly mechanical | **Byte-identical verify vs. step 7** | 1 wk |
| **9** | `Locator` + `dest_slot` item field; fix `thisIndex`/`CkMyPe()` conflation. K=1 `BlockLocator` → identical; then K=8 `HashLocator` | Verify identical at K=1 | 1.5 wk |
| **10** | `AcicController` extraction (priority-rank space, watchdog, round-time metric) | Verify identical for SSSP | 1 wk |
| **11** | BFS + CC on the engine | Validate vs. GAPBS | 1.5 wk |
| **12** | PageRank: second engine (delta, non-idempotent, mass histogram, log buckets, descending). Then k-core / BC if Gate B clears early | Convergence vs. GAPBS reference | 3 wk |

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

### Combining — the paper's central mechanism

Key per **`dest_node` with bucket as a field**, not per `(dest_node, bucket)`: cross-bucket
folding is where the win is, since `(v,100)` and `(v,50)` live in different buckets and
per-bucket tables can only fold items that already agree. Open-addressed, power-of-two, 8-bit
tag prefilter (64 tags/cache line → one miss for a negative lookup), intrusive bucket index,
**lazy decrease-key**: on improvement update `bkt[i]` and push onto the new bucket list
without unlinking; `release` skips entries where `bkt[i] != b`. O(1), no allocation.
Break-even absorb rate ~8–12%.

Two corrections to my initial framing, both important:

- **`on_absorb` is a correctness requirement, not a metric.** The app owns `histogram[]` and
  `updates_created` (`sssp_smp.cpp:1099-1100`); silently destroying items makes the
  termination predicate `processed - created == 1` (`:477`) unreachable and the run hangs.
- **Source-side combining captures only half the redundancy.** On power-law graphs the
  dominant redundancy is *across* source PEs. Add a batch-local fold in the `deliver` callback
  (~30 lines, fits in L2) and **report the two contributions separately**.

The adaptive on/off policy — disable per destination below an 8% trailing absorb rate,
re-probe one round in 16 — is itself a figure: *"the system turns combining off on road
networks and on for RMAT, automatically."*

> **Step 7.2 result: built as specified, correct, and a net loss.** Absorbing an
> update removes a receiver reject, which costs one comparison, and does not
> reduce relaxations. RMAT's are unchanged and the mesh's rise. The second
> correction above does not hold for this library: a delivery callback under WPs
> sees one source PE's message, so the batch-local fold reaches only same-source
> redundancy, which is exactly what the hold already catches. The 8% adaptive
> on/off policy is moot, since no absorb rate observed (up to 51%) paid. See
> `design/step7-combining.md`.

Measure the ceiling before writing any of it: `sssp_smp.cpp:1257` already counts
`rejected_updates` and `:588` prints it normalized to `|E|`. Run it on RMAT after step 3.

### Why scale-free loses — four hypotheses (step 6)

> **Answered. `design/scale-free-diagnosis.md` carries the result; the four
> notes carry the evidence. The short version is that the hypotheses below were
> asked of the wrong graph class.**
>
> H1, H3 and H4 are each **refuted on RMAT and confirmed on the mesh**. The
> controller has *more* bucket resolution on RMAT (18.5 buckets above the
> frontier per round) than on the uniform graph (9.7) or the mesh (2.2, with 60%
> of rounds having none). A 1-D partition of a power law is imbalanced 1.19× at
> 32 PEs, which the calibration shows costs nothing; the mesh is balanced 1.00×
> in edges and idles its PEs 85% of rounds — **including on one PE, where there
> is no partition**. And flushing the aggregation buffers every round rather
> than one in five is 3.2× faster on the mesh and 4% *slower* on RMAT.
>
> **The finding no hypothesis anticipated:** on a power-law graph the redundant
> work is not order-dependent, so no ordering can prevent it. Switching ACIC's
> work admission off entirely (`p_heap` 0.005 → 0.999) changes RMAT's rejected
> updates by nothing measurable, 1.161 → 1.129 per edge, and makes the run 1.14×
> *faster* because the bookkeeping is not free. The same switch costs the mesh
> **5.5×** its rejected updates. ACIC's central mechanism is not mistuned on
> scale-free graphs — it has nothing to bite on, because hub contention puts the
> competing updates in the same bucket at the same time. Which leaves combining,
> H2, as the only remaining line of attack on the class the paper has to explain.

> **Two corrections found while building the instrument, both of which change
> how the hypotheses below must be read.**
>
> **The uniform mode's partition is deliberately skewed and no other mode's is.**
> Mode 1 draws each PE's share of the vertices at random within ±20% of `V/N`
> (`sssp_smp.cpp`, the `partition_rng` block); the mesh and RMAT both divide
> evenly. At 16 PEs that injected skew measures 1.30 max/mean in edges — the
> same order as anything the power law produces. So *any* load-balance
> comparison between the uniform graph and a scale-free one, including
> whatever informed H3 below, was comparing a deliberately imbalanced partition
> against an even one. `--partition-jitter` now exposes it; H3 runs every mode
> at 0 and uses the jitter as a calibration.
>
> **The combining-adaptivity figure as drafted is not what the system does.**
> The combining section below proposes presenting the policy as *"the system
> turns combining off on road networks and on for RMAT, automatically."* The
> mesh's batch-local absorb rate is **46%, higher than RMAT's 41.6%**, so a
> policy keyed on absorb rate switches combining *on* for the high-diameter
> graph too. The two rates have unrelated causes — RMAT's is structural
> (hub in-degree), the mesh's is temporal (a narrow frontier revisited) — and
> `<prefix>.arrivals.csv` separates them. The defensible claim is narrower:
> combining pays wherever traffic concentrates, and traffic concentrates for
> two different reasons. See `design/h2-hub-redundancy.md`.

- **H1 — bucket width has no resolution on RMAT.** *(**Refuted.** RMAT and the
  uniform graph occupy 199 and 204 of the 2048 buckets respectively and
  concentrate half their updates into the same 36; the percentile knob moves
  RMAT's runtime by 1.17× and its redundant work not at all. Deriving the width
  from the distribution is still worth doing — 1.7× on the mesh — but it is not
  the scale-free fix. [Step 7.3: after 7.1's flush fix it is worth 0.88× on the
  mesh and 0.90× on RMAT at 2^20, and nothing at 2^22. The 1.7× was measured
  while the mesh was cadence-bound; see `design/step7-bucketing.md`.] Note also that `histo_reduction_width` (256) exceeds the
  entire occupied range on both random-graph classes, so the sliding window
  never slides on either. See `design/h1-bucket-resolution.md`.)*
  `bucket(d) = d/log(V)` (`:843`, which
  reduces algebraically to `1/log(V)`) is fixed at startup and derived from nothing but `|V|`.
  RMAT's small diameter collapses the range into a handful of the 2048 buckets, so percentile
  thresholds have nothing to cut and the controller degenerates toward plain distributed
  control. Cheapest test in the plan: log bucket occupancy for RMAT vs. random. Fix: derive
  width from the observed distribution, and use `lmax` — already reduced in `begin()` (`:362`)
  and then discarded.
- **H2 — redundant updates to hubs.** *(Supported, with the framing correction
  above. On RMAT the reject rate climbs monotonically with destination
  out-degree, 27% at degree 0 to 99.9% above 4096, and 59% of all rejects land
  on the 2.9% of vertices with degree ≥128; on the uniform graph the same rate
  is flat to a tenth of a percent across every degree class, which is the
  control behaving as it must. Batch-local combining alone absorbs 41.6% at
  bufSize 2048 against an 8–12% break-even. See
  `design/h2-hub-redundancy.md`.)* Addressed by combining, above.
- **H3 — 1D partitioning imbalances on a power law.** *(**Refuted as a spatial
  problem.** 1.19× max/mean in edges at 32 PEs, in a region a deliberate-skew
  calibration shows costs nothing measurable; RMAT's PEs are idle 9% of rounds
  against the mesh's 85%. The expectation below that migration LB is a poor fit
  is confirmed and now measured — but it is a statement about high-diameter
  graphs. See `design/h3-partitioning.md`.)* The **overdecomposition** half will
  likely pay, but via `HashLocator` scattering hubs and via more schedulable work overlapping
  the `[whenidle]` drain — *not* via load balancing. Measurement-based migration LB is a
  dubious fit: SSSP imbalance is temporal (the frontier moves), so past load anti-predicts
  future load. Build the migration path because it's cheap and unblocks routing; expect the LB
  result may be negative and write it up honestly. Your own
  `~/paratreet2/design/uf2-k-chares.md` reached this shape of conclusion for UnionFindLib.
- **H4 — the tail advances only at reduction cadence.** *(**Confirmed far more
  strongly than stated, and it is not the tail and not RMAT.** On the mesh the
  whole run is cadence-bound: 4 ms of added round delay multiplies runtime 27.7×
  on one node and 38.5× on two, with the round count unchanged, and flushing
  every round instead of one in five is 3.2× faster. The tail as defined — after
  99% of vertices settle — is 3.2% of mesh runtime, so chasing it would have
  been chasing the wrong few percent. On RMAT buffers fill on their own and
  flushing every round is 4% slower. See `design/h4-tail-cadence.md`.)* Re-enable htram's idle-triggered
  partial flush (`IDLE_FLUSH` is `#if`'d out at `htram_group.h:7`; `idleFlush()` is a stub),
  make the cadence adaptive, and replace the two-tier `histogram_sum <= N*100` hack (`:494`)
  with a controller over histogram *shape* — exactly what the 2024 future-work asks for.
  Diagnose with traces: prior Reconverse work found a gap that looked idle was really
  same-node message delivery.

---

## Algorithm portfolio

**Frame the portfolio as the GAP Benchmark Suite.** GAPBS specifies exactly six kernels — BFS,
SSSP, PageRank, Connected Components, Betweenness Centrality, Triangle Counting — which is
precisely the list you asked for. That gives the paper a standard, recognized scope statement,
reference implementations for validation, and a single-node efficiency baseline, all for free.

| Tier | Kernel | ⊕ | Priority | Effort | Role |
|---|---|---|---|---|---|
| Core | SSSP | min | ascending | steps 1–9 | Reference; tests the controller |
| Core | BFS | min | ascending level | ~1 wk | Reuses `Update` verbatim |
| Core | Connected components | min label | — | ~1 wk | **Combining-isolation experiment** |
| Core | PageRank (async push) | **sum** | **descending** | ~3 wk | Proves ACIC isn't just label-setting |
| Stretch | Betweenness centrality | sum | two-phase | ~3–4 wk | Needs predecessor storage + reverse traversal |
| Stretch | k-core | min | ascending | ~2 wk | Monotone; fits. Not a GAPBS kernel — use only if BC slips |
| Cut | Triangle counting | set intersection | none | unbounded | See below |

**Reposition CC.** Its priority key is a vertex ID, not a cost, so the histogram doesn't drain
left-to-right and ACIC has no principled progress metric for it. That makes it your *cleanest*
isolation experiment for combining (huge hub redundancy, no useful prioritization). Frame the
portfolio as a 2×2 — does prioritization matter? does combining matter? — rather than six
instances of one pattern.

**Triangle counting should be a limitations paragraph.** It is not a prioritized-update
algorithm; it needs neighbor-*list* exchange and set intersection, which means a second
communication primitive inside htram serving nothing else in the paper. "The framework covers
commutative-combine vertex programs; set-intersection workloads require a different
aggregation primitive" is stronger than a weak sixth benchmark. Because GAPBS frames the
scope, saying "five of the six GAP kernels, and here is precisely why the sixth does not fit"
is a *crisper* claim than an unbounded one. Revisit only if Gate B clears early.

---

## Evaluation

### Baselines

| Kernel | Distributed CPU baseline | Single-node reference | GPU baseline |
|---|---|---|---|
| SSSP | RIKEN Graph500-SSSP Δ-stepping (integrated); D-Galois/Gluon | GAPBS (Δ-stepping) | Gunrock; D-IrGL |
| BFS | D-Galois/Gluon (direction-optimizing); Graph500 ref | GAPBS (direction-optimizing) | Gunrock; **Atos**; D-IrGL |
| CC | **FastSV** (CombBLAS); D-Galois CC | GAPBS (Afforest); ConnectIt | Gunrock; D-IrGL |
| PageRank | D-Galois/Gluon; Gemini | GAPBS (pull-direction) | Gunrock; **Atos**; D-IrGL |
| BC | D-Galois distributed BC (UT Austin ISS); MFBC | GAPBS (Brandes) | Gunrock |

**D-Galois/Gluon is the primary multi-algorithm distributed baseline** — it covers four of the
five kernels in one system, and Gluon reports ~3.9× over Gemini, so it is the number to beat.
**GAPBS is the single-node reference for all of them**, which SC reviewers routinely demand to
rule out "a fast parallel version of a slow code." **Gunrock, Atos, and D-IrGL/Gluon-GPU are
run, not merely cited** — reviewers discount cross-paper comparisons on different hardware.
Running D-Galois and D-IrGL together is especially clean: both halves of one system, so the
CPU/GPU difference is not confounded by a framework difference.

**Positioning — the adjacent work to engage with directly:**

- *Wasp* (SC'25) attacks the same target as ACIC — reducing parallelism-induced redundant work
  in SSSP via asynchrony and priority-aware work stealing, 1.38–4.7× over prior work — but on
  shared-memory multicore. Closest competing *idea*. Distinguish: ACIC's introspection is
  global and distributed (reductions across nodes) rather than local work stealing, and it
  steers the communication layer, for which a multicore system has no analogue.
- *Atos* (SC'22) is the closest competing *system*: asynchronous, synchronization-free,
  PGAS/NVSHMEM, and it implements a concurrent communication aggregator for InfiniBand — which
  is htram's function, on the GPU. Beats Gunrock, Groute, and Galois. Distinguish on two
  points, both of which the paper itself supplies: it has **no work-admission mechanism** (its
  persistent-warp version admits to generating **3.5× more work** than Gunrock and wins anyway
  on raw throughput), and it **does not scale** — 4 GPUs on NVLink, 8 on Summit, with scaling
  dropping past 3.
- Also: distributed control (Zalewski — the async baseline ACIC is measured against), KLA,
  ConnectIt/Afforest (CC), and Gluon (the closest thing to a competitor for htram itself).

### GPUs: threat assessment, and the two axes where CPU wins

**Decision: the implementation stays CPU-only; GPUs enter as baselines and as positioning.**
A full hybrid ACIC is realistically a second paper and does not fit alongside the scale-free
fix, the refactor, and four kernels. But the GPU question cannot be dodged — a reviewer will
ask it, and on raw throughput the honest answer for some kernels is "GPUs win."

Per-kernel threat, and what it implies:

| Kernel | GPU threat | Why | Implication |
|---|---|---|---|
| BFS | **High** | Graph500 BFS #1 (Nov 2025) is eos-dfw, 1024 NVIDIA H100 nodes, ahead of CPU Fugaku at 152K nodes. Direction-optimizing BFS is regular and high-parallelism | Do **not** contest raw TEPS on low-diameter graphs |
| PageRank | **High** | Dense, regular, iterative — near-perfect GPU fit. Atos beats Galois/Gunrock/Groute | Compete on work and on memory footprint, not throughput |
| Triangle counting | **Very high** | GPU set-intersection (TRUST et al.) dominates | Independently reinforces cutting TC |
| SSSP | **Medium** | The ordering constraint fights GPU parallelism; GPU Δ-stepping inherits the same speculation problem, worse | Genuinely contestable |
| CC | **Medium** | Afforest-style sampling is GPU-friendly, but label propagation converges irregularly | Contestable; also the combining-isolation experiment |
| BC | **Medium-low** | Two-phase, predecessor storage, memory-bound | Contestable |
| **High-diameter / road** | **Low, across every kernel** | Insufficient parallelism to saturate even *one* GPU | **The structural refuge** |

Two defensible axes, both of which the competing literature hands you:

1. **Work-efficiency, not throughput.** Atos's own paper reports its persistent-warp version
   doing 3.5× the work of Gunrock and winning anyway on brute-force throughput. That is the
   whole GPU strategy stated plainly: buy speed with wasted work. ACIC's thesis is the exact
   opposite, so report edge relaxations alongside every wall-clock number and make
   relaxations-per-result a first-class figure. This also connects to energy, which is
   increasingly a reviewable axis.
2. **Graph classes and problem sizes GPUs cannot serve.** High-diameter graphs cannot saturate
   one GPU. And graphs exceeding aggregate GPU memory force host-memory staging that erases
   the throughput advantage — worth an explicit experiment, since ACIC's CPU-side memory
   capacity is a genuine structural advantage at the sizes this paper targets.

There is also a scaling story in the competitors' own numbers: the async GPU work (Atos) tops
out at 4–8 GPUs with scaling degrading past 3, while the GPU systems that *do* scale
(Graph500 leaders) are bulk-synchronous. **Asynchronous-and-scalable is currently unoccupied
territory on GPUs** — which is both the right framing for this paper's contribution and the
natural follow-on paper. Say so in future work; don't try to build it now.

### Graph inputs

Two paths, deliberately:

- **Generated in memory, for scale.** Kronecker/RMAT with Graph500 parameters, seeded
  per-reader so the split is deterministic and reproducible across PE counts. No I/O, no
  storage, no staging — this is what makes 512-node runs practical, and it is how the largest
  scaling curves get produced.
- **Read from disk, for realism.** GAPBS `.sg`/`.wsg` binary only. **GAPBS ships a `converter`
  tool**, so let it handle every text format (SNAP, DIMACS, MatrixMarket) offline and read one
  well-defined binary layout at scale. That collapses the I/O work to a single binary reader
  plus generators and removes the entire text-parsing surface from the critical path. Verify
  the exact header layout against GAPBS `Writer::WriteSerializedGraph` at implementation time.

Real graphs to carry: GAP `twitter`, `web`/sk-2005, `kron`, `urand`, and **`road`** — the
DIMACS USA road network is genuinely weighted, high-diameter, and the class where an
asynchronous approach should beat every bulk-synchronous baseline. It is currently untestable
and is the **most likely source of a headline win**. Keep the mesh generator: it is the only
ground-truth-checkable input.

Make `WeightAssigner` orthogonal to topology (`UNIT`/`UNIFORM`/`LOGNORMAL`/`DEGREE_CORRELATED`/
`FROM_FILE`, deterministic in `(u,v,seed)`). This separates "RMAT is hard because of topology"
from "RMAT is hard because of the weight distribution" — a question a reviewer will ask
specifically, since the bucket width is tuned to weights in `[1,1000]`.

### Validation, ablations, scaling

Nothing exists today beyond one reduced scalar (`get_max_cost`, `:1578`); `print_distances`
prints nothing (`:1533-1539` commented out). Needed before any performance number: serial
Dijkstra, then GAPBS reference output for every kernel.

Ablations, one per adaptive mechanism: thresholds off (= distributed control), combining off
(and each combining point separately), adaptive bucketing off, overdecomposition off, adaptive
htram buffering off, tail-mode off, and **`LCI_ATTR_PACKET_SIZE` co-tuning off**. Plus one
GPU-facing experiment: scale a graph past aggregate GPU memory and show where the GPU
baselines' throughput advantage collapses into host-memory staging.

Scaling: strong and weak, 1 → 512 nodes on Frontier (1024 if the allocation allows), plus a
portability slice across Frontier (Slingshot), Delta, Vista, and Anvil to show the adaptation
is not tuned to one interconnect.

---

## Schedule

Anchored on an assumed **Apr 27 2027** deadline (~33 weeks). Writing starts at Gate B, not
after the campaign, and results freeze with 3.5 weeks of slack.

| Phase | Window | Steps | Gate |
|---|---|---|---|
| **P0 Foundation** | Sep 10 – Oct 23 | 1–4: Reconverse, verify harness, defects, varsize | §1 fix quantified; `p_tram` and buffer sweeps re-run on the new stack |
| **P1 Solidify SSSP** | Oct 19 – Jan 15 | 5–7: retire dead code, graphlib, diagnose, build the winners | **Gate A (Dec 18):** RMAT within 1.5× of Δ-stepping at 16 nodes, or re-scope |
| **P2 Generalize** | Jan 11 – Mar 12 | 8–12: erasure, locator, controller, BFS/CC/PR (+BC) | **Gate B (Feb 12):** 4 kernels validated vs. GAPBS |
| **P3 Adaptive htram** | Feb 1 – Mar 19 | Self-tuning buffer size + flush cadence; transport co-tuning | Within X% of the hand-tuned optimum without being told |
| **PB Baselines** | Nov 2 – Mar 5 | Background track: stand up RIKEN, D-Galois, GAPBS, FastSV, then Gunrock, D-IrGL, Atos | All baselines runnable on ≥1 machine before Gate C |
| **P4 Campaign** | Mar 1 – Apr 9 | Scaling, real graphs, ablations, baselines, portability | **Gate C (Mar 19):** 256+ node data in hand |
| **P5 Writing + AD** | Jan 15 – Apr 27 | Outline at Gate B; full draft Mar 26; AD appendix | **Gate D (Apr 2):** results frozen, writing only |
| **P6 Artifact Eval** | Jul – Sep 2027 | Post-acceptance AE badge process | Reproducibility scripts already written in P4 |

Three scheduling notes. **The AD appendix ships with the paper** (April) while **AE evaluation
is post-acceptance** (summer) — so the pre-deadline budget only needs the AD, but it should be
written against artifacts that are already scripted. Build for that during P4 rather than
retrofitting: every figure regenerable by one command, raw data committed, and the machine
config directory from step 1 doubling as the AE environment description.

**PB is a deliberately separate background track**, because standing up seven baselines — three
of them GPU, one of them research code — is weeks of work that is *fully independent* of ACIC
development and must not sit on the critical path in March. Sequence it by difficulty: RIKEN
(already integrated) → GAPBS (trivial, and it unblocks validation in P2) → D-Galois →
Gunrock → D-IrGL (same stack as D-Galois, so mostly free) → FastSV → Atos last, since it is
the least-maintained and only strengthens positioning rather than gating a result. If Atos
resists, cite it and move on — the argument against it rests on numbers printed in its own
paper, not on a re-run.

**Own step 3 in the paper.** "We found and fixed a control-path pathology; all numbers below
are against the fixed baseline" is far better than a reviewer finding it.

---

## Verification

"Done" means measured, not built:

- **Steps 1–4, 8–10:** `--verify` hash matches the pre-change binary on a fixed input set, in
  CI. This is what makes a refactor safe on a paper deadline.
- **P0:** serial Dijkstra agrees on mesh + small RMAT; the 2024 random-graph speedup
  reproduces within noise on the Reconverse stack; a weighted road graph loads and validates.
- **P1:** each hypothesis gets an A/B with everything else fixed, written up as a design note;
  Projections traces confirm the *mechanism*, not just the wall clock.
- **P2:** every kernel validates against its GAPBS reference on ≥3 graph classes before any
  performance number is quoted.
- **P3:** the self-tuning claim is checked against a hand-tuned sweep — and that sweep has to
  exist for the claim to be makeable.
- **P4:** every figure regenerable from the experiment driver by one command, raw data
  committed. This doubles as the AD/AE artifact.

Reuse the campaign methodology already proven in `~/paratreet2/design/` — those A/B notes and
campaign reports are the right template for P1 and P4.
