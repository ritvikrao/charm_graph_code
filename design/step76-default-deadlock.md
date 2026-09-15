# The shipped default deadlocked: queue order, found and repaired

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

## The repair: guard the rescue on progress, not on emptiness

There is already a rescue for this shape of failure, added in 7.6a. When the
reduced window sums to zero the controller admits every bucket, on the
reasoning that whatever is left must be past the window's right edge where no
threshold computed inside the window can reach it:

```cpp
if (histogram_sum <= 0) {
  heap_threshold = tram_threshold = bfs_threshold = HISTO_BUCKET_COUNT - 1;
}
```

It does not fire in these stalls, and the global line says exactly why:

```
live=5973  window_sum=4  above_window=5969  clamped=5969  first_nonzero=511
```

`histogram_sum` is 4, not 0. **Four updates inside the window blocked a rescue
that 5,969 updates needed.** The situation the rescue exists for is present in
full -- work is stranded above the window, and no threshold the window can
express will reach it -- but the guard asks whether the window is *empty*
rather than whether it is *getting anywhere*.

Note also that `above_window` equals `clamped` exactly, in every stall. All
5,969 stranded updates are in the overflow slot, and none of them is a phantom:
the histogram's total is `created - processed` by construction, so every one of
the 5,973 is a real live update. This is not an accounting failure.

The repair changes the guard to progress. `stall_rounds` already counts
consecutive reductions in which no update was created or retired anywhere --
the comment on it already says "that is a stall, not convergence, and nothing
will restart it" -- so the second rescue fires on that count, with work above
the window outstanding:

```cpp
} else if (stall_rescue_rounds > 0 && stall_rounds >= stall_rescue_rounds &&
           above_window > 0 && live_updates > 0) {
```

`--stall-rescue N`, default 32, 0 disables. Deliberately far earlier than the
stall report's 256, and deliberately not 1: updates in flight are created but
not yet retired, so a run under heavy buffering can legitimately hold both
counters still for a few rounds.

### Why this is a repair and not a watchdog

The rule attached to item (1) is that a watchdog forcing an exit is not a
repair, and this does not force an exit. It changes admission so that stranded
work becomes reachable, and the run continues to a correct answer by the same
argument the empty-window rescue already rests on: with every bucket admitted,
every live update is below both thresholds, every PE can retire what it holds,
and `updates_processed` has to rise.

But it repairs the symptom, not the cause. Whatever left four admissible
updates unprocessed at their own threshold is untouched by it. So every firing
is made loud rather than quiet:

- `STALL_RESCUE` is printed with the full state that provoked it;
- `Stall rescues: N` is in every run's summary, including the zero, so the
  question "did this run need rescuing?" is answerable from any run;
- **`scripts/verify.sh` fails on `STALL_RESCUE`**, alongside `PROGRESS_STALL`,
  `CONSERVATION VIOLATED` and `COARSEN_CLAMPED`. The rescue is for production
  runs; a fixture that needs it is a regression, and without this the gate
  would go green on a deadlock the rescue happened to save.

Verified by forcing it: at `--stall-rescue 1` on the coarsening fixture it
fires twice and the run still passes against serial Dijkstra; at the default 32
it does not fire at all. The first version printed without a leading newline,
so the counter incremented while the gate's `^STALL_RESCUE` match found
nothing -- a rescue that reports itself into the middle of another line is a
rescue the gate cannot see, which is the failure mode this whole section is
about.

### What is still open

The drain failure itself. Four updates sat at their own admission threshold and
were not processed, and nothing in the report as it stood could say where they
were. That is what the new `pq_top_bucket` / `held_lowest` / `held_admissible`
fields are for, and jobs 22093288 (mesh22) and 22093373 (mesh20) run the
instrumented binary **without** the rescue, over the campaign's four sources at
eight processes of fifteen, so that the hang still happens and is fully
described when it does. The rescue must not be staged into those runs or they
measure nothing.

## The cause: process_heap() stops at a top that hides runnable work

*2026-09-15, on Anvil. Resolved; the repair is `--pq-overflow-last`, default on.*

The drain failure was not in the histogram, the thresholds or the transport. It
was the priority queue's order.

`process_heap()` pops from a heap ordered by distance and **stops at the first
top whose bucket is above the heap threshold**. That is correct only if bucket
never decreases along the heap's order. It can:

- At bucket scale 1, index 2047 is both the top real bucket and the overflow
  slot. `charge_new_update()` flags everything it sends there, including the
  in-range slice `[2047, 2048)` widths, so such an update keeps bucket 2047 for
  life however the scale later moves.
- While the frontier is near the top of the range, the threshold is 2047 and
  one of those flagged updates is admitted to a PE's heap.
- A coarsening by `k` divides the threshold to `floor(2047 / k)`. The flagged
  update stays on top of that heap -- its distance is smaller than anything
  created since -- at bucket 2047, above the threshold.
- Every update that PE is later handed with a slightly larger distance, charged
  to `floor(2047 / k)` and so admissible, goes into the heap *behind* it.
  `process_heap()` looks at the top, sees 2047, and stops.

The window then sits at `floor(2047 / scale)` with a handful of admissible
updates that nothing will ever pop, and everything else in the overflow slot
where no threshold the window can compute will reach. That is the recorded
state exactly, including the one detail nothing else explained: every stall in
22071815 pinned at `floor((bucket_limit - 1) / bucket_scale)`.

