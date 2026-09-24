# ACIC current evidence

*Status at repository revision `48b6c2b`, 2026-09-23; Wasp, RIKEN and scaling
evidence added 2026-09-24. This document replaces
the individual experiment narratives. Raw summaries remain under
`design/onenode-data/`; job logs remain in the recorded campaign directories;
the deleted narratives remain in Git history.*

## Executive conclusion

*Reporting rule (2026-09-24): every comparison is a speedup, the reference
time divided by ACIC's (or the named arm's) time. Above 1× means ACIC is
faster; below 1× means it is slower.*

ACIC has one accepted performance result. On `mesh26-z`, the frozen candidate
has a 1.21–1.41× speedup over tuned one-node GAPBS at eight Anvil nodes and
1.25–1.44× at sixteen Frontier nodes. Both cells use 896 PEs, four held-out sources and two
allocations. Removing heap slice 8 loses the win, so the cadence mechanism is
causal rather than a favorable baseline draw.

Against tuned Wasp, the stronger modern multicore baseline, the Frontier cell
is narrower: the speedup over Wasp is 0.96–1.24× by source (median 1.08×) in
both allocations, so ACIC is faster on three of four held-out sources and
slower on one (0.96–0.98×). The claim against the best multicore code is a narrow median
win, not a win on every source.

