# Step 7.6 — the progress repair

**Result: a run whose frontier left the controller's window could not get it
back, and stopped. One update was never counted, which made "the window is
empty" read as "the window sums to -1", which stopped the one rescue that
covers this case from firing. The source update is now counted; six fixtures
that used to hang now finish in milliseconds, with the same answers.**

This is item 1 of `design/step75-comparisons.md`'s next work, and it was placed
first because it is a correctness gate: two cells of the comparison matrix are
censored by it, and no performance conclusion above it is worth anything until
a run is guaranteed to finish.

## What failed

The step 7.5 campaign recorded three losses of progress, in two shapes.

| Job | Case | Policy | State at the timeout |
|---|---|---|---|
| 22033411 | rmat22, 1 node x 120 workers, source 3882232 | tuned fixed, `--bucket-width 3` | 246,571,826 created, 246,100,381 processed, **empty reduced window** |
| 22033887 | rmat22, 8 x 15 layout, source 3882232 | the same transferred policy | 25,649 created, **3** processed, empty reduced window |
| 22032689 | mesh20, 2 nodes x 16 workers, source 736504 | current defaults | 605,016 created, 601,703 processed, thresholds frozen at 102/125 |

The first two are the shape this note repairs. They are reproducible -- the
fixed policy failed all five replays of job 22034066 while the current defaults
passed all five -- and they share a signature the campaign wrote down but could
not yet read: **an empty reduced window with work still outstanding.**

The third is the intermittent one, one query in sixteen, and its thresholds are
not at a window origin. It is a different state and this note does not claim it.

## The mechanism

The controller does not see the whole priority range. It reduces a window of
`histo_reduction_width` = 256 of the 2048 buckets, starting at the lowest
bucket it last saw occupied, and computes both admission thresholds from
percentiles of what is inside it. Two facts about that window matter here:

1. **It only moves forward, and only to something it can see.** `first_nonzero`
   is the lowest bucket inside the window holding a positive count. A window
   holding nothing leaves the origin where it was.
2. **Work can leave it in one step.** A bucket width of 3 makes the window span
   768 distance units, and these inputs have edge weights up to 1000. One
   relaxation can therefore move a vertex from inside the window to past its
   right edge. On rmat22 with `--bucket-width 3` this is not an edge case: on a
   16K-vertex RMAT with the same policy, 13 of 15 controller rounds have the
   frontier outside the window.

The code has a rescue for exactly this. If the window sums to zero, both
thresholds go to `HISTO_BUCKET_COUNT - 1` and everything is admitted: the
controller cannot aim at work it cannot see, so it stops aiming. That is a
guaranteed step -- every live update is then below both thresholds, so every PE
can retire what it holds and `updates_processed` has to rise.

The rescue tested `histogram_sum == 0`, and the window did not sum to zero. It
summed to **-1**.

The source update is injected by `Main::begin()` and retired like any other:
processed, with its bucket handed back. Nothing ever charged it. Bucket 0 --
where distance 0 falls -- therefore held a permanent -1 in the global
histogram, which the termination test accounted for by asking for
`processed == created + 1` rather than for equality. Bucket 0 is inside the
window for as long as the window has not moved, which is precisely the state
this failure is in. So:

* the window holds no live update, and sums to -1;
* `histogram_sum == 0` is false, and the rescue does not fire;
* the percentile scan looks for the first bucket whose running total reaches
  `histogram_sum * heap_percent`, a negative target it starts below and never
  reaches, so both thresholds keep their initial value of 0 -- which is to say,
  the window's own origin;
* nothing above the origin is admitted, so nothing is processed, so no bucket
  in the window ever becomes occupied, so the window never moves.

The run then turns reductions over as fast as the network allows and changes
nothing: 1.83 million of them in the recorded mesh failure, 2.7 million in the
smallest reproduction here, until the timeout.

## The repair

`start_algo()` charges the source update to the PE it starts on, in both the
histogram and `updates_created_locally`, and the termination test becomes
`updates_created > 0 && updates_processed == updates_created`. The `> 0` is
what used to be proved by the `+ 1`: it says the source has started. Small
components and an isolated source still terminate -- a degree-zero source
creates exactly its own update and retires it.

The invariant this restores is the one everything else in the controller reads:
**a bucket's global count is the number of live updates in it, and is never
negative.** With it, `histogram_sum == 0` means what the rescue needs it to
mean. The rescue is written `<= 0` anyway, because a window that sums below
zero is this same situation seen through an accounting error, and the failure
mode of getting that wrong is a hang rather than a wrong answer.