The repair sorts every flagged update after every unflagged one
(`ComparePairs`, under `--pq-overflow-last on`). Bucket is then monotone along
the heap again, and the `break` is correct. It changes nothing but the order in
which a PE pops what it already holds, and only where flagged and unflagged
updates share a heap.

### Evidence

**Instrument.** Every per-PE stall line now carries `pq_admissible` (queued
updates at or below the threshold, wherever they are in the heap) and
`pq_overflowed`, besides `pq_top_bucket`. A PE with `pq_top_bucket` above its
threshold and `pq_admissible > 0` is a PE whose heap hides runnable work.

**Minimized reproducer, one process.** A 300x300 mesh (`90000 0 1 <src> 2`) at
width 12 or 16 with `--two-tier-absolute 1600`, 4 PEs, `--stall-rescue 0`. Found
by sweeping mesh size, width, two-tier limit and source on one Anvil node; the
three configurations that stalled were then repeated 60 times per arm, 24 runs
at a time:

| configuration | stalled, off | stalled, on |
|---|---:|---:|
| width 16, source 12345 | 14 / 60 | 0 / 60 |
| width 12, source 777 | 15 / 60 | 0 / 60 |
| width 16, source 0 | 9 / 60 | 0 / 60 |

Every one of the 38 stalls pinned at `floor(2047 / scale)` -- 1023, 682, 511,
341, 292, 255, 227, 186, 170, 157, 146, 136, 127, 120, 113 across scales 2 to
18 -- with `window_sum` equal to the `pq_admissible` of PEs whose
`pq_top_bucket` was 2047. Every stall also failed verification against serial
Dijkstra; every run with the repair on passed.

**The recorded configuration.** mesh22, four test sources, eight processes of
fifteen on one node, `--bucket-width-rule logv --clamp-freeze on --stall-rescue
0 --timeout 45`, both arms interleaved per (source, rep), four one-node jobs
(20743825-28, binary `acic_g3` sha256 `e9e2265c...`, `scripts/repro_stall.sh`):

| `--pq-overflow-last` | runs | hung | digest matches reference |
|---|---:|---:|---:|
| off | 80 | **12** | 68 |
| on | 80 | **0** | **80** |

A 15% hang rate at the shipped default, on a second machine, in the same state
as Delta's. Where both arms finished the repair was not slower: faster in 49 of
68 pairs, median off/on 1.02x (no floor was measured in these jobs, so read
that as "no regression", not as a speedup). mesh20 did not hang in 80 runs of
either arm on Anvil: on this machine its early rounds never hold more than the
two-tier limit, so it crosses the clamp without ever coarsening, and the
mechanism needs a coarsening.

### The first explanation was wrong, and was tested before it was believed

The first candidate was scale skew between PEs: a receiver still at scale 1
charging an unflagged update from an already-coarsened creator to index 2047,
the one index the merge skips. It predicts the same pin. It is built as
`--skew-defer` with an unconditional counter, `skew_top_arrivals`, and **the
counter was 0 in all 160 mesh20/mesh22 runs** (jobs 20743700-03), including the
14 that hung, and in every single-process sweep. The flag stays, off, with the
counter as the receipt that the race is not happening.

### Two instrument bugs found on the way, both of which would have hidden this

1. **The new flags were not readonlies.** Both were first added as plain
   globals parsed in `Main`, which sets them only in process 0. At 8 x 15 the
   "on" arm was on for 15 PEs of 120; jobs 20743768-71 recorded 2 hangs in 21
   "on" runs, and the stall lines named PEs 78-95 with flagged updates on top
   and admissible ones behind -- impossible with the comparator active. They
   are now declared in `sssp_smp.ci`, and each PE's own copy is printed on its
   stall line. The skew counter was unaffected (it does not depend on the
   flag), which is why its zero stands.

2. **`scripts/verify.sh` could not see a stall.** Its checks were
   `echo "$out" | grep -q PATTERN` under `set -o pipefail`. A stalled fixture
   writes ~200 KB; `grep -q` exits at its first match, `echo` dies of SIGPIPE,
   and pipefail reports the pipeline as failed -- a match read as no match. On
   Anvil the pipeline form missed 7 of 7 `STALL_RESCUE` lines that a here-string
   caught, and the gate passed twice with the repair off. **So the claim above,
   that the gate fails on `STALL_RESCUE`, was not true for any fixture whose
   output exceeded a pipe buffer** -- which is the case the check exists for.
   Every output test is now `grep -q ... <<< "$out"`. The earlier fixtures still
   pass under the corrected checks.

### The gate fixture, and how strong it is

The reproducer is intermittent, so the fixture is repeated and sized from a
measurement: 16 runs of source 12345 and 4 of source 777, both with the rescue
at its default, so a regression fails in 32 rounds. Run one at a time those
needed a rescue in 6-11 of 20 and 3-4 of 20 runs with the repair off, which puts
the chance the gate misses a regression near 0.2%. With the corrected checks,
`SSSP_EXTRA_ARGS="--pq-overflow-last off" scripts/verify.sh` fails 9 fixture
runs of 20; with the default it passes.

### What this closes

Item (1) is closed for the stall that reopened it: the cause is identified, the
repair is a change of admission order and not a watchdog, the rescue does not
fire in the gate, and the 15% hang rate at the recorded configuration goes to 0
of 80. The progress-guarded rescue stays as a production guard, loud and
counted. What remains open is only what was never closed: other graphs, more
nodes, other scales -- which is what 7.6h's hang column in every campaign table
now watches for.
