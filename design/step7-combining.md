# Step 7.2 — combining: the source hold and the batch-local fold

**Result: both are built, correct, and a net loss. They ship off by default.**
`--combine hold` is 1.17–1.39× slower on RMAT and 1.26–1.38× slower on the
uniform graph. It helps the mesh on one node (0.92–0.93×) and hurts it on two
(1.07–1.12×). `--batch-fold on` is 1.09–1.14× slower on RMAT and 1.07–1.13× on
the uniform graph, and does nothing measurable on the mesh.

Two findings matter more than the ratios, because they change the plan's model
of what combining is for:

1. **Folding redundant updates does not reduce relaxation work.** It removes
   updates that the receiver would have rejected with one comparison. RMAT's
   updates created, the count of edges actually relaxed, do not fall with either
   mechanism. On the mesh they *rise*: +52% with the hold on two nodes and +10%
   with the fold, both outside the rep-to-rep spread.
2. **In this library the hold and the fold catch the same redundancy, and it is
   same-source redundancy.** A delivered batch is one source PE's message, split
   per destination PE on arrival. With the hold on, no source ever puts two
   updates for one vertex in the same message, and the fold then finds exactly
   **zero** duplicates, on every graph. The plan's reason for the fold was that
   "on power-law graphs the dominant redundancy is *across* source PEs". Under
   WPs aggregation, no delivery callback ever sees updates from two sources
   together, so a receiver-side fold cannot reach that redundancy.

## What was built

**`CombiningHold`** (`htram/htram_combine.h`) is written against a byte-oriented
`HoldOps` (`item_size`, `key`, `combine`, `on_absorb`), as the plan's strategy
section requires, so step 8's type erasure lifts it unchanged. One hold per
destination: an open-addressed power-of-two table with an 8-bit tag per slot,
a stable entry pool, and per-bucket reference lists with **lazy decrease-key**.
An improving fold moves the entry and pushes a new stamped reference; the old
reference is skipped at release. Exact live counts per bucket are kept beside
the lists, because the library's release trigger counts admitted items and
the lists include stale references. `htram/tests/test_combine.cpp` model-checks
it against a `std::map` (200 randomized trials, 4M operations, clean under ASan
and UBSan): conservation (inserted = held + released + absorbed), minimum per
key, lowest-bucket-first release, exact live counts.

**In htram** (`HTram::enableCombining`), a destination's hold replaces both
`tram_hold` and the direct path into `msgBuffers`. Every item waits in the hold
until a full buffer's worth has been admitted by the threshold
(`releaseFull`), or until a flush reaches it (`flushDest`, same fillers as
`tflush`). Nothing that is still waiting has become unfoldable.

**`on_absorb` in the app** (`SsspChares::absorb`) gives the retired update's
histogram bucket back and counts it as processed: exactly what the receiver
would have done for a reject. Without it the termination predicate is
unreachable; with it, every configuration terminates and verifies.

**The batch-local fold** (`SsspChares::fold_batch`, `--batch-fold on`) keeps the
minimum per destination vertex within a delivered batch, does the same
bookkeeping for each loser, and processes the survivors in delivery order.

## Measurements

2^20, 16 PEs per node, exclusive Delta CPU nodes, `+setcpuaffinity`, median of
5, variants interleaved within each repetition, all on top of step 7.1's
`--flush-policy adaptive`. Ratios are against `off` in the same job.

**The hold**, two independent jobs per node count (`1node-hold.out` /
`1node-fold.out`, `2node-hold.out` / `2node-fold.out`):

| | mesh | RMAT | uniform |
|---|---|---|---|
| 1 node | **0.92× / 0.93×** | 1.17× / 1.22× | 1.38× / 1.26× |
| 2 nodes | 1.12× / 1.07× | 1.39× / 1.31× | 1.27× / 1.28× |
| items absorbed in the hold (1 node / 2 nodes) | 40% / 51% | 12–13% / 18–19% | 0.1% / 0.2% |
| rejected + absorbed per edge, 2 nodes (off: rejected only) | 2.08 (1.36) | 1.48 (1.43) | 0.95 (0.95) |

**The fold** (`1node-fold.out`, `2node-fold.out`):

| | mesh | RMAT | uniform |
|---|---|---|---|
| 1 node | 0.95× (spread overlaps) | 1.09× | 1.07× |
| 2 nodes | 1.00× | 1.14× | 1.13× |
| folded per edge, 1 node / 2 nodes | 0.52 / 0.91 | 0.12 / 0.21 | 0.001 / 0.002 |
| folded per edge with the hold also on | **0** | **0** | **0** |
| `hold+fold` against `off`, 1 node / 2 nodes | 0.92× / 1.12× | 1.20× / 1.30× | 1.26× / 1.35× |

**Relaxation work**, updates created per repetition, two nodes
(`2node-fold-created-per-rep.txt`):