The result is specific to large sparse meshes. On held-out road sources at 16
Frontier nodes, ACIC's speedup is 0.72–0.84× over tuned GAPBS and 0.39–0.50×
over tuned Wasp, so road misses the plan's local-efficiency gate (within 1.5×
of the faster of the two, a speedup of at least 0.67×). On the scale-free
graphs at 16 Frontier nodes the speedup over RIKEN is 0.77–0.85× on `uniform25`
and 0.28–0.48× on orkut and RMAT 25–27 (Anvil's older comparison: 0.27–0.53×).
No measured live adaptive policy causes the accepted result.

## Accepted result matrix

| Regime | ACIC result | Interpretation |
|---|---|---|
| `mesh26-z`, Anvil | 8 nodes, 16 processes/node × 7 workers/process; speedup over GAPBS 1.21–1.41× | Accepted win on four held-out sources in jobs 20866513/20866514. GAPBS references are jobs 20866515/20866516 plus the earlier fixed-setting cells. |
| `mesh26-z`, Frontier | 16 nodes, 8 × 7; speedup over GAPBS 1.26–1.43× (jobs 5534022/5534023) and 1.25–1.44× (scaling jobs 5536321/5536322); over Wasp 0.96–1.24× (median 1.08×) | Independent-machine reproduction at the same 896 PEs. Eight Frontier nodes give 0.90–0.95×, near parity rather than a win. |
| `mesh24-z`, Frontier | Fixed candidate, 1–16 nodes; best is 16 nodes with speedup 0.85–1.02× over GAPBS (median 0.92×) and 0.72–1.00× over Wasp (median 0.89×) | No win on a quarter-size mesh, as predicted. Jobs 5538412/5538413 (held-out, two allocations), GAPBS 5538410, Wasp 5538411. |
| `road-usa-z`, Anvil | Best fixed width/cap arms reach a speedup of about 0.67× over GAPBS | Width 32K reaches about 1.5 attempts/edge but needs 889–1,444 rounds. Width 128K trades 1.7–2.0 attempts/edge for 520–800 rounds and ties cap 7 with 40–60% less work. Jobs 20868020–22 and 20876828–30. |
| `road-usa-z`, Frontier | Width 128K: speedup 0.66–0.69× over GAPBS | Two allocations agree within about 1%; jobs 5534016/5534017. Training sources, 8 nodes. |
| `road-usa-z`, Frontier held-out | Width 128K, 16 nodes: 0.146–0.148 s; speedup 0.72–0.84× over GAPBS (median 0.78–0.79×), 0.39–0.50× over Wasp (median 0.46–0.47×) | Jobs 5538405/5538406, four held-out sources, two allocations within 1%. 16 nodes has a 1.14–1.16× speedup over 8; width 128K has a 1.45–2.15× speedup over the plain mesh candidate, which does 10.8–11.0 attempts per edge against 3.1. |
| Scale-free suite | ACIC speeds up from 2 to 8 nodes, but its speedup over RIKEN is 0.27–0.53× | Last complete 8g comparison at application revision `de0ed1c`. The newer high-diameter paths resolve inactive, but the formal frozen-binary regression is incomplete. |
| Scale-free suite, Frontier | 16 nodes, current candidate, layout tuned on training sources; speedup over RIKEN 0.77–0.85× (`uniform25`), 0.28–0.48× (orkut, RMAT 25–27) | Held-out sources, two allocations, jobs 5538389–5538392 against RIKEN 5536474/5536475. A counter-regime result: ACIC loses on every source. |

## Mechanism chain established September 18–23

### 1. Scale-free work growth was repaired

Lazy heavy-edge relaxation, an htram hold bitmap, revised idle flushing and
empty-delivery suppression changed the scale-free trend. ACIC gained a 1.1–2.3×
speedup from two to eight nodes on `rmat25`, Orkut, `rmat26` and `rmat27`.
RIKEN retained a substantial lead, so these changes are a regression defense
and scaling repair rather than a winning scale-free result.

### 2. Sparse one-node execution was reorganized

Reader tiling and process-shared state exposed more useful parallelism on mesh
and road. Work-cost instrumentation then showed that redundant edge attempts
and queue operations, rather than one unnamed Charm++ overhead, dominated the
remaining distributed cost.

Process-wide nearest priority sharply reduced attempts. Removing one entry at
a time was expensive, while batches of eight retained the ordering benefit and
produced large local gains. The frozen batch-8 source is `48ca9c0`; its Anvil
production and diagnostic hashes begin `bd07ed61` and `6daa7dba`.

### 3. Global concurrency was isolated

The fixed-total experiment changed physical placement, priority-domain count
and workers/process separately. Only workers/process crossed the noise rule:
raising 7 to 15 increased road work 1.59–1.84×. Placement and domain count were
within noise. The two performance allocations were jobs 20833718/20833719;
the 224-solve gate was 20833717. Machine-readable audits are
`design/onenode-data/r1-attrib-anvil-20833718.json` and its matching files.

### 4. A runtime scheduler regression was found

Reconverse commit `146ec42` registered queues with a new scheduler. The changed
polling/order adds 30–45% road work, and the registered scheduler's speedup
over `+old-scheduler` is about 0.69–0.77× on road. On the same current runtime,
`+old-scheduler` matches the earlier v0916 behavior within the control floor.
Jobs 20841653/20841654 contain the matched comparison. Every current
performance run therefore uses `+old-scheduler`.

### 5. Heap cadence produces the mesh win

A fixed process drain cap controls road speculation but hurts mesh. Heap slice
8 instead yields to the scheduler after eight removals, allowing messages and
other tasks to interleave. It reduces distributed mesh rework enough to beat
GAPBS at 896 PEs on both machines. On one Frontier node, where cross-process
rework is small, the slice's speedup is 0.93–0.95×; activation is graph/scale
dependent.

### 6. Road becomes round bound after ordering

The default 2,048-bucket histogram covers only about 35K distance units on
road, while observed distances reach 37–54M. The frontier therefore sits in
overflow and supplies almost no global ordering. A fixed wider bucket makes the
range representable and reduces speculation.

The smallest useful width creates too many global rounds. Width 128K is the
best measured compromise, but still loses to GAPBS. Node-level control does not
shorten the solve. On Anvil, early idle-round time grows roughly linearly with
PEs while its runtime has `SPANTREE=0`; the matched spanning-tree screen is
prepared and remains the only open road-runtime test.

### 7. Frontier reproduces mesh and exposes launch bimodality

Every input regenerates byte-identically on Frontier. The mesh result
reproduces, but RMAT and uniform launches randomly enter a slow mode whose
speedup relative to the fast mode is about 0.84× (rmat27, 8 nodes). A whole launch moves together; work counts remain the same, reductions
stay short and remote updates trickle through the tail. Frozen R0 shows the
same behavior, so it is not caused by the recent candidate policies.

Send caps, packet/rendezvous behavior, receive matching, LCI device count,
ASLR, huge pages, NUMA placement/prebinding, fabric congestion and adaptive
solver state have been ruled out. Jobs 5535017–5535803 cover those probes. The
remaining bounded hypothesis is Reconverse backend progress/polling.

### 8. The fixed mesh candidate strong-scales, and each mechanism is causal

On Frontier the unchanged candidate (nearest, batch 8, slice 8, 8 × 7 per
node, `+old-scheduler`) ran `mesh26-z` at 1, 2, 4, 8 and 16 nodes on the four
held-out sources, in two allocations (jobs 5536321/5536322). All 448 timed and
160 work-cost solves passed the audit; allocations agree within 1.5%.

| Nodes | PEs | Time (s) | Speedup over 1 node | Efficiency | Attempts/edge | Rounds | Speedup over GAPBS (median) |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 56 | 1.70–1.72 | 1.00× | 100% | 1.35 | 598–607 | 0.19–0.20× |
| 2 | 112 | 0.98–0.99 | 1.71–1.72× | 85–86% | 1.54 | 612–630 | 0.33–0.34× |
| 4 | 224 | 0.56–0.58 | 2.91–2.97× | 73–74% | 1.72–1.73 | 550–555 | 0.56–0.58× |
| 8 | 448 | 0.364 | 4.68–4.70× | 59% | 1.94–1.95 | 586 | 0.92× |
| 16 | 896 | 0.259–0.260 | 6.55–6.58× | 41% | 2.76–2.80 | 500 | 1.29× |

Time falls at every doubling on every source. The loss of efficiency is
extra work, not communication: attempts per edge double from one to sixteen
nodes, updates crossing nodes stay below 0.5% of attempts, and the work-cost
build's idle share rises from 5% to 20%. Queue operations stay about 48% of
work time at every scale. ACIC passes one-node GAPBS between 8 and 16 nodes.

The cumulative ablation at 16 nodes, same allocations and sources:

| Arm | Candidate's speedup over this arm (median) | Attempts/edge | Rounds |
|---|---:|---:|---:|
| Local queue, no batch, no slice | 3.21–3.33× | 18.1–18.2 | 287–288 |
| Nearest, unbatched | 1.83× | 4.00–4.06 | 254–255 |
| Nearest, batch 8 | 1.47–1.56× | 5.09–5.41 | 250–252 |
| Nearest, batch 8, slice 8 (candidate) | 1.00× | 2.76–2.80 | 500 |

Process-wide priority removes 78% of the local queue's work. Batching then
trades 25–35% more work for a 1.15–1.24× speedup over the unbatched arm. The slice halves the remaining
work while doubling the rounds, and is the step that crosses GAPBS. Every
step is faster than the one before on every source in both allocations. 18 of
20 recorded predictions were met in allocation A and 20 of 20 in B; the two
misses were a 2.5% control difference on one source and batch-8 work of 5.41
against a predicted ceiling of 5.4 attempts per edge.

### 9. Wasp and RIKEN references on Frontier

**Wasp** (SC25 artifact, `bin/wasp_sssp`, digest adapter
`benchmarks/wasp_driver.cpp`) matches the independent reference on all 58
sources of seven graphs (job 5536535), so it reads the native weights. Tuned
like GAPBS, a joint thread × Δ search on training sources then four held-out
sources (job 5536541), it selects 56 threads with GAPBS's Δ (4096 on mesh,
32768 on road). Both choices sit at the 56-thread boundary.