A progress argument, rather than a watchdog: in any round the reduced window
either holds a live update, in which case the percentile scan returns a
threshold at or above the lowest occupied bucket and the work in that bucket is
admitted; or it does not, in which case the rescue admits everything. Either
way at least one live update is admitted somewhere, and an admitted update is
retired without needing another round. `updates_created - updates_processed` is
finite and cannot rise without an update being created, which requires one to
be retired first. `design/step75-comparisons.md` says a watchdog that forces an
exit is not a repair, and this is not one: nothing here ends a run.

## Diagnostics

The failure was recorded in September and could not be read from what the run
printed, so the repair comes with the vocabulary to read the next one.

* **The conservation invariant is now computable.** Every live update is
  charged to exactly one bucket, so `created - processed` is the global
  histogram's total over all 2048 buckets. The reduction carries 256 of them.
  The difference, `above_window`, is the work past the window's right edge --
  the one thing the controller otherwise cannot tell apart from having
  converged. It cannot be negative, and Main says so when it is.
* **Loss of progress is detected and reported.** Both global sums standing
  still between two rounds means no update was created or retired anywhere.
  After 256 such rounds Main prints one `PROGRESS_STALL` line -- window origin
  and width, `first_nonzero`, occupied buckets, span, window sum,
  `above_window`, clamped mass, both thresholds, bucket scale -- and each PE
  prints one saying where its share of the outstanding work actually is:
  lowest and highest live bucket over the whole range, heap size, held items,
  and htram's held, admitted and buffered counts. The report repeats on a
  doubling schedule, so a run that stalls for two minutes prints about a dozen
  lines rather than filling the log.
* **Coarsening over a non-empty clamp bucket is reported.** See the last
  section: `COARSEN_CLAMPED` names the merge factor, the clamp count, and the
  bucket the mass is stranded at.
* **`HTram::pendingItems()`** splits what a PE is holding three ways: held in a
  per-destination hold whatever its bucket, admitted by the threshold, and
  sitting in a partly filled buffer. That is what distinguishes "the threshold
  is not admitting it" from "it is admitted and nothing has flushed it".
* **Blind rounds are counted.** Every run now prints `Rounds with the frontier
  outside the window`, and the largest number of updates that were ever outside
  it. A round in that count admitted everything, which is to say it exercised
  no admission control at all.

The stall report on the smallest reproduction, from the pre-repair binary,
states the whole failure in two lines:

```
PROGRESS_STALL 1 main rounds_without_progress=256 ... created=8 processed=6
  live=2 window_first=0 window_width=256 first_nonzero=0 occupied=0 span=0
  window_sum=-1 above_window=3 clamped=3 heap_threshold=0 tram_threshold=0
PROGRESS_STALL 1 pe=0 created=8 processed=6 ... lowest_live_bucket=2047
  highest_live_bucket=2047 pq=3 pq_hold=0 tram_held=0 heap_threshold=0
```

`window_sum=-1` with `occupied=0` is the bug; `heap_threshold=0` with the work
at bucket 2047 is its consequence.

## The regression

`scripts/verify.sh` grows six progress fixtures, run after the digest checks.
Three are the step 7.5 policy verbatim -- `--flush-policy fixed
--flush-interval 1 --bucket-policy fixed --idle-flush off --bucket-width 3` --
on the smallest sources that reproduce its hang: RMAT 16384/262144 seed 1 from
sources 27 and 31, and the 10000-vertex uniform graph from source 0. The other
three force the same geometry from a width so narrow that every edge leaves the
window, once over the coarsening path and once over a delayed round.

Every one of the six hangs on the pre-repair binary and finishes on the
repaired one, and the fixtures fail the gate on two separate grounds: a run
that does not match serial Dijkstra, and a run that finishes but printed a
`PROGRESS_STALL` on the way. These sources are not in `golden_digests.txt` --
they are chosen for the hang, not for the generator -- so the answer is checked
against the in-process serial reference instead.

## What this does not fix

* **The window still cannot follow work that jumps past it.** The rescue
  restores progress by giving up admission control, and it does not move the
  window, so a run that gets into that state stays in it: on the 16K RMAT with
  `--bucket-width 3`, 13 rounds of 15. That is a controller-robustness
  question, item 2 of the step 7.5 list, and the blind-round counter is there
  to measure it. It is also the first candidate for the work explosion in that
  note's mesh22 counter table, where current delivers 16 times as many updates
  as tuned fixed at 16 nodes.