| | mesh | RMAT |
|---|---|---|
| off | 7.50M – 7.65M | 24.0M – 25.5M |
| fold | 8.21M – 8.42M (**+10%**) | 24.0M – 26.1M (no change) |
| hold | 11.41M – 11.72M (**+52%**) | 24.8M – 26.0M (no change beyond spread) |

## Why it loses

**The fixed cost is paid on every item and bought back only on absorbed ones.**
The uniform graph absorbs 0.1% and is 26–38% slower, which is the cost of the
mechanism with nothing to show for it. A login-node profile (uniform 2^18,
4 PEs) puts the send path at 18.8% of samples without the hold
(`insertValueWPs`, one sequential write per item). With it on, `insert` is 11.3%
and `releaseFull` 14.3% (two random-access table operations per item, a
stamped list push and walk, and a second destination lookup at release).
Compute time went from 0.140 s to 0.187 s. The plan's 8–12% break-even absorb
rate assumed a cost per item this implementation does not reach. It is not
clear any implementation would: the direct path it replaces is one `memcpy`.

**The absorbed work was never expensive.** What a fold removes is an update the
receiver would have rejected after one comparison against `distances[]`.
Rejected + absorbed per edge on two nodes is *at least* what `off` rejected
(RMAT 1.48 against 1.43), so the hold is catching rejects, not preventing
relaxations. Step 6 already said where RMAT's redundancy lives: hub contention,
updates that land "in the same bucket at the same time". The hold catches the
copies that happen to come from one source PE, and those are the cheap ones.

**On the mesh the work goes up, and that is not explained.** Two candidate
causes were ruled out on a login node at 2^18
(`login-node-diagnosis.txt`):

- *The controller.* Absorbed updates leave the histogram at the source, which
  could move the percentile cut. It does not: mean reach
  (`heap_threshold − first_nonzero`) is 6.87 buckets without the hold and 6.83
  with it on the mesh, and 11.5 against 12.1 on RMAT.
- *Filler padding.* The hold's flush pads any short message with
  above-threshold items, where `tflush()` pads only the message carrying
  released held items. Rebuilding htram without `ADD_FILLERS` leaves the effect
  in place: mesh created +14%, RMAT +8%.

What the counters do show is that on the mesh the number of distance changes
barely moves (464,805 → 463,444) while relaxations rise (created is four times
the relaxations at degree 4): a larger share of improvements get relaxed
before something better supersedes them. The fold does it too, which rules out
anything specific to holding items at the source. The remaining suspect is
timing: a mechanism that makes each round's traffic cheaper also changes which
updates arrive before a vertex is popped. That is a hypothesis, not a result.

## Correctness

The gate passes all 18 configurations plus the 3 diagnosis-build checks under
`--combine off`, `--combine hold`, `--combine hold --flush-policy fixed`,
`--batch-fold on`, `--combine hold --batch-fold on`, and the defaults. The
two-node gate passes all 15 configurations under `off`, `hold` and
`hold --batch-fold on`. It now includes two RMAT rows, since hub traffic is what
combining folds.

Running the two-node gate for this step exposed a bug in the gate itself.
Submitted with `sbatch`, the script `cd`'d relative to Slurm's spooled copy of
itself and exec'd a binary that does not exist, so every configuration failed.
It now starts from `$SLURM_SUBMIT_DIR`.

## Consequences for the plan

- **Keep both mechanisms, off.** They are correct, cheap to carry, and the
  ablation figure needs them. Their measured contribution is negative, and the
  paper should say so.
- **Drop the adaptive combining on/off policy** (disable below 8% trailing
  absorb rate). It rested on combining paying above a break-even rate. The
  mesh absorbs 40–51% and still loses on two nodes. RMAT absorbs 13–18% and
  loses at every node count. There is no rate at which it pays.
- **Cross-source redundancy is still unaddressed, and no receiver-side fold can
  reach it** under WPs. Reaching it needs a structure that sees updates from
  several sources before any of them is relaxed. Two candidates: a per-vertex
  "best pending distance" check at the *receiver's* relaxation (partly what
  `distances[]` already is), or aggregation that merges sources before delivery
  (the node-level sort in `HTramRecv::receive` does see all sources on a node,
  per message). Neither is in the plan, and neither is justified by anything
  measured here, since the redundancy caught so far was all cheap.
- **The scale-free deficit is not a combining problem either.** Step 6 left
  combining as the only candidate among H1–H4 for RMAT. It does not close the
  gap. The next question for RMAT is where its time actually goes when
  relaxation work is fixed: hub fan-out (one relaxation of a 69,197-degree
  vertex creates 69,197 updates) is the obvious candidate. That needs its own
  profile on RMAT; the one above is of the uniform graph and was taken to cost
  the hold, not to answer this.

## Reproducing

```
sbatch [-N 2] scripts/ab_delta.sbatch scripts/ab/7.2-combining-hold.txt 20
sbatch [-N 2] scripts/ab_delta.sbatch scripts/ab/7.2-batch-fold.txt 20
SSSP_EXTRA_ARGS="--combine hold --batch-fold on" sbatch scripts/verify_2node.sh
make -C ../htram test_combine
```