| Graph | Wasp held-out | GAPBS held-out | Wasp's speedup over GAPBS | ACIC's speedup over Wasp |
|---|---:|---:|---:|---:|
| `mesh26-z` | 0.262–0.293 s | 0.316–0.352 s | 1.16–1.30× | 0.96–1.24× at 16 nodes (median 1.08×) |
| `road-usa-z` | 0.067–0.077 s | 0.116–0.142 s | 1.66–1.83× | 0.39–0.50× at 16 nodes; 0.35–0.41× at 8 nodes, training sources |

The mesh speedups pair each held-out source with the 16-node cells of jobs
5536321/5536322; the 8-node road speedups pair the training sources of jobs
5534016/5534017 with Wasp's training runs of its selected setting. The Wasp and
ACIC cells come from different jobs. Five Wasp launches at mesh Δ 4 hit the
180 s launch limit (the other four at that Δ took 52–68 s, against GAPBS's
4–7 s); they are recorded and do not affect the selection.

**RIKEN** at 16 nodes (jobs 5536474/5536475, independent searches) selects
8 ranks/node with Δ 16 (rmat25/26/27) or 64 (orkut, uniform25) in both
allocations; held-out medians agree within 1%: orkut 0.031 s, rmat25 0.063 s,
uniform25 0.187–0.188 s, rmat26 0.123 s, rmat27 0.231 s. On `mesh26-z` (job
5536476, one allocation) it selects Δ 1024 and takes 19–55 s per solve: ACIC's
16-node speedup over RIKEN there is 87–210× (median about 197×). Road is outside RIKEN's exact-distance range. The
only failures are the known Δ-equal-to-denominator aborts.