* **The intermittent two-node mesh failure is not explained.** Its thresholds
  were frozen at 102/125, not at a window origin, so the window arithmetic
  above does not describe it.

  There is a candidate, and the run's own numbers are what suggest it. That
  query ended with `Bucket scale: 20 (1 coarsenings)` and
  `first nonzero: 102`, and **2047 / 20 = 102**. The clamp bucket is the one
  bucket a merge cannot place: its contents mean "an original index at or past
  2048 x scale", not an index, so `coarsen_buckets` sending them to 2047 / k
  strands them. Whoever retires such an update recomputes its bucket from the
  distance at the new scale, finds a real index somewhere above 2048 / k, and
  leaves a live count at 2047 / k that nothing will take away -- and if that is
  the lowest count the controller can see, the window pins there exactly as
  observed. `choose_coarsening` already refuses while the reduced clamp count is
  positive, but it reads that count as it stood when the chares contributed,
  and a chare keeps creating and retiring updates between contributing and
  receiving the broadcast that carries the coarsening. That gap is open.

  **The mechanism is demonstrated; the race that lets it happen in the field is
  not.** Forty file-mode mesh runs left to themselves produced no instance --
  the guard does its job -- so the mechanism was checked by removing the guard.
  `--coarsen-clamped allow` on a 10,000-vertex file-mode mesh reproduces the
  recorded signature on demand:

  ```
  COARSEN_CLAMPED pe=0 k=6 scale=1 clamp_bucket=199 strands_at=341
  PROGRESS_STALL 1 main ... window_first=341 first_nonzero=341 occupied=5
    window_sum=232 above_window=0 heap_threshold=341 tram_threshold=341
    bucket_scale=6
  PROGRESS_STALL 1 pe=0 ... live=68 lowest_live_bucket=183 highest_live_bucket=351
    pq=0 pq_hold=90 tram_held=4 tram_admitted=0 tram_buffered=0
  ```

  341 is 2047 / 6. Three PEs merged with 199, 140 and 85 updates charged to the
  clamp bucket between them; the 424 phantom counts that leaves at 341 become
  the lowest thing the controller can see, the window pins there, both
  thresholds pin with it, and real work at bucket 183 ends up *below* the
  window, where it is as invisible as work above it. 2.9 million reductions, no
  convergence, and `VERIFY FAIL: run was truncated`. `above_window = 0` is what
  separates this from the failure the repair above covers: there the live work
  is all outside the window, here it is all inside and most of it is not real.

  What is still unproven is that the shipped `block` guard lets this through in
  the field. It refuses while the reduced clamp count is positive, and the gap
  argued above is the only way past it. That gap is consistent with a failure
  that happens to one query in sixteen, and 2047 / 20 = 102 is consistent with
  the recorded run, but consistency is not evidence. So what ships is the
  detector rather than a repair: `coarsen_buckets` prints `COARSEN_CLAMPED`,
  once per PE, naming the merge factor and the bucket the mass is stranded at,
  and the gate fails on it. A replay that prints it before pinning has proved
  the race; one that pins without printing it refutes this entirely.

  `--coarsen-clamped strict` -- refuse to merge for the rest of the run once
  the clamp count has ever been positive -- is the candidate repair, and it is
  conservative: on a graph that ever clamps, it turns coarsening off. What that
  costs is an item 2 measurement, not a guess, which is why it is a flag and
  not a default.

  `--coarsen-clamped allow` is deliberately unsound and the gate does not run
  it: over the 18-configuration matrix it produced a 574 MB log from one 45
  second run, because the per-round console output the production build prints
  is the only thing a stalled run does quickly.

## Reproducing

```
scripts/verify.sh                 # includes the six progress fixtures
# the smallest reproduction, on a pre-repair binary:
./sssp_smp 16384 262144 1 27 3 0.999 0.005 --timeout 15 \
    --flush-policy fixed --flush-interval 1 --bucket-policy fixed \
    --idle-flush off --bucket-width 3 +ppn 4
```

On Delta, `benchmarks/run.py --mode progress` replays every recorded failure
under its own failing policy, on the binary that produced it and on the
repaired one, in the same allocation and in randomized order, with and without
a delayed round:

```
sbatch -N 1 benchmarks/compare.sbatch CAMPAIGN --mode progress --workers 120
sbatch -N 2 benchmarks/compare.sbatch CAMPAIGN --mode progress --workers 16
```
