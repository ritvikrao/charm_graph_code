# Step 7.4 — flushing when the PE has nothing left to do

**Result: `--idle-flush starved` ships on by default. It is a 1.09x speedup on
the one-node mesh and a 1.12x speedup on two-node RMAT, and inside the
harness's own position bias in the other four cells.** The ungated variant,
`--idle-flush on`, is 1.12–1.16x *slower* on the uniform graph at both node
counts, and is kept only as the ablation's other arm.

Speedups below are `baseline / variant`, so above 1 is faster. `diag_report.py`
prints the reciprocal in its "vs base" column, which is what the raw outputs in
`design/step7-data/7.4/` contain.

The two wins come from different mechanisms, which is the interesting part:
on the mesh it removes **rounds**, and on RMAT it removes **relaxation work**.
Step 7.2 and 7.3 both left RMAT's updates-created count untouched
(77.1M against 78.2M for adaptive bucketing at 2^22); this is the first thing
built in step 7 that reduces it.

## What was built

`--flush-policy` decides what happens at a *round boundary*: `adaptive` flushes
the destinations that are not filling their own buffers, whenever the last
reduction said the library is starved. Between boundaries nothing pushes a
partial buffer out. A PE that has drained its heap and has no admissible work
left cannot add to any buffer until a message arrives, so every item it is
holding is waiting on a round it could have shortened.

**`HTram::flushIdle()`** (`htram/htram_group.C`) is `flushStale()` without the
`full_sends` test: every destination with admitted items or a non-empty buffer
gets `flushDest()`, which drains the held items and ships the partial buffer
with the usual fillers. It counts `idle_flushes`, reduced as a ninth value from
`tramStats`.

The cost constraint is what shapes it. Reconverse raises `STILL_IDLE` on *every*
idle scheduler pass, and a `[whenidle]` entry that returns true re-registers
itself, so this is called continuously while a PE is idle — not once per idle
period. After the first call nothing is buffered, so the loop has to be cheap
in the common case: it is one comparison per destination, and `destCount()` is
`CkNumNodes()` under WPs. Outside WPs/WW it is a **no-op**, deliberately.
`flushStale()` can fall back to a whole-library `tflush()` because it runs once
per round; here that fallback would be a busy loop shipping empty messages.

**`--idle-flush off|on|starved`** (`sssp_smp.cpp`) calls it from
`idle_triggered()`, the `[whenidle]` entry that already drains the heap:

- `off` — the step 7.3 behaviour.
- `on` — whenever the PE goes idle.
- `starved` — only while the last round was starved, the same signal
  `--flush-policy adaptive` gates on. **Default.**

Two details matter. `process_heap()` re-enqueues itself when it has yielded
(every 100 pops, or after a `pq_hold` bucket), and a PE with a re-queued entry
method is not idle; `heap_yielded` suppresses the flush in that case, so the
flush only happens when the heap is genuinely out of admissible work. And
`last_round_starved` is latched in `current_thresholds`, so the gate reflects
the most recent closed-loop round rather than a local guess.

## Measurements

2^20, 16 PEs per node, exclusive Delta CPU nodes, `+setcpuaffinity`, variants
interleaved within each repetition, on top of the 7.1 and 7.3 defaults.
`off-again` is the baseline configuration run last in each repetition: its
number is the harness's own position bias, not a result.

**Four-variant sweep, 10 repetitions** (`sweep-1n.out`, `sweep-2n.out`):

| | mesh 1n | mesh 2n | RMAT 1n | RMAT 2n | uniform 1n | uniform 2n |
|---|---|---|---|---|---|---|
| `on` | **1.13x** | 1.00x | 1.00x | 1.00x | **1.16x slower** | **1.12x slower** |
| `starved` | 1.07x | 1.02x | 1.04x | 1.06x | 1.00x | 1.02x |
| `off-again` | 1.02x | 1.03x | 0.99x | 0.98x | 1.07x slower | 1.00x |

**Confirmation, 20 repetitions, `off` / `starved` / `off-again` only**
(`confirm-1n.out`, `confirm-2n.out`):

| | mesh | RMAT | uniform |
|---|---|---|---|
| `starved`, 1 node | **1.09x** | 1.01x | 0.99x |
| `off-again`, 1 node | 1.02x | 1.00x | 0.98x |
| `starved`, 2 nodes | 1.01x | **1.12x** | 0.99x |
| `off-again`, 2 nodes | 1.00x | 0.99x | 1.00x |

Both effects survive the rerun, and the RMAT one grows (1.06x to 1.12x). The
four cells that read 0.99–1.02x are indistinguishable from their controls.

## Why it wins, twice, for different reasons

**One-node mesh: fewer rounds.** Reductions fall 3,292 to 2,952, a 10% cut,
while updates created rise only 5.3% (6.313M to 6.646M) and messages 10%.
H4 established that the mesh run is cadence-bound end to end — 4 ms of added
round delay multiplies runtime 27.7x — so removing a tenth of the rounds is
worth roughly a tenth of the runtime, and that is what it is worth. Note that
the idle flush largely *replaces* the round-boundary one rather than adding to
it: stale flushes fall from 22,552 to 5,042 while 19,750 idle flushes take over.
The same destinations get flushed, earlier.