**ACIC on the scale-free graphs at 16 nodes** chose 4 × 14 for rmat25,
uniform25, rmat26 and rmat27 and 8 × 7 for orkut on training sources (job
5536460). The launch bimodality nearly disappears at 16 nodes: rmat25, rmat27,
uniform25 and orkut ran every launch in one mode, and rmat26 had 1–2 slow
launches of 8 at negligible cost.

Held-out confirmation, 16 nodes, four held-out sources, 8 launches per arm, two
allocations (jobs 5538389–5538392; 720 audited solves), paired with RIKEN's
allocation of the same letter:

| Graph | Layout | ACIC fast-mode median | RIKEN | ACIC's speedup over RIKEN (by source) | Slow launches |
|---|---|---:|---:|---:|---:|
| orkut | 8 × 7 | 0.066 s | 0.031 s | 0.45–0.48× | 1/16, 0/16 |
| `rmat25` | 4 × 14 | 0.208–0.209 s | 0.063–0.064 s | 0.29–0.34× | 0 |
| `uniform25` | 4 × 14 | 0.230 s | 0.187–0.188 s | 0.77–0.85× | 0 |
| `rmat26` | 4 × 14 | 0.366–0.369 s | 0.123 s | 0.31–0.35× | 0 |
| `rmat27` | 4 × 14 | 0.743–0.751 s | 0.231 s | 0.28–0.33× | 0 |

ACIC is slower than RIKEN on every source of every scale-free graph: a speedup
of 0.77–0.85× on `uniform25` and 0.28–0.48× elsewhere. Every recorded
prediction (written as time ratios before the reporting rule changed) was met.
With almost no slow launches, fast-mode and plain medians agree within 0.1%.
The repeated-control prediction (within 3%) held for orkut but missed on all
four 4 × 14 graphs in both allocations: at 4 × 14, repeated launches on the
same source spread about ±6% in both directions (control/candidate
0.95–1.06), a continuous spread rather than two modes. That floor is too wide
for a few-percent regression decision at this layout, but not for these
comparisons.

