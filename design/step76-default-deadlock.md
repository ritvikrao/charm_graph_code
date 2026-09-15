# The shipped default deadlocks, and item (1) is not closed

Item (1) of the stopping rules is progress repair, with the rule attached that
*a watchdog that forces an exit is not a repair*. It was closed earlier in
step 7. Job 22071815 reopens it, and the evidence is not about `--range-extend`
at all.

## What was seen

Of 253 runs at one node, eight did not finish. Five of them are baseline arms:
three `logv-frozen` and two `control`, all on mesh22, all with `--range-extend`
off and therefore running the configuration that ships today. (The other three
are the range arm, and belong to `step76-range.md`.)

The runs do not run slowly. They stop. `PROGRESS_STALL` is emitted repeatedly
over the life of each hung run, and every counter in it is bit-identical from
the first report to the last:

```
run2  t 0.1 -> 114.8  live=5973  clamped=5969  first_nonzero=511  heap_thr=511  occupied=1
run3  t 0.1 ->  59.1  live=5639  clamped=5629  first_nonzero=204  heap_thr=204  occupied=1
run4  t 0.1 ->  58.8  live=5618  clamped=5613  first_nonzero=157  heap_thr=157  occupied=1
run6  t 0.1 ->  60.1  live=6592  clamped=6573  first_nonzero=170  heap_thr=170  occupied=1
run7  t 0.1 -> 117.8  live=4955  clamped=4937  first_nonzero=682  heap_thr=682  occupied=1
```

`live`, `window_sum`, `clamped`, `first_nonzero` and the thresholds do not move
by one across two minutes. The controller keeps turning -- it is the thing
counting `rounds_without_progress` -- but nothing underneath it moves.

These are the unextended stalls and they can be told apart from the range arm's
without ambiguity, because only the extend path raises `bucket_limit`. At one
node 7,438 stall lines carry `bucket_limit=2048.000000`, its initial value. At
eight and sixteen nodes there are none: every stall line there has a raised
limit, which is why only the range arm failed at those allocations.

## The part of the diagnosis that is solid

`clamped` is within twenty of `live` in five of the eight, and `occupied=1`.
Nearly every update still alive is above the window, in the overflow slot, and
exactly one bucket inside the window holds anything at all.

That is 7.6e's range diagnosis reaching its end state. Under `clamp_freeze`,
`bucket_limit` is set once to `HISTO_BUCKET_COUNT` and then never again for a
run that does not extend, while `bucket_scale` multiplies on every coarsen --
here to 3, 4, 10, 12 and 13. So the set of distances that miss the overflow
slot is frozen at `distance < 2048 * width` for the whole run, and coarsening
does not widen it; it only compresses the distances that are already in range
into `2048 / bucket_scale` indices. Once the frontier passes the clamp, the
live population goes to the overflow slot, which is excluded from merges, and
stays.

What 7.6e recorded as *ordering is lost* is therefore too mild. The terminal
case is not a slow run. It is a run that stops, and it happens in the shipped
default.

## One diagnosis attempted here was wrong, and the reason matters

The first version of this note claimed the stall was not threshold starvation,
on the grounds that most PEs in a stall report a `lowest_live_bucket` at or
below their own `heap_threshold` -- work they are already allowed to run -- and
that some report live work with every queue empty, which looked like phantom
counts pinning the window. Both readings are wrong, and they are wrong for the
same reason.

`report_progress_state` derives `live`, `lowest_live_bucket` and
`highest_live_bucket` from this PE's `histogram[]` array. That array is not an
inventory of this PE's work. The histogram is a distributed ledger: it is
incremented by the PE that **creates** an update and decremented by the PE that
**retires** it, which is the property commit `1a46a9e` established and which
makes re-binning impossible. So a PE's `histogram[b] > 0` means *this PE
created an update in bucket b that nobody has retired yet*, and that update is
almost certainly sitting on some other PE. It follows that:

- `lowest_live_bucket <= heap_threshold` says nothing about whether this PE
  holds runnable work, so the 73,537 "admissible" lines are not evidence of a
  drain failure;
- a PE with live counts and empty queues is the **normal** case, not a phantom
  -- it created updates that are now elsewhere;
- per-PE `live` may legitimately be negative, which is why `live=-26` appears
  and is not in itself a conservation violation.

Only the `main` line, which sums the histogram across PEs, carries the meaning
those per-PE fields look like they carry. Everything below is taken from it.

What the global line does say, for a one-node baseline stall:

```
live=5973  clamped=5969  first_nonzero=511  heap_threshold=511  occupied=1
```

5,969 of 5,973 live updates are above the window, in the overflow slot. Four
are in-window at bucket 511, with the threshold at 511. The run is therefore
within four updates of having nothing admissible at all, and the 5,969 in the
overflow slot cannot be admitted, because under `clamp_freeze` index 2047 is
the excluded overflow slot and the window is anchored at 511.

That is a coherent account of *why nothing can drain*, and it needs no claim
about where work physically sits. What it does not yet explain is why the last
four updates do not retire and end the run, and the per-PE fields cannot be
used to find out. **The instrument that would answer it does not exist yet**:
there is no per-PE report of what this PE physically holds, by bucket, as
distinct from what it has created. Adding one is the first step of 7.6g.

## What would settle it

A reproducer and a real instrument, not another campaign.

The instrument first, because without it the next campaign produces the same
unreadable logs: `report_progress_state` must report what this PE *holds* --
`pq` and `pq_hold` contents summarised by bucket, and the lowest bucket in
which it holds anything -- separately from what it has *created*, which is what
`histogram[]` gives. The two are currently printed side by side with names that
invite exactly the confusion above.

The reproducer is harder than it first looked. `--bucket-width 1` on a 200x200
mesh puts every distance past the clamp but **does not reproduce the stall**:
it completes, with 0 coarsenings, because with all mass in the overflow slot
the band test refuses to merge (`COARSEN_BAND_NARROW`). The stalled runs had
`bucket_scale` at 3, 4, 10, 12 and 13, so they coarsened repeatedly *and then*
ran past the clamp. The fixture has to produce both, in that order, which the
single forced width does not.

Note what this does and does not disturb. It does not touch the width result:
the `weight` arm did not hang once in this campaign at any allocation, and
`mesh20`/`mesh22`, the two graphs the per-graph map names, are the two where
`weight` keeps the frontier inside the clamp and so never reach this state.
The mechanism is in fact an argument *for* the map. But a default that can stop
is a defect of the default, and it is item (1), not item (3).

## Why 1,080 earlier runs did not show it

They did not show it at all: the three prior width campaigns emit zero
`PROGRESS_STALL` lines between them. The source is identical -- no tracked file
changed between the two builds, `sssp_smp.cpp` last moved in `c07dc22` which
predates both, and htram last moved before that -- so this is not a regression
introduced by the range work. It is an intermittent failure that 1,080 runs
happened to miss and 253 happened to hit, which at five events in roughly
twenty-six eligible baseline runs is a rate high enough that missing it
twenty-six times running was luck.

The lesson for the campaign, not just the bug: every one of those 1,080 runs
was scored valid, and validity was doing more work than it looked like. A
campaign that reports only medians over valid runs cannot distinguish "this
configuration is fine" from "this configuration stops sometimes and we did not
draw one". The hang rate belongs in the table beside the speedup.