**Two-node RMAT: less work.** Rounds are unchanged (74.5 against 74), but
updates created fall 25.05M to 21.90M (−12.6%), distance changes 1.853M to
1.596M (−13.9%), bytes sent 601M to 526M, and rejects per edge 1.455 to 1.269.
Relaxations arrive sooner, so `distances[]` is tighter when later updates for
the same vertex are generated, and fewer improving relaxations ever happen.
Step 6 called RMAT's redundancy "updates that land in the same bucket at the
same time"; shortening the time an update spends buffered is the first thing
tried that reduces it.

**It needs real latency to do that.** One-node RMAT shows nothing (1.01x,
created 20.03M against 20.00M), and fires 62 idle flushes against two nodes'
354. With `destCount() == 1` a buffered update is a pointer handoff away from
its destination, so holding it costs almost nothing and releasing it early
changes no ordering. The mesh gain at one node is not a counterexample: that
one is about rounds, not about delivery latency.

**Two-node mesh: the two effects cancel.** Rounds fall 15% (2,780 to 2,360) —
a bigger cut than at one node — but updates created rise 17% (8.49M to 9.93M)
and distance changes 16%. Net 1.01x. This is the same shape step 7.2 found on
the mesh, where the source hold raised created by 52%: a mechanism that gets
mesh updates moving earlier gets more of them relaxed before something better
supersedes them. At one node that penalty is 5%; across nodes it is 17%, and it
eats the round saving.

**Why `on` loses on the uniform graph.** Same bytes, twice the messages: 8,471
to 13,779 at one node and 9,018 to 17,896 at two, with bytes sent flat to
within 1%. The uniform graph's buffers fill on their own, so an idle PE that
flushes is converting a full buffer into two partial ones for nothing. `starved`
fires 133 and 713 idle flushes in those same cells against `on`'s 5,544 and
9,578 — the gate is doing exactly the job it was added for.

**A calibration worth keeping.** On the one-node mesh `on` and `starved` fire
almost the same number of idle flushes (19,190 against 19,540) yet read 1.13x
and 1.07x. Two configurations doing near-identical work differ by 4%, which is
the clearest statement yet of this harness's resolution — consistent with the
~5% position bias step 7.3 measured, and a reason to keep a control variant in
any A/B claiming less than that.

## Correctness

The local gate passes all 18 configurations plus the 3 diagnosis-build checks
under `--idle-flush off`, `on`, `starved`, `on --combine hold`, and the new
default. `scripts/verify_2node.sh` passes all 15 configurations under `on` and
under `starved` (`2node-gate-on.out`, `2node-gate-starved.out`). The digests are
byte-identical across every setting, which is the step 7 gate: the flag changes
the schedule, not the answer.

The combining path needed no special case. `flushDest()` already drains a
destination's hold before shipping, so `flushIdle()` works with `--combine hold`
unchanged, unlike step 7.3's bucket coarsening, which cannot re-split in-flight
items and falls back to fixed buckets.

## Consequences for the plan

- **H4's idle-flush item is answered, and re-enabling the 2024 stub would not
  have answered it.** The plan asks to "re-enable htram's idle-triggered partial
  flush (`IDLE_FLUSH` is `#if`'d out at `htram_group.h:7`; `idleFlush()` is a
  stub)". Reading it: `idleFlush()` calls `tflush(true)`, which ships a
  destination's `msgBuffers` entry when it is more than `PARTIAL_FLUSH` (0.2)
  full. That gate is a static per-buffer occupancy rule, not the controller's
  starvation signal — and under `BUCKETS_BY_DEST` it never touches
  `tram_hold`, where admitted items actually wait; only the direct path below
  `direct_threshold` stages items in `msgBuffers` at all. So the stub would
  release almost none of what this step releases, and neither win would have
  appeared. What was needed was a new entry point (`flushIdle()`, built on
  `flushDest()`) plus a gate the library cannot compute, since only the
  application sees the reduction. The `IDLE_FLUSH` stub stays off.
- **Cadence work is finished, and it was worth about 4x on the mesh.** 7.1
  flushing (3.6x/3.8x) and 7.4 rounds (1.09x at one node) are the same lever
  pulled at two granularities, and the second pull is much smaller than the
  first. There is no third.
- **The mesh's "more updates created when traffic gets cheaper" effect is now
  seen three times** — the 7.2 hold (+52%), the 7.2 fold (+10%), and this
  (+17% at two nodes, +5% at one). It is reproducible, it scales with node
  count, and it is still unexplained. It is the best remaining lead on the
  mesh, and it is a *bucketing/ordering* question, not a communication one: the
  candidate is that `heap_threshold` admits work that a tighter frontier would
  have deferred. Worth a diagnosis build before the paper claims the mesh is
  solved.
- **RMAT has a lever after all.** Steps 7.2 and 7.3 both left its relaxation
  work unchanged and step 6 left combining as the only H1–H4 candidate for it.
  Cutting buffered-update latency cuts its work 13% at two nodes. That points
  at delivery latency, not redundancy, as the thing to attack next, and it is
  consistent with 7.2's finding that the redundancy reachable at the source was
  all cheap.

## Reproducing

```
sbatch [-N 2] scripts/ab_delta.sbatch scripts/ab/7.4-idle-flush.txt 20
SSSP_REPS=20 sbatch [-N 2] scripts/ab_delta.sbatch scripts/ab/7.4-idle-confirm.txt 20
SSSP_EXTRA_ARGS="--idle-flush starved" sbatch scripts/verify_2node.sh
SSSP_EXTRA_ARGS="--idle-flush off" scripts/verify.sh
```