**Second mesh size** (`mesh24-z`, 16.8M vertices; jobs 5538412/5538413 with
references 5538410 GAPBS and 5538411 Wasp, both 56 threads, Δ 4096): the fixed
candidate falls at every doubling, from 0.41 s at one node to 0.085 s at 16
(4.8×, against 6.6× on `mesh26-z`), while attempts per edge rise from 1.30 to
3.3. The best cell, 16 nodes, has a speedup of 0.85–1.02× over GAPBS (median
0.92×) and 0.72–1.00× over Wasp (median 0.89×), so the smaller mesh does not
cross on the median. This was the recorded prediction: rounds follow the
diameter, which halves, while the shared-memory work follows size, which
quarters. The 16-node repeat was within 2% except one source in allocation B
(3.5%). The mesh win therefore needs enough work per round; the paper's mesh
claim is `mesh26-z` at 896 PEs, not the mesh family.

**Road on held-out sources** (jobs 5538405/5538406): width 131072 at 16 nodes
takes 0.140–0.197 s by source, a 1.14–1.16× speedup over 8 nodes, with repeats
within 2%. At 16 nodes the speedup is 0.72–0.84× over GAPBS and 0.39–0.50× over
Wasp. Both comparison predictions (recorded as time ratios, GAPBS 1.3–1.8 and
Wasp 2.2–3.2) missed on the favorable side; the node-count, width and control
predictions were met. Without the width, the plain candidate does 10.8–11.0
attempts per edge, and width 128K has a 1.45–2.15× speedup over it.

### 10. Frontier gates: RMAT regression NO-GO by the recorded rule; spanning tree kept, small effect

**RMAT regression gate** (jobs 5538463/5538464, 16 nodes, 8 × 7, four held-out
sources, 16 launches per arm; 2,040 audited solves). The recorded pass rule is
`onenode_accept.py --nodes 16 --variant n16_s8 --one-node-variant n1_s8
--regression-reps 16` over allocations (5536321, GAPBS 5529591, 5536321,
5538463) and (5536322, GAPBS 5538465, 5536322, 5538464). Result: **NO-GO.**

A graph passes if the candidate's worst per-source speedup over frozen R0 is
no lower than the noise bound set by the repeated R0 (1 / the largest
control-versus-frozen difference).

| Graph | Candidate's worst per-source speedup over frozen R0 (A / B) | Noise bound (A / B) | Verdict |
|---|---:|---:|---|
| `mesh26-z` C6 | speedup over GAPBS 1.29× / 1.28× median, worst 1.25× / 1.27× | — | pass, both |
| `rmat25` | 0.972 / 0.982 | 0.942 / 0.944 | pass, both |
| orkut | 0.990 / 0.975 | 0.978 / 0.972 | pass, both |
| `uniform25` | 0.945 / 0.999 | 0.919 / 0.948 | pass, both |
| `rmat26` | 0.949 / 0.965 | 0.967 / 0.982 | **fail, both** |
| `rmat27` | 0.975 / 0.955 | 0.976 / 0.976 | **fail, both** |

What the failures are made of:

- **`rmat26`**: each allocation fails on one source, and a different one each
  time (speedup 0.950 on source 1 in A, 0.965 on source 4 in B); the other
  sources are 0.99–1.03×. No launch was slow. On fast-mode medians the
  candidate's speedup over frozen is 0.974× in A and 1.022× in B.
- **`rmat27`**: the slow launch mode returned at 16 nodes on this graph, and
  more often for the candidate (7/16 and 4/16) than for frozen (4/16, 0/16) or
  control (2/16, 0/16); pooled, 11/32 against 6/64 (Fisher p = 0.004). In fast
  mode the candidate's speedup over frozen is 1.008× and 1.006×, so its fast
  solves did not regress. The opposite imbalance appeared on `uniform25` in B (candidate 0/32
  against R0 11/64 pooled over allocations, p = 0.014), so a mode-rate
  difference is not yet shown to be a property of the candidate.
