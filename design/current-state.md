# ACIC current evidence

*Status at repository revision `48b6c2b`, 2026-09-23. This document replaces
the individual experiment narratives. Raw summaries remain under
`design/onenode-data/`; job logs remain in the recorded campaign directories;
the deleted narratives remain in Git history.*

## Executive conclusion

ACIC has one accepted performance result. On `mesh26-z`, the frozen candidate
beats tuned one-node GAPBS by 1.21–1.41× at eight Anvil nodes and 1.27–1.43× at
sixteen Frontier nodes. Both cells use 896 PEs, four held-out sources and two
allocations. Removing heap slice 8 loses the win, so the cadence mechanism is
causal rather than a favorable baseline draw.

The result is specific to sparse meshes. Road remains 1.45–1.51× slower than
GAPBS after its work is brought close to the useful-work floor. Scale-free
graphs remain 1.87–3.65× slower than RIKEN in the last complete comparison.
No measured live adaptive policy causes the accepted result.

## Accepted result matrix

| Regime | ACIC result | Interpretation |
|---|---|---|
| `mesh26-z`, Anvil | 8 nodes, 16 processes/node × 7 workers/process; ACIC/GAPBS 0.71–0.83 | Accepted win on four held-out sources in jobs 20866513/20866514. GAPBS references are jobs 20866515/20866516 plus the earlier fixed-setting cells. |
| `mesh26-z`, Frontier | 16 nodes, 8 × 7; ACIC/GAPBS 0.70–0.79 | Independent-machine reproduction at the same 896 PEs, jobs 5534022/5534023. Eight Frontier nodes reach parity rather than a win. |
| `road-usa-z`, Anvil | Best fixed width/cap arms remain about 1.5× behind GAPBS | Width 32K reaches about 1.5 attempts/edge but needs 889–1,444 rounds. Width 128K trades 1.7–2.0 attempts/edge for 520–800 rounds and ties cap 7 with 40–60% less work. Jobs 20868020–22 and 20876828–30. |
| `road-usa-z`, Frontier | Width 128K is 1.45–1.51× behind GAPBS | Two allocations agree within about 1%; jobs 5534016/5534017. |
| Scale-free suite | ACIC speeds up from 2 to 8 nodes but trails RIKEN by 1.87–3.65× | Last complete 8g comparison at application revision `de0ed1c`. The newer high-diameter paths resolve inactive, but the formal frozen-binary regression is incomplete. |

## Mechanism chain established September 18–23

### 1. Scale-free work growth was repaired

Lazy heavy-edge relaxation, an htram hold bitmap, revised idle flushing and
empty-delivery suppression changed the scale-free trend. ACIC became 1.1–2.3×
faster from two to eight nodes on `rmat25`, Orkut, `rmat26` and `rmat27`.
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
polling/order adds 30–45% road work and time. On the same current runtime,
`+old-scheduler` matches the earlier v0916 behavior within the control floor.
Jobs 20841653/20841654 contain the matched comparison. Every current
performance run therefore uses `+old-scheduler`.

### 5. Heap cadence produces the mesh win

A fixed process drain cap controls road speculation but hurts mesh. Heap slice
8 instead yields to the scheduler after eight removals, allowing messages and
other tasks to interleave. It reduces distributed mesh rework enough to beat
GAPBS at 896 PEs on both machines. On one Frontier node, where cross-process
rework is small, the same slice costs 5–7%; activation is graph/scale
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
reproduces, but RMAT and uniform launches randomly enter a roughly 18% slower
mode. A whole launch moves together; work counts remain the same, reductions
stay short and remote updates trickle through the tail. Frozen R0 shows the
same behavior, so it is not caused by the recent candidate policies.

Send caps, packet/rendezvous behavior, receive matching, LCI device count,
ASLR, huge pages, NUMA placement/prebinding, fabric congestion and adaptive
solver state have been ruled out. Jobs 5535017–5535803 cover those probes. The
remaining bounded hypothesis is Reconverse backend progress/polling.

## Evidence and provenance

| Evidence | Machine-readable record / configuration |
|---|---|
| Anvil mesh C6 | `design/onenode-data/c6-mesh-8n-anvil-20866513.json` and matching 20866514 record; `benchmarks/c6-mesh-8n-variants.json` |
| Frontier mesh C6 | Frontier summaries under `design/onenode-data/` with job IDs 5534022/5534023; `benchmarks/frontier-c6-mesh-variants.json` |
| Anvil road ordering/rounds | `design/onenode-data/road-order-8n-anvil-20868022.json`, road-round records 20876828–30; `benchmarks/road-order-8n-variants.json`, `benchmarks/road-rounds-8n-variants.json` |
| Frontier road | `design/onenode-data/frontier-road-rounds-5534016.json` and 5534017; `benchmarks/frontier-road-rounds-8n-variants.json` |
| Runtime scheduler | layout and old-scheduler JSON summaries under `design/onenode-data/`; `benchmarks/oldsched-variants.json` |
| Frontier RMAT behavior | `design/onenode-data/frontier-rmat-regression-5534333.json` and 5534334 plus probe configurations named `benchmarks/frontier-*-probe-variants.json` |

Anvil raw logs are rooted at `/anvil/scratch/x-rrao/acic/`; Frontier raw logs
at `/lustre/orion/csc710/scratch/rrao/acic/campaign/logs/`. Build identities
and paths are consolidated in [configurations.md](configurations.md).

## Claims supported now

1. At 896 CPU PEs, sliced asynchronous ACIC beats tuned one-node GAPBS on the
   measured `mesh26-z` class on two machines and held-out sources.
2. Process-wide priority plus batched removal reduces redundant sparse-graph
   work, and heap slicing converts that reduction to a distributed mesh gain.
3. On road, a representable global ordering window approaches minimal edge
   work, after which controller round cost is the dominant measured limit.
4. Runtime scheduling materially changes asynchronous SSSP work; registered
   scheduling causes a reproduced 30–45% regression on road.

## Claims not supported

- ACIC is generally faster than GAPBS, RIKEN or GPU SSSP systems.
- Live feedback or algorithm/communication co-design causes the current win.
- High-diameter graphs generally favor ACIC; the real road graph still loses.
- ACIC has demonstrated a fixed-candidate strong-scaling curve to many nodes.
- The current candidate is regression-free on RMAT; its formal frozen-binary
  gate is incomplete.
- Frontier launch bimodality is caused by LCI, the network or ACIC. Evidence
  localizes it only to remote tail progress.
