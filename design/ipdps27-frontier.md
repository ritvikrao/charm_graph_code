# Frontier: second-machine port, one-node layouts, road rounds, C6 on mesh, and RMAT launch bimodality

2026-09-23. The campaign moved to OLCF Frontier while Anvil queue waits grew.
This records the port, five completed experiments, and seven probes of a
timing defect that blocks the RMAT regression gate (§7). Anvil remains the
primary machine; nothing here replaces an Anvil cell, and no absolute time is
compared across machines except where the text says so explicitly.

**Headline:** the [mesh C6 result](ipdps27-c6-mesh.md) **reproduces on a second
machine**. At 16 Frontier nodes (896 PEs, exactly Anvil's 8-node PE count),
ACIC is 0.70–0.79 of tuned one-node GAPBS on all four held-out sources in two
allocations, against Anvil's 0.71–0.83. On road, a fixed bucket width of
131072 is the fastest arm measured on either machine, and it answers part of
Anvil's [queued rounds screen](ipdps27-road-order.md#next-screen-fewer-rounds-or-cheaper-rounds-recorded-before-submission)
before that screen has run.

## 1. Port

| Item | Pin |
|---|---|
| ACIC source | `5ab6d5b`, binaries `acic_slice` (sha256 `460c7b15…`), `acic_slice_diag`, `acic_slice_cost_diag` |
| htram | `7db9c0a` (the campaign pin) |
| Charm++ | charmplusplus/charm `reconverse-specific-build` `f6c74074f` (the Delta pin), `--with-production --enable-tracing --enable-shmem` |
| Reconverse | autofetched `123313027faa`, the same commit Anvil runs, so `+old-scheduler` applies identically and is used in every arm |
| LCI | autofetched `ca88ce2c` (the Delta pin) |
| Toolchain | PrgEnv-gnu 8.6.0, gcc-native 13.2, cray-mpich 8.1.31, libfabric 2.3.1, cmake 3.31.11 |
| Baselines | GAPBS `2972aeb`, RIKEN `552f156`, Galois `b67f942`, same adapters and patches as Delta/Anvil |
| Campaign root | `/lustre/orion/csc710/scratch/rrao/acic` |

**Inputs are bit-identical to Delta's.** Every step-7.5 graph regenerated here
matches the recorded sha256, metadata and reference digests in
[step75-data/graphs.json](step75-data/graphs.json), including four Galois `.gr`
copies. The Morton relabelings reproduce [E2's](ipdps27-sprint.md#e2-why-high-diameter-solves-do-not-speed-up-from-2-to-8-nodes)
edge-cut table exactly (mesh24 0.34/0.49/1.0%, road-usa 0.20/0.29/0.63% at
112/224/896 PEs), and each `-z` graph's distance sums equal its native graph's.

**Layouts differ, and the harness now knows it.** A Frontier node exposes 56
cores: eight 8-core L3 regions with the first core of each (PUs 0, 8, …, 56)
reserved by Slurm, SMT off. `benchmarks/machine.py` holds the layout for every
machine; `launch_acic.sh`, `run.py`, `onenode_ab.py`, `onenode_gap_tune.py`,
`work_cost_gap.py` and `check_priority_runs.py` take `-c`, `+ppn`, `+pemap`,
thread counts and audit expectations from it. Delta and Anvil behaviour is
unchanged.

- Delta/Anvil 8 × 15 (one process per NUMA domain, one core left free for the
  OS) becomes **8 × 7** here: the reserved core already is that free core.
- Anvil's 16 × 7, which carries its mesh and road results, **cannot be run**:
  112 workers do not fit 56 cores. The Frontier analogues are 8 × 7 (most
  processes, fewest workers each) and 4 × 14.
- Ranks per node must be 1, 2, 4, 8 or 56; `scripts/frontier/check_affinity.sbatch`
  verifies on a compute node that every PE binds to a usable core.
- Frontier keeps Cray PMI. Anvil's `ACIC_SRUN_MPI=pmi2` must **not** be copied:
  under `--mpi=pmi2` every LCI rank gets world size 1, so each process solves
  the whole graph alone and still prints the right answer. The gate's
  process/PE banner assertion catches it.

**Correctness gate** (jobs 5529574 at one node, 5529575 at two): 224 serial-verified
solves each, over batch sizes 1/8/32/64, local and nearest queues, tiling on and
off, sparse and dense inputs, repeated and isolated sources and range extension,
with `--heap-slice 8 --process-drain-cap 3 +old-scheduler` active, plus queue
conservation and edge accounting from the work-cost build, and all 90 inter-node
work checks at two nodes.

## 2. GAPBS reference (job 5529591)

Delta's frozen settings (mesh 128 threads, road 64) exceed Frontier's 56 cores,
so the reference is re-selected here, on training sources only, with the same
joint thread × delta search as Anvil.

| Graph | Threads | Δ | Training sources | Held-out (4 sources) |
|---|---|---|---|---|
| mesh26-z | 56 | 4096 | 0.346 / 0.311 | 0.316–0.352 |
| road-usa-z | 56 | 32768 | 0.138 / 0.114 | 0.116–0.142 |

Both winners sit at the **thread boundary**: 56 is the whole node, so the
search cannot show an interior optimum in threads, and the reference is stated
with that limit, as Anvil states its grid edges. The deltas are interior.

The bars are close to Anvil's (mesh 0.335–0.376, road 0.117–0.142), so the
target is about the same on both machines even though a Frontier node has
fewer than half the cores.

## 3. One node: layout and slice (jobs 5529592/5529593)

Two allocations, mesh26-z and road-usa-z, two training sources, one warmup and
three repetitions, arms rotated, every solve digest-checked. All arms run 56
workers per node, so this varies workers per process at a fixed node total —
the axis the [attribution](ipdps27-r1-attribution-results.md) identified.
Medians of source medians, A / B, with attempts per edge.

| Arm | mesh26-z | road-usa-z |
|---|---|---|
| 1 × 56 | 2.605 / 2.610 (1.01) | 0.881 / 0.852 (1.24) |
| 2 × 28 | 1.979 / 1.992 (1.16) | 0.815 / 0.821 (1.84) |
| 4 × 14 | 1.842 / 1.836 (1.28) | 0.706 / 0.726 (1.93) |
| **8 × 7** | **1.529 / 1.547** (1.42) | 0.554 / 0.575 (2.01) |
| 8 × 7 slice 8 | 1.609 / 1.654 (1.30) | **0.500 / 0.512** (1.54) |
| 4 × 14 slice 8 | 1.893 / 1.894 | 0.624 / 0.624 |
| 2 × 28 slice 8 | 2.083 / 2.092 | 0.718 / 0.724 |

The repeated control agrees with its arm within 0.02–0.4%, so the floor is
under 1% and every difference above clears it.

- **More processes win on both graphs**, monotonically: 1 × 56 → 8 × 7 is 1.7×
  on mesh and 1.6× on road. Same direction as Anvil's preference for 16 × 7.
- **At one node the slice splits by graph**, which Anvil had not measured
  ([round cadence](ipdps27-round-cadence.md) lists one-node slice behaviour as
  unmeasured): road gains 9–11%, mesh **loses** 5–7%. §5 resolves this: the
  slice pays only once cross-process rework dominates, which at one node it
  does not.
- **Least work is not least time.** 1 × 56 does 1.01 attempts per edge on mesh,
  near the Dijkstra floor, and is the slowest arm by 1.7×.
- **The one-node gap** to tuned GAPBS on the same sources is 4.7–4.9× on mesh
  and 3.9–4.2× on road, against Anvil's 2.6× and 3.5×, consistent with 56 cores
  against 128.

## 4. Road at 8 nodes: fewer rounds, cheaper rounds (jobs 5534016/5534017)

Recorded before submission and run in two allocations, road-usa-z, training
sources, one warmup and three repetitions ([config](../benchmarks/frontier-road-rounds-8n-variants.json)).
This tests the [road-order](ipdps27-road-order.md) finding — the solve is
round-bound once the histogram is in range — on a machine with half Anvil's
PEs per node. Anvil's best road arm, 8 × 15 cap 7, has no Frontier twin (15
workers per process), so 4 × 14 cap 7 stands in.

Medians per source, A / B, with attempts per edge and round counts
(`Number of reductions`, median over launches).

| Arm | 5620086 | 1294456 | Work | Rounds |
|---|---|---|---|---|
| 8 × 7 slice 8 (baseline) | 0.200 / 0.197 | 0.259 / 0.253 | 4.8–4.9 | 335 / 410 |
| + width 32768 | 0.180 / 0.180 | 0.237 / 0.237 | **1.48** | 963 / 1494 |
| + width 32768, slack on | 0.200 / 0.206 | 0.255 / 0.250 | 4.58 | 297 / 435 |
| **+ width 131072** | **0.167 / 0.165** | **0.208 / 0.208** | 2.25–2.29 | 549 / 812 |
| + width 524288 | 0.189 / 0.189 | 0.249 / 0.251 | 4.51 | 334 / 444 |
| 4 × 14 cap 7 | 0.212 / 0.212 | 0.258 / 0.258 | 2.45 | 739 / 901 |
| 8 × 4, width 131072 | 0.204 / 0.202 | 0.250 / 0.249 | 2.16 | 569 / 829 |

Controls agree with their arm within 0.1–0.9%; the two allocations agree within
about 1%.

**Against Anvil's pre-registered predictions for `w128k`:**

| Prediction | Outcome here |
|---|---|
| At most 3 attempts per edge | **met** (2.25–2.29) |
| Faster than the cap arm on both sources, both allocations | **met** (20–21% against 4 × 14 cap 7) |
| At most 0.45× the rounds of `w32k` | **missed** (0.54–0.57) |
| At least 25% faster than `w32k` | **missed** (8–12%) |
| `w512k` turns the curve: ≥1.5× the work, no faster | **met** (2.0× the work, 12–20% slower) |

- **Width 131072 is the fastest road arm measured on either machine**: 0.165 s
  on 5620086 against Anvil's best of 0.174–0.196 s. Cross-machine absolute
  times are not poolable; the point is that the width lever works, and Anvil
  should expect its `w128k` arm to beat `8x15_cap7`.
- **The ordered regime's rounds are much cheaper here, at the same round
  count.** `w32k` runs 963/1494 rounds against Anvil's 889–911/1436–1444 — as
  expected, since rounds follow distance range ÷ width, not the machine — while
  time is 0.180/0.237 s against 0.268/0.425 s. Time divided by rounds is
  0.16–0.19 ms at 448 PEs against about 0.30 ms at 896 PEs. That is a derived
  quantity, not a measured reduction latency, and the machines differ in CPU
  and fabric as well as PE count; but it is consistent with round cost growing
  with PEs, which is what Anvil's `--control node` screen targets.
- **Fewer PEs per node does not pay within a machine.** The 8 × 4 arm (256 PEs)
  is *slower* than 8 × 7 (448 PEs) despite fewer, cheaper rounds: the lost
  compute outweighs the saving. So the cross-machine round saving is not a
  within-machine knob.
- **Live slack loses to a fixed width here.** On top of `w32k` it returns work
  from 1.48 to 4.58 attempts per edge and is 12–20% slower than fixed `w128k`.
  On Anvil slack beat its baseline. Under C3c this is a negative cell: on this
  machine the live controller does not beat a well-chosen constant.
- **Road against GAPBS:** 0.165 / 0.208 s against 0.114 / 0.138 s, so
  **1.45–1.51× behind**, the same ratio Anvil reports.

## 5. C6 on mesh at 8 and 16 nodes (jobs 5534022/5534023)

Two 16-node allocations, each running both node counts so the sources, binary
and allocation are shared ([config](../benchmarks/frontier-c6-mesh-variants.json)).
Four **held-out** sources, one warmup and three repetitions, every solve
digest-checked. The reference is §2's tuned one-node GAPBS on the same sources.
16 Frontier nodes is 896 PEs, exactly the PE count of Anvil's passing 8-node
16 × 7 configuration.

Medians per source, A / B, with the ratio to GAPBS for the candidate arm.

| Source | GAPBS | 8 nodes, slice 8 | **16 nodes, slice 8** | 16n control | 16 nodes, no slice |
|---|---|---|---|---|---|
| 21824001 | 0.341 | 0.377 / 0.370 | **0.270 / 0.271** (0.79) | 0.271 / 0.272 | 0.398 / 0.500 |
| 35305828 | 0.322 | 0.347 / 0.350 | **0.255 / 0.251** (0.78) | 0.250 / 0.250 | 0.380 / 0.394 |
| 41458868 | 0.316 | 0.337 / 0.331 | **0.222 / 0.221** (0.70) | 0.222 / 0.222 | 0.332 / 0.345 |
| 44514593 | 0.352 | 0.391 / 0.384 | **0.276 / 0.278** (0.78) | 0.278 / 0.275 | 0.480 / 0.412 |

- **The C6 rule passes at 16 nodes**: at or below the reference on every
  held-out source in both allocations, at **0.70–0.79** of GAPBS's time
  (1.27–1.43× faster). Anvil's passing cells are 0.71–0.83 (1.21–1.41×).
- **8 nodes fails**, at 1.05–1.11×: parity, exactly what Anvil's unsliced
  8 × 7 arm gives. A Frontier node has fewer than half an Anvil node's cores,
  so equal node counts are not equal machines; **equal PE counts agree**.
- **The slice carries the win, again.** Without it, 16 nodes is 0.33–0.50 s —
  worse than its own 8-node sliced arm — and work is 5.13 against 2.80 attempts
  per edge. With §3's one-node result, the slice hurts when rework is low and
  pays when cross-process rework dominates.
- Controls agree with their arms within 0.4%; allocations agree within 1%.

**Claim supported (mesh class only):** on two independent machines, with
training-selected settings and held-out sources in two allocations each,
distributed ACIC with an 8-entry drain slice solves mesh26-z SSSP 1.2–1.4×
faster than tuned one-node GAPBS Δ-stepping.

## 6. RMAT suite: the new settings are inert, but this is not the gate (jobs 5534333/5534334)

The five regression graphs at 8 nodes (8 × 7), four held-out sources, warmup
plus three repetitions, two allocations. Three arms of `acic_slice`: the mesh
candidate flags (`--process-share auto`, queue `nearest`, batch 8, slice 8),
the same binary with `--process-share off`, and the candidate repeated.

**What it shows.** All 480 solves match the independent references. All 120
launches print `Process sharing: off`, queue, batch and slice `(inactive)`,
and no reader tiles: with 31–76 arcs per vertex, `auto` resolves off, as
the source says it should. Candidate and sharing-off run the same effective
configuration.

**What it does not show.** The [gate](ipdps27-onenode-gap.md) compares the
candidate with a **frozen pre-change binary**, with a repeat of that frozen
arm as the floor (`benchmarks/onenode_accept.py`). This suite used one
binary, so it cannot detect a regression in code shared by both paths since
the frozen build. Its arms also do not carry the tool's labels. **The
regression gate has still not run**, on either machine.

Median over sources (s), and the worst per-source candidate / sharing-off
ratio against the spread between the two identical candidate arms:

| Graph | Allocation | Candidate | Sharing off | Repeat | Worst cand/off | Same-config spread |
|---|---|---:|---:|---:|---:|---:|
| rmat25 | A / B | 0.304 / 0.308 | 0.309 / 0.326 | 0.346 / 0.319 | 1.000 / 1.001 | 1.162 / 1.072 |
| orkut | A / B | 0.100 / 0.100 | 0.100 / 0.101 | 0.099 / 0.102 | 1.013 / 1.006 | 1.022 / 1.038 |
| uniform25 | A / B | 0.383 / 0.380 | 0.397 / 0.360 | 0.390 / 0.299 | 0.992 / 1.108 | 1.063 / 1.378 |
| rmat26 | A / B | 0.685 / 0.589 | 0.664 / 0.648 | 0.678 / 0.602 | **1.085** / 0.952 | 1.038 / 1.117 |
| rmat27 | A / B | 1.409 / 1.227 | 1.204 / 1.239 | 1.237 / 1.436 | **1.207** / 1.022 | 1.145 / 1.196 |

Read under the tool's rule, rmat26 and rmat27 in allocation A would fail: the
candidate is slower than sharing-off by more than the same-config spread.
Nothing in the configuration differs, so these would be false failures, and
they come from how the time varies:

- **Launches are bimodal, and a whole launch moves together.** On rmat27 every
  source in a launch runs about 1.2 s or about 1.43 s; uniform25's launches
  total about 1.1–1.3 s or about 1.55–1.7 s. The mode is not tied to the arm
  or its position in the interleave. Delta saw the same on rmat25 at one node
  ([step 8](step8-scaling.md), [step 7.6](step76-external.md)).
- **Slow launches do the same work.** rmat27's wasted updates stay within about 3%
  (3.67–3.78 × 10⁹) across all launches; the slowest and fastest candidate
  launches in allocation B differ by 0.1% in work and 18% in time. The cause is
  launch state, not the algorithm, and it is not yet identified.
- With three launches per arm, a source median is decided by how many of them
  landed slow. The allocation A candidate drew three slow launches on rmat27,
  and allocation B's repeat drew three. One repeated arm then gives a floor
  of about 1.0 or about 1.2, by chance.

Orkut is not bimodal, and its three arms agree within 1.3% on every source.

**Consequence for the real gate.** Run as it stands, with three launches per
arm, the frozen/control rule would also pass or fail rmat26, rmat27 and
uniform25 by chance. Before it runs, either (a) the bimodality is explained
and removed, or (b) the gate draws more launches per arm, fixed in advance, so
a median is not decided by one or two draws.

**Author's decision (2026-09-23):** freeze R0 as the gate's frozen binary, and
take (a): find and remove the cause before the gate runs. §7 records the
search so far.

## 7. RMAT launch bimodality: what it is and is not (jobs 5535017–5535803)

Seven 8-node probes, each rmat27 plus uniform25 (rmat25 in the one-node probe),
four held-out sources, warmup plus 6–8 launches per arm, arms interleaved in
one allocation ([data](onenode-data/frontier-bimodal-probes.json)). They are
diagnostic runs, not gate cells, and every solve still validates against the
references. **The cause is not found.** The probes pin down what the effect
is and rule out nine candidate causes.

**Frozen binary.** `acic_r0_control` (sha256 `3991df3d…`) is built on
Frontier from R0's solver source `598f13b` with the production flags, the
campaign Charm++ tree and htram `7db9c0a` (R0's own htram revision was never
recorded; `7db9c0a` is the campaign pin). It prints the same `Process
sharing`, `Live slack` and `Reader tiles` lines under the same auto rules, so
the gate audit applies unchanged.

### What the effect is

A slow rmat27 launch is one whose solves all take more than 1.32 s; fast
solves run about 1.2 s and slow ones about 1.4–1.5 s.

- **It is set per launch.** Over 188 rmat27 probe launches (752 solves),
  38.7% of solves are slow. If each solve drew independently, 84% of launches
  would mix fast and slow solves. 17 (9%) do, several of them borderline
  (one solve at 1.33–1.36 s). A launch picks a mode at start-up and keeps it
  through all four sources.
- **Launches are independent of each other.** Fast and slow launches
  interleave across arms and time with no runs beyond chance, so this is not
  a congested period of the fabric.
- **Only the tail differs.** Splitting each solve at 95% of updates created
  (`benchmarks/tail_split.py`), rmat27's bulk is 1.064 s fast against 1.063 s
  slow (medians over launches). The tail is 0.16 s against 0.37 s, over 218
  against 610 rounds. uniform25 is the same: bulk 0.23 s in both modes, tail
  0.05 s against 0.14 s.
- **In the slow tail, data between nodes trickles while reductions stay
  fast.** The round diagnostics (`--diag`) show slow tails made of many short,
  nearly empty rounds (90th percentile about 0.3 ms). Fast tails have fewer
  rounds, many of them 1–2.6 ms, each settling more work. A round ends quickly
  because its reduction completes quickly, but the TRAM buffers from other
  nodes that would give it work arrive late.
- **Same work.** Updates, wasted updates and edge attempts agree between modes
  within the source-to-source spread (§6).
- **It needs more than one node.** In the same allocation (5535429), one node
  runs rmat25 at 2.08–2.18 s and uniform25 at 2.17–2.30 s over 16 launches
  each, one mode (one uniform25 outlier at 2.79 s), while 8 nodes is bimodal on
  both graphs.
- **R0 has it too.** The six frozen R0 arms are slow as often as the
  candidate (2/6 in each of the four in 5535071 and 5535085, 6/8 and 4/8 in
  5535803), so it predates every change the gate is meant to judge.

### What it is not

Slow rmat27 launches, fully slow and mixed, out of launches per arm:

| Job | Arm | Changes | Slow | Mixed | Launches |
|---|---|---|---:|---:|---:|
| 5535071 | hybrid | candidate, defaults | 2 | 0 | 6 |
| | sends256 / sends1024 | `LCI_ATTR_NET_MAX_SENDS` and `FI_CXI_DEFAULT_TX_SIZE` 256 / 1024 | 2 / 3 | 0 / 0 | 6 / 6 |
| | r0_hybrid / r0_sends1024 | R0, defaults / 1024 sends | 2 / 2 | 0 / 0 | 6 / 6 |
| | hybrid_log / sends1024_log | CXI warnings logged per rank | 0 / 5 | 0 / 0 | 6 / 6 |
| 5535085 | hybrid | candidate, defaults | 3 | 0 | 6 |
| | pkt64k | `LCI_ATTR_PACKET_SIZE` 65536, 32768 packets | 1 | 0 | 6 |
| | pkt64k_sends1024 | both | 1 | 2 | 6 |
| | r0_hybrid / r0_pkt64k | R0, defaults / 64 KB packets | 2 / 2 | 0 / 0 | 6 / 6 |
| | pkt64k_log / pkt64k_sends1024_log | logged | 1 / 3 | 2 / 0 | 6 / 6 |
| 5535202 | base / base_mem | candidate / with RSS and huge-page sampling | 3 / 4 | 1 / 2 | 8 / 8 |
| | aslr_off | `setarch -R` | 2 | 0 | 8 |
| | thp_off | `prctl(PR_SET_THP_DISABLE)` (`benchmarks/nothp.c`) | 2 | 1 | 8 |
| 5535399 | base / base_numa | candidate / with per-node page sampling | 3 / 1 | 0 / 3 | 8 / 8 |
| | prebind / prebind_numa | `taskset` to the rank's cores before start-up | 6 / 2 | 0 / 2 | 8 / 8 |
| 5535803 | ndev4 | candidate, 4 LCI devices (the launcher default) | 0 | 1 | 8 |
| | ndev7 / ndev7_pkt64k | 7 devices, one per PE / plus 64 KB packets | 3 / 3 | 2 / 1 | 8 / 8 |
| | r0_ndev4 / r0_ndev7 | R0, 4 / 7 devices | 6 / 4 | 0 / 0 | 8 / 8 |

Probe 5535017 (receive matching `FI_CXI_RX_MATCH_MODE=software`, and one LCI
device per process) failed on logging after its warmup launches (defect 6
below). Each of those two arms got one launch, and both ran slow (1.43–1.52 s
per solve), so neither setting removes the slow mode. Their slow rates were
not measured. With 6–8 launches per arm and P(slow) near 0.4, arms from 0/8
to 6/8 are within chance of each other; no change moves the rate outside that
range.

- **Send-queue cap.** At the default 64 outstanding sends per device, CXI logs
  "TXC attr size saturated" throughout the solve. 256 or 1024 removes those
  warnings and leaves the slow rate and the fast-mode time unchanged.
- **Packet size.** TRAM buffers reach 6144 × 8 B + 96 B = 49,248 B, above
  LCI's 8,192-byte default packet, so each buffer goes by rendezvous RMA write
  and CXI logs tens of thousands of "Failed to emit dma command -11" per
  solve. 64 KB packets remove those and leave the rate unchanged.
- **Receive matching.** Software matching instead of hybrid: its one launch
  ran slow.
- **LCI devices per process.** The launcher has run 4 devices for 7 PEs on
  every Frontier result so far (`device = thread / ceil(7/4)`, so three devices
  serve two PEs and one serves one). The intended setting is one device per PE.
  1 device (one launch, slow), 4 and 7 devices all produce slow launches.
  `r0_ndev7` once hung in start-up on uniform25 (after `Degree CV`, with 224
  of 448 PEs having printed their read lines;
  `logs/AB-uniform25-8n-5535803/r0_ndev7-g0-r-1.launch.out`), which failed
  the probe's uniform25 half, so the launcher still defaults to 4 until that
  is understood.
- **Address layout.** `setarch -R`: no change.
- **Transparent huge pages.** The nodes run THP `never`, and the sampler sees
  0 GB of huge pages in every launch; disabling it per process changes
  nothing.
- **NUMA placement.** Without pre-binding, the runtime pins threads only after
  start-up allocation, and on most ranks 34–55% of pages sit on a remote NUMA
  node. `taskset` pre-binding fixes placement and not the bimodality.
- **Fabric congestion.** Excluded by the independence above.
- **Adaptive settings in the solver or htram** that could latch a mode: none
  carry state across sources.

The send cap, the rendezvous path and the NUMA placement are real
inefficiencies worth fixing on their own, but none is this cause.

### Working hypothesis and next probe

Something fixed at start-up decides, for the whole launch, how promptly data
between nodes is delivered once traffic is light. In the bulk each round has
local work, so delivery delay is hidden. In the tail each round waits on a
few remote buffers, and the delay becomes the whole cost. The untested
candidate is **progress**: Reconverse polls a device's completion queue only
from that device's own threads, when they return to the scheduler. Which
threads sit longest in the solver, and so which devices go unpolled, could be
set by start-up timing. A probe for it, proposed and not submitted: 2 and 4
nodes for where the effect starts, and `+backend_poll_thread` and
`+backend_poll_freq` arms, about 1.3 node-hours. If a dedicated poll thread
makes every launch fast, that is the cause and the gate's configuration.

## 8. Scope and what is still missing

- **Two graphs for performance.** Every performance claim here is mesh26-z and
  road-usa-z. On the five RMAT graphs, §6 shows the new mechanisms are inactive
  and answers are correct, but the frozen/control regression gate required
  before R3 acceptance has not run on either machine. On Frontier it is
  blocked on §7's launch bimodality, which R0 shows as well.
- **Every Frontier ACIC result ran 4 LCI devices for 7 PEs per process**, and
  LCI's default 8 KB packet, below the 49 KB TRAM buffers. Neither setting is
  the one the runtime intends (§7). Neither changed the bimodality; whether
  they change the §3–§5 times has not been measured.
- **Width 131072 is a road-specific constant chosen for a mechanism test.** It
  must come from the graph as read or from live round statistics before it can
  be a default, and it must then be checked on mesh and RMAT.
- **Layout is a per-graph-class tuning choice** on both machines, like GAPBS's
  thread count. 16 × 7 (Anvil) and 8 × 7/16 nodes (Frontier) are not the same
  configuration; the shared claim is about the mechanism and the PE count.
- Cross-machine absolute times are not pooled anywhere above. Each machine has
  its own GAPBS reference, its own tuning and its own allocation floors.
- Road remains 1.45–1.51× behind one-node GAPBS on both machines.

## 9. Cost and evidence

**13.75 node-hours** on Frontier for every job in this note, including the
input preparation, the affinity check, the baseline smoke test, two failed
gates and two failed audits. The four §2–§5 experiments are about 1.4
node-hours. The §6 RMAT suite is 1.43. The seven §7 probes are 9.77, two of
which failed (5535017, 5535803).

Audits, all passing, with per-source and per-repetition metrics and raw log
hashes:

- one node: [5529592](onenode-data/frontier-onenode-5529592.json), [5529593](onenode-data/frontier-onenode-5529593.json)
- road rounds: [5534016](onenode-data/frontier-road-rounds-5534016.json), [5534017](onenode-data/frontier-road-rounds-5534017.json)
- mesh C6: [5534022](onenode-data/frontier-c6-mesh-5534022.json), [5534023](onenode-data/frontier-c6-mesh-5534023.json)
- GAPBS selections: [mesh](onenode-data/frontier-gap-mesh-5529591.json), [road](onenode-data/frontier-gap-road-5529591.json)
- RMAT suite: [5534333](onenode-data/frontier-rmat-regression-5534333.json), [5534334](onenode-data/frontier-rmat-regression-5534334.json)
- bimodality probes (not gate audits; per-launch times, slow counts and tail
  splits): [frontier-bimodal-probes.json](onenode-data/frontier-bimodal-probes.json)

Scripts are in `scripts/frontier/` (`build_baselines.sh`, `prepare_inputs.sbatch`,
`smoke_baselines.sbatch`, `check_affinity.sbatch`, `gap_tune.sbatch`,
`onenode_ab.sbatch`, `bimodal_probe.sbatch`, `README.md`). The probe
configurations are `benchmarks/frontier-*-probe-variants.json`; the tail
split is `benchmarks/tail_split.py`. Raw logs stay at
`/lustre/orion/csc710/scratch/rrao/acic/campaign/logs` (`AB-*`, and the
probes' `cxi-stderr/`, `memlog/` and `diag-rounds/`).

**Harness defects found and fixed while running this** (all affected checking,
not solving; no result was recomputed):

1. `check_priority_runs.py` rejected a single-process layout because Reconverse
   prints "1 process", not "1 processes".
2. The same script asserted Delta's 120 workers per node in every manifest.
3. The gate's second binary must be the `ACIC_WORK_COST` build, which emits the
   `WORK_COST` records it parses; the `ACIC_DIAG` build does not.
4. `onenode_ab.py` and `run.py` sized `srun -c` as `128 // rpn`.
5. `check_priority_runs.py` assumed the mesh/road path: it required
   `Process sharing: on`, a `Reader tiles:` line and queue settings without
   `(inactive)`, and computed attempts per edge with a ledger formula that is
   invalid under lazy-heavy. It now derives the expected sharing and tiling from
   the flags and the graph's `.meta` using `process_share_active()`'s
   8-arcs-per-vertex rule, and records time only on lazy-heavy solves. The
   §3–§5 audits re-run to identical summaries.
6. Under `FI_LOG_LEVEL=warn`, libfabric's CXI warnings (820k lines in probe
   5535017) interleaved with ACIC's output and broke the per-source markers the
   harness splits on. `launch_acic.sh` now sends each rank's stderr to its own
   file when `ACIC_STDERR_DIR` is set, and records the step in a `PROBE_STEP`
   line. Its other probe knobs (`ACIC_MEMLOG_DIR`, `ACIC_MEMLOG_NUMA`,
   `ACIC_DIAG_DIR`, `ACIC_PREBIND`, `ACIC_LAUNCH_PREFIX`, `ACIC_LCI_NDEVICES`)
   are off by default, and `onenode_ab.py` takes per-arm environment
   overrides, so earlier configurations launch unchanged.