- Predictions: every control-versus-frozen difference was under 10% (met); every graph passes (missed
  on `rmat26`, `rmat27`); at most 2 slow launches of 48 per graph (missed on
  `rmat27` in A, 13/48, and `uniform25` in B, 11/48).

The failures are speedups of 0.95–0.975× on single sources, at the resolution limit the plan
stated for this gate. The recorded rule decides this run; a mode-aware rule
(fast-mode ratio within the floor and no higher slow rate) or more allocations
would have to be recorded before a new run, not applied to this one.

**Spanning-tree screen** (jobs 5538468/5538469, road, 8 nodes, training
sources; tree = `acic_slice` on the campaign runtime, flat = `acic_flat` on the
same Reconverse/LCI built with `SPANTREE=OFF`). The tree lowers round cost on
every arm in both allocations, but by 0.002–0.027 ms, not the predicted
≥ 0.05 ms. The tree's speedup over flat is 1.09–1.11× on `w32k` (predicted at
least 20% faster), 1.005–1.04× on `w128k` (at least 10%) and 0.99–1.01× on
`4x14_cap7` (at least 10%). Work stays within 10%,
repeats within 2%, and `w64k` within 10% of `w128k`. By the plan's rule
`SPANTREE=ON` stays the campaign setting, and the screen stops here: the
broadcast tree is not what makes road's rounds cost 0.16–0.31 ms. The
remaining per-round cost is in the reduction or Main's per-round work.

### 11. RMAT profile: the waste is updates to hubs that are already final

A PC-sampling profile, a Projections trace and RIKEN's relaxation counter on
`rmat26` at 16 Frontier nodes, 4 × 14 (jobs 5538732, 5538734, 5538765), give
a work-versus-cost split. RIKEN sends 1.22B relaxations in 0.125 s. ACIC
creates 1.81B updates plus 0.19B lazy tokens in 0.357 s: about 1.5× the work
at about 1.9× the cost per relaxation. 89% of ACIC's arrivals (1.61B) find
their target already final, and 1.33B of those land on vertices of degree
≥ 128. The samples have no single hotspot. Two exact micro-fixes aimed at the
largest entries (a lazy-range boundary table and a vector hold FIFO) are
within the 0.94–1.11× control spread at 16 nodes (jobs 5538752/5538753).

Hub-distance hints target the waste directly. Vertices of degree ≥ 256
publish their current distance once per controller round to a per-process
table, and senders drop updates no better than it. This is exact because
distances only fall; it needs no settled test. With the auto gate (the
lazy-heavy regime), at 16 nodes over two allocations (jobs 5538953/5538954),
the speedup over frozen is 1.25–1.26× on `rmat25`, 1.30–1.34× on `rmat26`,
1.23–1.30× on `rmat27` and 1.01–1.07× on orkut; `uniform25` is unchanged.
`rmat26` updates fall from 1.81B to 0.68B, under RIKEN's count, yet ACIC stays
about 2.2× slower than RIKEN. The remaining cost is the sender's edge scan
and probe, heap and tokens, runtime polling and an idle tail, not update
volume. Adding the initial-exec TLS runtime gives a further 1.00–1.07×.
These are training-source results; held-out confirmation is pending.

The same instruments on `road-usa-z` at 16 nodes (job 5538868) show PEs idle
61.5% of the solve, with about 800 controller rounds of about 0.23 ms each.
Road at this size is bound by round latency, not work, which is why larger
road inputs are the next test (`road-eu`, `road-na`; `scripts/frontier/prepare_osm.sbatch`).

### 12. Delta road attribution: one-node work and queued heap callbacks

The September 24 rebuild uses latest Charm++ `f6c74074f` and Reconverse
`0c97c4d`, production/shared-memory/spanning-tree settings and `+old-scheduler`.
Unmodified upstream SSSP `7a4da59` and the output-only sensitivity build passed
448 serial-verified solves across one-node job 22354859 and two-node job
22354910; all 112 launches confirmed the old scheduler. A two-PE profiler
smoke passed four serial checks and per-source timer-count checks. The
workspace binary/config now use this runtime; previous copies are preserved.

One-node attribution job **22354907** completed all 29 road digests, work
audits and 12 empty-cycle launches (16 × 7 road layout, two training sources).
Production medians are 0.437/0.465 s, about 670/841 rounds and 1.54/1.59
attempts per edge. Quiet-round speedup is 1.009/0.980×, versus repeated-control
speedup 0.992/0.966×: **no resolved logging benefit**. The phase build puts
Main at 18.5–20.6 us/round including 8.8–9.2 us of logging and 9.0–10.5 us
of broadcast-call work. Worker threshold handling averages 5.8–7.8 us.
Production round cost is 0.55–0.64 ms, while the 267-long empty cycle at the
same layout averages 0.080–0.090 ms across launches. The work-cost build
spends 73–77% of PE time in solver work, with 13–17% idle. One-node road is
therefore predominantly work-bound, not explained by an unloaded collective
floor or controller arithmetic. These are attribution measurements from one
allocation, not a new accepted performance result.

The single-source trace is within 5% of production time and locates long
queue waits: Main's reduction callback has p90 send-to-execute latency
0.23 ms; heap callbacks have p50 0.10 ms and p90 0.35 ms. Four inspected
PEs have **62–75 pending heap callbacks at peak**, with 14–17 already waiting
at median heap execution. Code inspection identifies a cause: every ordinary
controller round queues another heap callback even when the sliced shared
heap already has one pending. Each callback can reschedule its own chain.

A compile-time prototype, `ACIC_COALESCE_HEAP`, uses the existing pending
flag to suppress duplicate shared-heap wakeups. The non-shared path and all
threshold/relaxation rules remain unchanged. This is **experimental**: its
four-source local serial/work smoke passed; distributed gate **22355072**
and matched road/mesh timing are next. The prediction and controls are in
`benchmarks/delta-heap-coalesce-{road,mesh}-variants.json`.

Eight-node attribution **22354948** is queued after successful correctness
and one-node dependencies; the scheduler currently forecasts an overnight
start. It is needed to explain the previously observed distributed road
round limit. Raw one-node evidence is archived in
`design/onenode-data/delta-road-rounds-22354907.json`; logs are under
`/u/rao1/.tmp/road-rounds-20260924/logs/`.

## Evidence and provenance

| Evidence | Machine-readable record / configuration |
|---|---|
| Anvil mesh C6 | `design/onenode-data/c6-mesh-8n-anvil-20866513.json` and matching 20866514 record; `benchmarks/c6-mesh-8n-variants.json` |
| Frontier mesh C6 | Frontier summaries under `design/onenode-data/` with job IDs 5534022/5534023; `benchmarks/frontier-c6-mesh-variants.json` |
| Anvil road ordering/rounds | `design/onenode-data/road-order-8n-anvil-20868022.json`, road-round records 20876828–30; `benchmarks/road-order-8n-variants.json`, `benchmarks/road-rounds-8n-variants.json` |
| Frontier road | `design/onenode-data/frontier-road-rounds-5534016.json` and 5534017; `benchmarks/frontier-road-rounds-8n-variants.json` |
| Runtime scheduler | layout and old-scheduler JSON summaries under `design/onenode-data/`; `benchmarks/oldsched-variants.json` |
| Frontier mesh strong scaling and ablation | `design/onenode-data/frontier-mesh-scaling-5536321.json`, `-5536322.json` and `frontier-mesh-scaling-report.json` (`benchmarks/mesh_scaling_report.py`); `benchmarks/frontier-mesh-scaling-variants.json`, predictions recorded in commit `026e502` |
| Frontier Wasp and RIKEN references | `design/onenode-data/frontier-wasp-1n-5536541.json`, `frontier-riken-16n-5536474.json`, `-5536475.json`, `-5536476.json`; smoke job 5536535 |
| Frontier scale-free layout selection | `design/onenode-data/frontier-scalefree-layout-5536460.json` and `frontier-scalefree-layout-modes-5536460.json`; `benchmarks/frontier-scalefree-layout-variants.json` |
| Frontier scale-free held-out vs RIKEN | `design/onenode-data/frontier-scalefree-heldout-{5538389,5538390,5538391,5538392}.json`, matching `-modes-` files, `frontier-scalefree-vs-riken-16n.json`; `benchmarks/frontier-scalefree-heldout-{4x14,8x7}-variants.json` |
| Frontier road held-out | `design/onenode-data/frontier-road-heldout-5538405.json`, `-5538406.json`; `benchmarks/frontier-road-heldout-variants.json`, predictions in commit `8749a7d` |
| Frontier mesh24-z | `design/onenode-data/frontier-mesh24-scaling-5538412.json`, `-5538413.json`, `frontier-gap-mesh24-5538410.json`, `frontier-wasp-mesh24-5538411.json`; `benchmarks/frontier-mesh24-scaling-variants.json` |
| Frontier RMAT gate | `design/onenode-data/frontier-rmat-gate-16n-5538463.json`, `-5538464.json`, `-modes-` files, `frontier-accept-16n.json`; `benchmarks/frontier-rmat-gate-16n-variants.json`; GAPBS second allocation 5538465 |
| Frontier spanning-tree screen | `design/onenode-data/frontier-road-spantree-5538468.json`, `-5538469.json`; `benchmarks/frontier-road-spantree-8n-variants.json` |
| Frontier RMAT behavior | `design/onenode-data/frontier-rmat-regression-5534333.json` and 5534334 plus probe configurations named `benchmarks/frontier-*-probe-variants.json` |

Anvil raw logs are rooted at `/anvil/scratch/x-rrao/acic/`; Frontier raw logs
at `/lustre/orion/csc710/scratch/rrao/acic/campaign/logs/`. Build identities
and paths are consolidated in [configurations.md](configurations.md).

## Claims supported now

1. At 896 CPU PEs, sliced asynchronous ACIC beats tuned one-node GAPBS on the
   measured `mesh26-z` class on two machines and held-out sources. Against
   tuned Wasp on Frontier its median speedup is 1.08×, but not every source is
   above 1×.
2. Process-wide priority plus batched removal reduces redundant sparse-graph
   work, and heap slicing converts that reduction to a distributed mesh gain.
   At 16 Frontier nodes each step of the cumulative ablation is faster than the
   previous one; the candidate's speedup over the local-queue arm is 3.2–3.3×.
3. On road, a representable global ordering window approaches minimal edge
   work, after which controller round cost is the dominant measured limit.
4. Runtime scheduling materially changes asynchronous SSSP work; registered
   scheduling causes a reproduced regression on road (a speedup of about
   0.69–0.77× relative to `+old-scheduler`).
5. The fixed mesh candidate strong-scales from 1 to 16 Frontier nodes at
   6.6× (41% efficiency), with time falling at every doubling; the efficiency
   loss is measured redundant work, not inter-node traffic.

## Claims not supported

- ACIC is generally faster than GAPBS, RIKEN or GPU SSSP systems.
- Live feedback or algorithm/communication co-design causes the current win.
- High-diameter graphs generally favor ACIC; the real road graph still loses.
- ACIC has demonstrated a fixed-candidate strong-scaling curve beyond 16
  Frontier nodes, or any strong-scaling curve on Anvil.
- The current candidate is regression-free on RMAT. The Frontier frozen-binary
  gate returned NO-GO by its recorded rule (`rmat26`, `rmat27`, 2.5–5% on
  single sources, §10).
- Frontier launch bimodality is caused by LCI, the network or ACIC. Evidence
  localizes it only to remote tail progress.
