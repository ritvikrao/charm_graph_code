# Step 7.6b — controller robustness

*In progress. The knobs are built and gated; the measurements below are a
one-node pilot at 2^14–2^16 and are not the experiment. The Delta campaign is
`benchmarks/run.py --mode controller`.*

Item 2 of [step 7.5's next work](step75-comparisons.md#next-work-and-stopping-rules)
asks for one bounded experiment: vary the initial width, the PE-dependent
two-tier eligibility rule, and the treatment of clamping, one at a time, with
process geometry frozen; record why each round can or cannot coarsen; compare
time *and* delivered work against current defaults and a strong fixed policy.
This note holds the knobs that makes each of those a flag rather than a rebuild,
and what the pilot already says about where to look.

## The knobs

| Flag | What it isolates |
|---|---|
| `--bucket-width <w>` | the initial width, already a flag since 7.3. File mode derives log2(V), the generated mesh mode sqrt(V); neither reads anything about the distances the graph produces |
| `--two-tier-per-pe <n>`, `--two-tier-absolute <n>` | the eligibility rule. A round holding at most `N * 100` updates in its reduced window, with N the total PE count, abandons both percentiles for 0.9999 and skips coarsening. `--two-tier-absolute` pins the count instead, so the rule can be separated from the PE count |
| `--coarsen-clamped block\|allow\|strict` | the clamp guard. `block` is shipped: refuse to merge while the reduced clamp count is positive. It reads that count as of the previous contribution, and a chare keeps creating updates between contributing and receiving the broadcast that carries the merge. `strict` closes that gap by refusing for the rest of the run once the count has ever been positive. `allow` removes the guard |
| `--window-follow off\|on` | whether the reduced window may move to a bucket it cannot see. Off, a frontier that jumps past the window's right edge leaves it behind for good and every round from then on admits everything. On, an empty window with work outstanding slides up one width |

Every one of them verifies identically on and off: 18 configurations plus 3 on
`sssp_smp_diag` and the 6 progress fixtures, under each of the default,
`--window-follow on`, `--coarsen-clamped strict`, `--two-tier-absolute 0` and
`--two-tier-per-pe 10000`. `--coarsen-clamped allow` is excluded on purpose and
[step76-progress.md](step76-progress.md) says why.

Every run's `--diag` round series now carries `coarsen_reason` and `coarsen_k`,
so "why could this round not merge" is a column rather than an inference.

## What the pilot already says

**The two-tier rule, not the band test, is what stops the mesh coarsening — and
it is the rule that scales with the PE count.** A census of `coarsen_reason`
over whole runs on one node, 4 workers, at the gate's own sizes:

| Graph | rounds | merged | two-tier | band < 2 x target |
|---|---:|---:|---:|---:|
| mesh 10,000 | 281 | 2 | **167** | 111 |
| RMAT 16,384 / 262,144 | 54 | 1 | 7 | **45** |
| uniform 10,000 / 160,000 | 56 | 1 | 6 | **48** |

On the mesh 59% of rounds never reach the band test at all: they hold at most
`N * 100` updates and are declared too small to describe a distribution. On the
random graphs the population is large enough and the band is simply too narrow
to be worth merging.

This predicts something already in the 7.5 record. That note's mesh22 counter
table shows coarsening happening twice at one and two nodes and **not at all at
four, eight and sixteen** — and the two-tier limit there is `N * 100` with N the
*total* PE count, so it rises 1,600, 3,200, 6,400, 12,800, 25,600 across those
five allocations while the graph does not change. More of every run falls into
the branch that skips coarsening, for no reason connected to the graph.
`--two-tier-absolute 1600` is the direct test, and it is in the campaign.

**It is a prediction, not a result.** Coarsening disappearing and the two-tier
limit growing are consistent, and the pilot is at 2^14 on one node.

**Window-follow buys control and costs time, at least where it fires.** On the
16K RMAT with `--bucket-width 3`, where the frontier leaves the 256-bucket
window almost immediately:

| | rounds | rounds with the frontier outside the window | compute |
|---|---:|---:|---:|
| `--window-follow off` | 15 | **13** | 0.0141 s |
| `--window-follow on` | 638 | 1 | 0.0225 s |

Following the frontier takes the controller from steering in 2 rounds of 15 to
steering in 637 of 638 -- and makes the run **1.60x slower**. That is the
step 7.5 warning in the other direction: more control is not automatically less
time, any more than fewer rounds is automatically less work. At the widths
these graphs actually run with, the frontier never leaves the window and the
flag is inert, so what is being priced here is a repair for a regime the tuned
widths avoid.

## Wave 1: one node, 16 workers

Jobs 22038072 (mesh20), 22038073 (mesh22) and 22038074 (rmat22, road-ny,
youtube). Eight held-out sources, two repetitions, all 720 test queries valid.
`control` is current defaults run a second time and lands at 0.98-1.02x, so
**this allocation resolves about 3%** and nothing inside that band is a result.
Speedups are current/variant, so above 1 is faster than current defaults.

<!-- BEGIN CONTROLLER_1N -->
| Graph | Variant | Solve | vs current | Updates noted | vs current | Rounds | vs current |
|---|---|---:|---:|---:|---:|---:|---:|
| mesh20 | control | 0.3151 s | 0.98x | 5.15 M | 0.99x | 3,607 | 1.03x |
| mesh20 | **width-1024** | 0.2722 s | **1.13x** | 5.79 M | 1.11x | 2,827 | 0.81x |
| mesh20 | tuned-fixed | 0.2842 s | 1.09x | 5.68 M | 1.09x | 3,086 | 0.88x |
| mesh20 | width-217 | 0.2916 s | 1.06x | 5.24 M | 1.01x | 3,241 | 0.93x |
| mesh20 | clamp-strict | 0.3068 s | 1.01x | 5.14 M | 0.99x | 3,678 | 1.05x |
| mesh20 | two-tier-absolute-1600 | 0.3038 s | 1.02x | 5.15 M | 0.99x | 3,615 | 1.04x |
| mesh20 | window-follow | 0.3028 s | 1.02x | 5.16 M | 0.99x | 3,570 | 1.02x |
| mesh20 | two-tier-never | 0.4893 s | **0.63x** | 5.35 M | 1.03x | 6,714 | 1.93x |
| mesh22 | control | 0.9702 s | 1.00x | 18.23 M | 1.01x | 7,692 | 0.98x |
| mesh22 | tuned-fixed | 0.8828 s | 1.09x | 20.73 M | 1.14x | 5,744 | 0.73x |
| mesh22 | **width-1024** | 0.8930 s | **1.08x** | 20.81 M | 1.15x | 5,433 | 0.69x |
| mesh22 | width-495 | 0.9499 s | 1.02x | 18.83 M | 1.04x | 6,615 | 0.84x |
| mesh22 | clamp-strict | 0.9852 s | 0.98x | 18.26 M | 1.01x | 7,630 | 0.97x |
| mesh22 | two-tier-absolute-1600 | 0.9567 s | 1.01x | 18.22 M | 1.01x | 7,741 | 0.99x |
| mesh22 | window-follow | 0.9836 s | 0.98x | 21.33 M | 1.18x | 6,659 | 0.85x |
| mesh22 | two-tier-never | 1.1226 s | **0.86x** | 35.82 M | 1.98x | 8,364 | 1.07x |
| road-ny | control | 0.1854 s | 1.01x | 11.55 M | 0.98x | 450 | 1.01x |
| road-ny | **width-1074** | 0.1091 s | **1.72x** | 1.66 M | **0.14x** | 1,247 | 2.81x |
| road-ny | tuned-fixed | 0.1196 s | 1.57x | 2.45 M | 0.21x | 1,326 | 2.99x |
| road-ny | width-65536 | 0.1316 s | 1.43x | 2.59 M | 0.22x | 1,159 | 2.61x |
| road-ny | clamp-strict | 0.1824 s | 1.03x | 11.72 M | 1.00x | 450 | 1.01x |
| road-ny | two-tier-absolute-1600 | 0.1804 s | 1.04x | 11.57 M | 0.98x | 452 | 1.02x |
| road-ny | window-follow | 0.1841 s | 1.02x | 11.77 M | 1.00x | 434 | 0.98x |
| road-ny | two-tier-never | 0.2797 s | **0.67x** | 0.74 M | **0.06x** | 3,973 | 8.94x |
| rmat22 | every variant | 1.047-1.067 s | 0.99-1.01x | 132-133 M | 1.00x | 93-397 | 0.57-2.46x |
| youtube | two-tier-never | 0.1598 s | 1.11x | 6.66 M | 0.86x | 373 | 1.36x |
| youtube | everything else | 0.173-0.187 s | 0.94-1.03x | 7.3-8.1 M | 0.94-1.04x | 128-680 | 0.47-2.47x |
<!-- END CONTROLLER_1N -->

### The initial width is the defect, and it is worth more than everything else

**File mode derives the width as log2(V) and that is wrong for both the mesh and
the road graph.** A width of 1024 -- which is what the *generated* mesh mode
would have used, sqrt(V) -- is 1.13x faster on mesh20 and 1.08x on mesh22. On
road-ny, whose real distance weights reach 36,946, log2(264,346) is about 18, so
a single edge can span two thousand buckets: the whole histogram. Widening it to
1,074 is **1.72x faster**.

Two things follow that matter beyond this table. First, **width alone beats the
tuned fixed policy on road-ny** -- 1.72x against 1.57x -- so current's adaptive
machinery with a sane starting width is better than the best fixed policy the
7.5 search found. That is the first evidence in this project for adaptivity that
does not depend on the mesh. Second, every file-mode mesh and road number in the
7.5 comparison tables was taken at a width that costs ACIC 8% to 42%, which is
not nothing next to a RIKEN/ACIC ratio of 0.56 on road-ny.

### Time, work and rounds disagree, in both directions

The step 7.5 note warns against conflating them. The same knob does it twice,
opposite ways:

* On mesh20 the width win comes with **11% more work and 19% fewer rounds**.
* On road-ny the width win comes with **86% less work and 2.8x more rounds**.

And `two-tier-never` on road-ny does **94% less work than current** -- 0.74 M
updates noted against 11.75 M -- while being **1.49x slower**, because it takes
8.9x the rounds. A variant that removes fifteen sixteenths of the delivered work
and still loses is as clean a refutation of work-as-objective as this project
has produced.

### The two-tier branch is load-bearing, and not uniformly

Switching it off costs 1.59x on mesh20, 1.16x on mesh22 and 1.49x on road-ny --
but **gains 1.11x on youtube**, with 14% less work. It is doing real work on
high-diameter graphs and getting in the way on the social graph. Its
PE-dependence is untested here by construction: at 16 PEs `N * 100` is exactly
1,600, so `two-tier-absolute-1600` is current defaults by another name and reads
1.02x, 1.01x, 1.04x as it should. The two only diverge at two nodes and above,
which is what the rest of the campaign is for.

### The stall repair is free

`clamp-strict` -- refuse to merge buckets for the rest of the run once anything
has been charged to the clamp bucket -- is 1.01x, 0.98x, 1.01x, 1.03x and 1.01x
across the five graphs, with delivered work within 1% everywhere. **Every one of
those is inside the control band.** The conservative repair for the loss of
progress in [step76-progress.md](step76-progress.md) costs nothing measurable at
one node, which is the cheapest correctness fix available. Whether it costs
anything at four nodes and above is open, though the 7.5 counter table reports
no coarsening at all there, so there is less to give up rather than more.

### Window-follow does nothing here

0.98x to 1.03x on every graph, because at these widths the frontier never leaves
the window -- `Rounds with the frontier outside the window` is zero. It moves
work and rounds on mesh22 (18% more work, 15% fewer rounds) without moving time.
It is a repair for a regime that a sane width avoids, and the pilot above prices
it at 1.60x slower in the regime where it does fire.

## The campaign

`--mode controller` freezes one process per node and 16 workers, and runs ten
variants that each differ from current defaults by one flag, plus `control` --
current defaults run a second time, which is what the allocation can resolve --
and the `tuned-fixed` policy frozen by the matching 7.5 selection job. Eight
held-out sources, two randomized repetitions, and one untimed diagnostic query
per variant that writes the round series.

```
sbatch -N 1 benchmarks/compare.sbatch CAMPAIGN --mode controller \
    --selection-job 22032688 --graphs mesh20
sbatch -N 2 benchmarks/compare.sbatch CAMPAIGN --mode controller \
    --selection-job 22032689 --graphs mesh22
# controls, same allocations:
#   --graphs rmat22,road-ny,youtube
# then 4, 8 and 16 nodes on mesh22 with selection jobs
#   22032758, 22032759, 22032760
```

One and two nodes go first. The four-, eight- and sixteen-node allocations are
where the two-tier prediction is actually testable, and they are worth their
node hours only if the knobs move anything at all at the bottom of the range.

They do, so they were submitted: jobs 22042109, 22042110 and 22042111 on
`mesh20,mesh22,rmat22`. Only mesh22 and rmat22 have frozen selections at those
node counts, so mesh20 runs there without a `tuned-fixed` arm. mesh20 is
included anyway, for the reason the next section gives.

## Wave 2: two nodes, 32 workers

Three jobs, 720 timed runs, all valid: 22038075 (mesh20), 22038076 (mesh22),
22038077 (rmat22, road-ny, youtube). Same shape as wave 1 -- eight held-out
sources, two repetitions, randomized order, one process per node.

Speedup is current/variant, so above 1 is faster. `noted` and `rounds` are
medians relative to `current` on the same graph.

| variant | mesh20 | mesh22 | rmat22 | road-ny | youtube |
|---|---|---|---|---|---|
| control | 0.92x | 0.98x | 1.01x | 1.04x | 1.03x |
| tuned-fixed | 1.23x | 1.06x | 0.91x | 1.59x | 1.00x |
| width (small) | 1.28x | 1.01x | 0.93x | 1.86x | 1.07x |
| width-1024 | 1.27x | 1.11x | 0.98x | 1.53x | 1.04x |
| two-tier-absolute-1600 | **1.30x** | 0.94x | 0.97x | 1.07x | 1.07x |
| two-tier-never | 0.62x | 0.76x | 0.96x | 0.62x | 1.11x |
| clamp-strict | 1.04x | 1.03x | 0.97x | 1.06x | 1.03x |
| window-follow | 0.94x | 0.97x | 0.95x | 1.05x | 0.99x |

The small-width arm is width-217 on mesh20, width-495 on mesh22, width-3 on
rmat22, width-1074 on road-ny and width-8 on youtube; width-65536 is road-ny's
second candidate and reads 1.53x.

Two things carry over from wave 1 and got stronger. Width is still the largest
single effect, and on road-ny it is now **1.86x** against `tuned-fixed`'s 1.59x
-- a derived width beating the tuned fixed policy by a wider margin at two nodes
than at one. And `clamp-strict` is still free: 0.97x to 1.06x, delivered work
within 1% on all five graphs. Both of the defaults questions this campaign was
meant to answer now have the same answer at one node and at two.

But the number that matters in this table is `control` on mesh20, at 0.92x. At
one node `control` held 0.98x to 1.02x. A control arm that misses its own
baseline by 8% means the median is summarizing two different things, and the
next section breaks it apart per source.

### mesh20 at two nodes: defaults do 8-13x the work of any pinned rule

Pooling the four arms that are current defaults for this purpose -- `current`,
`control`, `clamp-strict`, `window-follow` -- against the arms that pin the
admission rule, per source (millions of updates noted, min-max over 8 runs):

| source | defaults | spread | pinned rules | spread |
|---|---|---|---|---|
| 8720 | 50-62 | 1.2x | 5-8 | 1.4x |
| 188488 | 8-78 | **9.7x** | 6-9 | 1.5x |
| 650560 | 6-7 | 1.2x | 5-7 | 1.2x |
| 736504 | 6-63 | **9.7x** | 5-7 | 1.4x |
| 742372 | 76-80 | 1.1x | 6-9 | 1.6x |
| 841388 | 7-55 | **7.8x** | 6-7 | 1.3x |
| 933220 | 7-60 | **8.5x** | 5-7 | 1.3x |
| 1015667 | 76-79 | 1.0x | 6-9 | 1.6x |

Two separate things are going on, and the pooled median hid both.

**First, it is mostly not random.** Three sources (8720, 742372, 1015667) take
the expensive path on every single run, with a spread of 1.0x-1.2x -- as
reproducible as anything in this campaign. One source (650560) never takes it.
The remaining four are genuinely bistable: the same source, in the same
allocation, delivers either ~7M or ~55-78M, 7.8x to 9.7x apart, with nothing in
between.

**Second, the pinned rules do not care.** `two-tier-absolute-1600` delivers
5.3M-5.9M on all eight sources; `width-217`, `width-1024` and `tuned-fixed` sit
between 6M and 9M on all eight. The defaults' cheap regime is that same 6-8M.
So defaults sometimes behave exactly like a pinned rule and otherwise do an
order of magnitude more work, and which one depends partly on the source and
partly on the run.

The nondeterministic half is not machine weather. The run order is randomized
across variants, so the regimes interleave with the pinned variants in the same
minutes:

```
idx  variant                 noted   sec  rounds
 84  width-217                 7.2 0.307   2276
 87  control                  50.1 0.554    683   <== defaults
 89  two-tier-absolute-1600    5.3 0.397   2610
 90  clamp-strict             57.4 0.560    678   <== defaults
 93  two-tier-absolute-1600    5.7 0.286   2408
 94  control                  75.9 0.486    995   <== defaults
 95  width-1024                9.4 0.344   1986
 97  width-217                 8.6 0.275   2063
 98  current                  78.8 0.477    948   <== defaults
```

Within twelve consecutive launches on the same two nodes, the pinned variants
never flip and the defaults flip four times. Whatever selects the regime is
inside the controller, not in the allocation.

**The expensive regime runs fewer rounds, not more** -- about 950 against about
2,400. Ten times the delivered work in a third of the rounds is coarse
admission, each round admitting far too much, and not extra iteration.

**It is specific to mesh20 at two nodes.** The same per-source breakdown over
every other graph and both node counts gives a worst-case spread of 1.0x to
1.4x:

| | mesh20 | mesh22 | rmat22 | road-ny | youtube |
|---|---|---|---|---|---|
| 1 node | 1.1x | 1.1x | 1.0x | 1.2x | 1.4x |
| 2 nodes | **9.7x** | 1.1x | 1.1x | 1.2x | 1.3x |

And at one node mesh20's defaults deliver 5.2M-5.7M -- the cheap regime, on
every source, every run, indistinguishable from what the pinned rules get at two
nodes. So this is not a graph that is simply hard to schedule: the same defaults
on the same graph are well-behaved at 16 PEs and lose an order of magnitude at
32.

This is a third way the controller's window stops describing reality, distinct
from both of the mechanisms in [step76-progress.md](step76-progress.md): the run
converges, every result validates, and nothing warns. It only spends an order of
magnitude more work than it needs to.

### Diagnosed: the two-tier rule and the clamp guard race, and PEs decide it

Job 22042108 ran current defaults on mesh20 twenty-four times at two nodes with
a round series each, on source 742372 -- one of the three that wave 2 found
expensive on every run. It came out 23 expensive and **one cheap**, which is the
comparison the campaign could not get: two traces of the same source in the same
allocation, differing only in regime. Even a source that looked deterministic is
bistable; it is just heavily biased.

The traces disagree in exactly one place, and it is not where I guessed.

| | cheap (rep03) | expensive (rep00, rep05, rep19) |
|---|---|---|
| final `bucket_scale` | 92 | **1** |
| merges | 3 (rounds 72, 96, 1769) | **none** |
| `coarsen_reason` census | 661 two-tier, 3 merged, 1355 band-narrow | 391 two-tier, **536 clamp-live** |
| left the two-tier branch at round | 72 | 237 |
| first `COARSEN_CLAMP_LIVE` | never | **237** |
| `window_first` reached | 914 | **2047** |

**The expensive regime never coarsens at all.** My hypothesis was the opposite --
an extra coarsening step making buckets wider than the largest edge weight -- and
it is wrong. `bucket_scale` stays at 1 for the whole run, the window walks out to
the clamp bucket, and 80M updates are delivered through a histogram that cannot
describe them.

The mechanism is a race between two guards, and the numbers name it:

1. While `histogram_sum <= N * 100`, the two-tier branch abandons both
   percentiles and `choose_coarsening` returns `COARSEN_TWO_TIER`. **Nothing can
   merge.** At two nodes N is 32, so the limit is 3,200.
2. A bucket index clamps at `2048 * bucket_scale * width`. mesh20 in file mode
   derives width 20, so at scale 1 **any distance at or above 40,960 is charged
   to bucket 2047** -- against a graph whose maximum distance is 246,154.
3. Once anything is live in the clamp bucket, `choose_coarsening` returns
   `COARSEN_CLAMP_LIVE` and refuses to merge, because merging over a non-empty
   clamp bucket strands live counts at `2047 / k`. That guard is the correct
   one; [step76-progress.md](step76-progress.md) shows what `--coarsen-clamped
   allow` does without it.

So the run must leave the two-tier branch *before* the frontier reaches distance
40,960, or it can never coarsen again. The cheap run left at round 72 and merged
**on that same round**, k = 23, which moved the clamp to `2048 * 23 * 20` =
942,080 -- past the whole graph, so it never clamped again. The three expensive
runs left at rounds 237, 243 and 239 and found `CLAMP_LIVE` already set **on that
exact round**. They never merged.

This is not a third independent hazard. It is the cost of hazard 2's guard, paid
when a scheduling race is lost, and **the PE-dependent two-tier rule is what
loses it**: the limit is `N * 100` over the total PE count, so more PEs means a
higher bar, a longer stay in the branch that cannot merge, and more time for the
frontier to pass 40,960. At 16 PEs the limit is 1,600 and the crossing comes
early enough that one node never shows this at all.

**That also explains something the 7.5 campaign recorded and could not account
for**: its mesh22 table shows coarsening happening at one and two nodes and
never at four, eight or sixteen. The limit there is 6,400, 12,800 and 25,600.
The prediction the 7.5 note filed as worth testing is now a mechanism, and it
says the defect gets monotonically worse with scale -- which is what jobs
22042109, 22042110 and 22042111 are in the queue to check.

It also explains why every pinned variant is immune, each for its own reason.
`two-tier-absolute-1600` restores the 16-PE bar at 32 PEs, so the crossing comes
early and the merge lands before the clamp -- which is why it wins on 8 of 8
mesh20 sources rather than on average. A fixed width large enough puts the
graph's whole distance range inside 2048 buckets, so nothing clamps and no
coarsening is needed.

### Window-follow is not inert -- it is a rare hazard

Wave 1 priced `window-follow` at 0.98x-1.03x and found it never fired. At two
nodes on mesh22 it fired in exactly three of 153 runs, sliding seven windows
each. Those three runs are exactly the three that blew up:

```
runs that slid:   total 1.552s   1.107s   1.054s
work delivered:   158.2M   101.0M    65.4M      (median for this variant: 20M)
```

The other 150 runs report `Windows slid past an empty reduced window: 0` and sit
at 19-22M with everything else. So the correlation is 3 for 3: sliding the
window forward when work is recorded above it never helped and cost 3x to 8x the
work every time it happened. `two-tier-never` shows a milder version of the same
shape on mesh22 (6.5x spread, 17M to 112M).

This is a clean negative result for the second repair proposed for the window
problem, and it argues for leaving `--window-follow` off, which is the default.
The first repair -- the one shipped in 7.6a -- stands unaffected.

### What the two-tier knob now says

Two nodes is the first allocation where `two-tier-absolute-1600` is not current
defaults by another name: at 32 PEs the `N * 100` rule gives 3,200 against the
pinned 1,600. It reads 1.30x on mesh20, 1.07x on road-ny and youtube, 0.97x on
rmat22, and 0.94x on mesh22 -- so it is not uniformly better across graphs, and
mesh22 is a real if small regression.

On mesh20 it is uniformly better across *sources*, which the median understates.
Per source, against pooled defaults:

| source | defaults | pinned | speedup |
|---|---|---|---|
| 8720 | 0.557s, 55.9M, 688 rounds | 0.385s, 5.3M, 2621 | 1.45x |
| 188488 | 0.392s, 40.7M, 1469 | 0.291s, 5.7M, 2397 | 1.35x |
| 650560 | 0.288s, 6.6M, 1833 | 0.260s, 5.5M, 1924 | 1.11x |
| 736504 | 0.366s, 6.7M, 2280 | 0.301s, 5.5M, 2214 | 1.21x |
| 742372 | 0.485s, 78.6M, 975 | 0.310s, 5.8M, 2361 | 1.56x |
| 841388 | 0.356s, 7.2M, 2263 | 0.340s, 5.6M, 2312 | 1.05x |
| 933220 | 0.401s, 31.7M, 1446 | 0.341s, 5.5M, 2498 | 1.18x |
| 1015667 | 0.609s, 77.9M, 666 | 0.378s, 5.9M, 2776 | 1.61x |

Eight sources out of eight, 1.05x to 1.61x, and the gain tracks the size of the
blowup it prevents. But it is not only the blowup: on 650560, 736504 and 841388
the defaults never enter the expensive regime and the knob still wins 1.11x,
1.21x and 1.05x. So the pinned threshold is worth something on its own, and
removing the 10x path is worth more on top.

That does not make it a default yet. mesh22 at 0.94x is the counterexample, and
the whole point of the PE-dependent rule is that its limit scales with the
allocation -- 1,600 is right for 16 PEs by construction and has no claim on 512.
Jobs 22042109/10/11 at 4, 8 and 16 nodes are where a fixed 1,600 either keeps
winning or falls apart.

Turning the branch off entirely remains bad -- 0.62x on mesh20 and road-ny --
and `two-tier-never` on road-ny is still this project's cleanest refutation of
work as an objective: it removes fifteen-sixteenths of the delivered work and is
half the speed.

## Wave 3: four nodes, 64 workers — the prediction holds, and sharpens

Job 22042109, `mesh20,mesh22,rmat22`, 432 timed runs, all valid. mesh20 has no
frozen 7.5 selection at this node count, so it runs without a `tuned-fixed`
arm.

| variant | mesh20 | mesh22 | rmat22 |
|---|---|---|---|
| control | 1.01x | 0.99x | 0.98x |
| tuned-fixed | — | 1.49x | 0.99x |
| width (small) | 1.59x | 1.59x | 0.94x |
| width-1024 | 1.51x | **1.61x** | 1.00x |
| two-tier-absolute-1600 | 1.53x | 1.57x | 0.99x |
| two-tier-never | 0.70x | 0.96x | 1.02x |
| clamp-strict | 1.01x | 1.00x | 1.01x |
| window-follow | 1.04x | 0.97x | 0.99x |

The race model predicted the defect worsens monotonically with PE count, and
every part of that shows up.

**mesh22 has crossed over.** At two nodes it was clean -- per-source spread
1.1x, and `two-tier-absolute-1600` was a 0.94x *regression*, the counterexample
that blocked any default change. At four nodes its bar doubles to 6,400 and it
now loses the race on every run: `bucket_scale` never leaves 1, 416 rounds
report `COARSEN_CLAMP_LIVE`, `window_first` reaches 2047, and the same knob is
now worth **1.57x**. Nothing about the graph changed.

**The bistability is gone on mesh20, in the bad direction.** Defaults no longer
flip: all 64 runs land between 101M and 169M, spread 1.7x. At 64 PEs the bar is
6,400 and the race is simply lost every time.

**rmat22 is the control, and it stays clean.** 1.00x everywhere, and its
diagnostic trace shows `current` coarsening normally -- `bucket_scale` 7, one
merge, no clamp round at all. The mechanism is about distance range, not about
the knob.

### Fixing the width beats fixing the bar, and the traces say why

The bistability did not disappear; it **moved**. On mesh20 at four nodes:

```
two-tier-absolute-1600   5 5 5 5 5 5 5 5 6 6 6 6 6 6 103 107    <- 2 of 16 lost
width-217               10 11 11 12 12 13 13 13 13 13 15 15 16 16 16 16
width-1024              11 11 12 12 13 13 13 13 13 14 15 16 16 16 17 24
```

`two-tier-absolute-1600` now loses the race twice in sixteen. The width arms
never lose it at all, and the reason is structural rather than statistical:

* Pinning the bar makes the race **easier to win**. The clamp still sits at
  `2048 * width` = about 28,400 with the derived width, and the frontier still
  runs at it.
* Setting the width large enough makes the clamp **unreachable**. At width 217
  it is 444,416, past mesh20's largest distance of 246,154, so nothing is ever
  charged to bucket 2047 and coarsening is never blocked.

So the two fixes are not interchangeable, and only one of them removes the
failure mode rather than reducing its probability.

### One rule explains every width result in the campaign

File mode derives the bucket width as `log(V)` -- the natural log of the
**vertex count**, which has nothing to do with the distances being bucketed.
The clamp sits at `2048 * width`. So a graph is safe exactly when
`log(V) > max_distance / 2048`:

| graph | V | max distance | `log(V)` | needs width > | short by | width speedup measured |
|---|---:|---:|---:|---:|---:|---:|
| road-ny | 264,346 | 1,290,145 | 12.49 | 630.0 | **50.5x** | **1.86x** |
| mesh22 | 4,194,304 | 506,463 | 15.25 | 247.3 | 16.2x | 1.61x |
| mesh20 | 1,048,576 | 246,154 | 13.86 | 120.2 | 8.7x | 1.59x |
| youtube | 1,134,890 | 7,285 | 13.94 | 3.6 | safe | 1.07x |
| rmat22 | 4,194,304 | 3,161 | 15.25 | 1.5 | safe | 1.00x |

**The shortfall ranks the measured speedups in order.** The two graphs whose
derived width already clears the bar are exactly the two where width does
nothing, and road-ny -- fifty times short, the worst in the table -- is where
width is worth the most in the whole campaign. This is one mechanism, not five
graph-specific observations, and it was the largest single effect in 7.6b
before there was any account of why.

### The rule out of sample

Applied to all nine graphs the campaign holds, the rule partitions them
cleanly, and the three it calls unsafe are exactly the three where width was
worth something:

```
CLAMPS: mesh20 (8.7x short), mesh22 (16.2x), road-ny (50.5x)
SAFE:   rmat20, rmat20-s2, rmat22, uniform20, uniform20-s2, youtube
```

The four graphs not used in 7.6b -- rmat20, rmat20-s2, uniform20, uniform20-s2
-- are all predicted safe, with maximum distances between 1,234 and 3,446
against a `log(V)` of 13.86. Their widths were never varied here, so this is a
prediction rather than a result.

The 7.5 fixed-policy search tuned bucket width independently, and its frozen
choices are weak, one-sided support. road-ny selected **65536**, by a wide
margin the largest width anywhere in that search, which is also the graph this
rule says is the most badly served by `log(V)`. mesh20 and mesh22 selected
1024. But those choices are not stable across allocations -- rmat22 selected 3
at 120 workers, nothing at 16, and 1024 at 64 -- so the search is selecting
noise wherever the width does not matter, and its agreement cannot be leaned on
where it does. The direct 7.6b width arms are the real test, and they are what
the table above reports.

### What the fix cannot be

The obvious repair -- derive the width from the distance range instead of from
V -- needs a bound on the range before the solve starts. `begin()` already
broadcasts one: `max_sum`, the sum of every vertex's maximum out-edge. It is
far too loose to use directly. On mesh20 it is order 8e8, so `max_sum / 2048`
would be a width near 400,000 and the graph's entire distance range would fall
in bucket 0 -- no resolution at all, which is the opposite failure.

The sharper observation is that **the controller already has the mechanism for
this and the guard disables it.** Coarsening exists precisely to widen buckets
when the range outgrows the histogram, and `COARSEN_CLAMP_LIVE` switches it off
at the moment it is most needed. The guard is correct as written: bucket 2047
means "at or above `2048 * scale * width`" rather than an index, so merging
sends it to `2047 / k` and strands counts that no retirement will remove.

Making coarsening safe with a live clamp bucket is therefore the repair that
addresses the mechanism rather than the symptom, and it cannot be done by
rewriting the merge -- an update charged to 2047 before the merge retires to
`d / (width * k)` after it, so increment and decrement land in different
buckets whatever the merge does. It needs the live updates re-binned at the new
scale rather than the counts merged. That is O(live updates) at each of about
three coarsenings per run, which is affordable, but it is a real change to the
histogram's maintenance and not a one-line guard fix.

**Re-binning is not available.** The histogram is incremented on the PE that
*creates* an update (`sssp_smp.cpp:2224`, `2470`) and decremented on the PE that
*retires* it (`2315`, `2359`, `2552`, `2558`, `2594`, `2631`). It is only
meaningful as a global sum, and no PE owns the live updates counted in its own
array: an update created on PE A and retired on PE B is A's increment and B's
decrement, and while it is in a TRAM buffer or on the wire it sits in no
enumerable structure at all. A PE cannot walk "its" live updates because there
is no such set. Re-binning would need global quiescence at every coarsening,
which is the thing the controller runs concurrently with.

What the identity actually rests on is worth stating, because it says where the
repair has to go. For non-clamped buckets the merge is exact:
`floor(floor(x/s)/k) == floor(x/(s*k))`, so a count merged from bucket `i` to
`i/k` lands exactly where the later retirement decrement will look, whichever
PE performs it. The clamp bucket breaks the identity for one reason only --
`coarsen_buckets` raises `bucket_limit` to `HISTO_BUCKET_COUNT * bucket_scale`
(line 2454), so an update clamped at creation is *not* clamped at retirement and
its decrement lands in an ordinary bucket.

That points at a repair the guard does not need:

1. **Freeze the clamp threshold.** If `bucket_limit` stayed at
   `HISTO_BUCKET_COUNT * width` rather than scaling, an update clamped at
   creation stays clamped at retirement, increment and decrement both land on
   2047, the clamp bucket never has to be merged, and `COARSEN_CLAMP_LIVE` can
   be deleted. Conservation holds for every bucket. **But coarsening then no
   longer extends the representable range**, and on mesh20 the clamp would sit
   at about 28,400 against distances to 246,154 -- the controller would be
   blind for most of the run. Correct, cheap, and insufficient alone.
2. **Seed the width from a range estimate**, keeping `log(V)` only as a floor.
   This is the load-bearing one: with an adequate initial width the clamp is
   never reached, which is what the width arms demonstrate. `max_sum` is
   already broadcast and far too loose; what a cheap and tight enough estimate
   looks like is the open question.
3. **Keep `log(V)` but raise `HISTO_BUCKET_COUNT`.** Cheapest, and strictly a
   postponement: it multiplies the clamp threshold without changing the shape
   of the failure.

(1) and (2) compose: freezing the threshold makes coarsening safe and lets the
guard go, and a sound initial width makes the frozen threshold sufficient.
Neither is implemented or measured here.

Item 2's stopping rule is one bounded experiment, and this was it. Which repair
to build is a decision for item 4, where the width question stops being a knob
and becomes the claim.

## Wave 4: eight nodes, 128 workers — the two fixes separate

Job 22042110, `mesh20,mesh22,rmat22`, 432 timed runs, all valid.

| variant | mesh20 | mesh22 | rmat22 |
|---|---|---|---|
| control | 1.01x | 0.96x | 1.01x |
| tuned-fixed | — | 1.29x | 0.99x |
| width (small) | **1.70x** | 1.41x | 0.93x |
| width-1024 | 1.59x | 1.38x | 0.98x |
| two-tier-absolute-1600 | 1.14x | 1.37x | 0.96x |
| two-tier-never | 0.55x | 0.72x | 1.01x |
| clamp-strict | 1.01x | 0.99x | 0.91x |
| window-follow | 0.99x | 0.94x | 0.96x |

The defect keeps growing with PE count exactly as the race model says. Median
work delivered by defaults on mesh20: **32M at two nodes, 123M at four, 216M at
eight.** On mesh22: 20M, 281M, 537M. rmat22 stays flat and is the control.

### Doing 50x less work buys 1.14x

The interesting number is `two-tier-absolute-1600` on mesh20. It delivers **2%
of the work** defaults do -- 5M against 216M -- and is only **1.14x** faster,
down from 1.53x at four nodes. Tracking both fixes across the campaign, with
`w` the median work ratio and `r` the median round ratio against defaults:

| | | two-tier-absolute-1600 | width (small) |
|---|---|---|---|
| mesh20 | 2 nodes | 1.30x, w=0.17, r=1.8 | 1.28x, w=0.23, r=1.5 |
| | 4 nodes | 1.53x, w=0.04, r=2.9 | 1.59x, w=0.11, r=2.6 |
| | 8 nodes | **1.14x**, w=0.02, r=3.3 | **1.70x**, w=0.11, r=1.9 |
| mesh22 | 2 nodes | 0.94x, w=0.91, r=1.1 | 1.01x, w=1.00, r=1.0 |
| | 4 nodes | 1.57x, w=0.07, r=6.0 | 1.59x, w=0.12, r=4.9 |
| | 8 nodes | 1.37x, w=0.04, r=4.0 | 1.41x, w=0.11, r=2.8 |

The two fixes reduce work by comparable orders and **differ in what they charge
in rounds**. On mesh20 at eight nodes the two-tier knob buys its 50x work
reduction at 3.3x the rounds; the width buys a 9x reduction at 1.9x. At 128 PEs
a round is a global reduction and rounds are the expensive resource, so the
cheaper-in-rounds fix wins on time despite doing more work. That is this
campaign's sharpest instance of its own warning: **work, rounds and time each
rank these two variants differently, and only time answers the question.**

The bistability is also still with the two-tier knob and still absent from the
width arms:

```
two-tier-absolute-1600   4 4 5 5 5 5 5 5 5 5 5 5 5 6 187 195    <- 2 of 16 lost
width-217               16 18 20 22 22 22 23 23 24 25 26 26 27 27 29 29
width-1024              21 21 21 22 22 22 23 23 24 25 26 26 27 28 29 30
```

Same two-in-sixteen failure rate as at four nodes, but a lost race now costs
187M instead of 103M, so the tail is heavier even where the median is not.

Taken with wave 3, the case for treating the **initial width** as the defect
and the two-tier bar as a symptom is now three-fold: the width fix removes the
clamp rather than racing it, it never loses the race in 48 runs across two
allocations, and it pays fewer rounds for the work it saves.

## Wave 5: sixteen nodes, 256 workers — the two-tier fix inverts

Job 22042111, `mesh20,mesh22,rmat22`, 432 timed runs, all valid.

| variant | mesh20 | mesh22 | rmat22 |
|---|---|---|---|
| control | 0.99x | 1.03x | 1.03x |
| tuned-fixed | — | 1.01x | 1.09x |
| width (small) | 1.54x | **1.52x** | 0.89x |
| width-1024 | **1.67x** | 1.47x | 1.15x |
| two-tier-absolute-1600 | **0.81x** | 0.95x | 0.99x |
| two-tier-never | 0.42x | 0.65x | 1.01x |
| clamp-strict | 0.98x | 1.01x | 0.99x |
| window-follow | 0.95x | 1.01x | 1.03x |

Defaults deliver **347M** updates on mesh20 and **1.02 billion** on mesh22, the
top of a monotone climb from 32M and 20M at two nodes. The defect scales with
PE count without limit, as the race model says it must.

### The campaign's conclusion, in one table

Speedup, median work ratio `w`, median round ratio `r`, and how often the
variant lost the race in sixteen runs:

**mesh20**

| nodes | PEs / bar | defaults noted | `two-tier-absolute-1600` | `width-1024` |
|---|---|---:|---|---|
| 2 | 32 / 3,200 | 32M | 1.30x w=0.17 r=1.8 lost 0/16 | 1.27x w=0.23 r=1.5 lost 0/16 |
| 4 | 64 / 6,400 | 123M | 1.53x w=0.04 r=2.9 lost 2/16 | 1.51x w=0.11 r=2.5 lost 0/16 |
| 8 | 128 / 12,800 | 216M | 1.14x w=0.02 r=3.3 lost 2/16 | 1.59x w=0.11 r=2.0 lost 0/16 |
| 16 | 256 / 25,600 | 347M | **0.81x** w=0.01 r=4.6 lost **5/16** | **1.67x** w=0.13 r=1.5 lost 0/16 |

**mesh22**

| nodes | PEs / bar | defaults noted | `two-tier-absolute-1600` | `width-1024` |
|---|---|---:|---|---|
| 2 | 32 / 3,200 | 20M | 0.94x w=0.91 r=1.1 | 1.11x w=1.12 r=0.8 |
| 4 | 64 / 6,400 | 281M | 1.57x w=0.07 r=6.0 lost 0/16 | 1.61x w=0.12 r=5.0 lost 0/16 |
| 8 | 128 / 12,800 | 537M | 1.37x w=0.04 r=4.0 lost 0/16 | 1.38x w=0.10 r=2.9 lost 0/16 |
| 16 | 256 / 25,600 | 1,015M | 0.95x w=0.02 r=5.2 lost 1/16 | **1.47x** w=0.09 r=2.7 lost 0/16 |

(At two nodes mesh22 defaults were not failing at all, so "lost" is not
meaningful in that row and is omitted.)

**The two-tier bar is not a fix.** On mesh20 it runs 1.30x, 1.53x, 1.14x,
**0.81x** -- it inverts into a regression at 256 PEs while delivering **1% of
the work**. Its race-loss rate climbs 0, 2, 2, 5 in sixteen, because the bar it
pins is crossed later as PE count rises, and its round cost climbs 1.8x, 2.9x,
3.3x, 4.6x.

**The width is a fix.** On mesh20 it runs 1.27x, 1.51x, 1.59x, **1.67x** --
monotone upward -- and **never once lost the race in 64 runs across four
allocations**. Its round cost *falls* with scale, 1.5x, 2.5x, 2.0x, 1.5x.

`tuned-fixed` on mesh22 at sixteen nodes is the clearest single number in the
campaign: **1.01x while delivering 6% of the work**, at 5.48x the rounds. The
strong fixed policy the 7.5 search selected buys nothing at scale, for exactly
the reason the two-tier knob inverts.

### What this settles

Rounds are the binding resource at scale, and the two candidate repairs differ
in what they charge in rounds rather than in how much work they save. Any
repair that buys a work reduction with a large multiple of rounds inverts
somewhere between 128 and 256 PEs. The width does not, because it is not
trading anything: it removes the clamp, so the controller coarsens normally and
resolves the frontier at the resolution it was designed for.

So of the three candidates in "What the fix cannot be" above, **(2), seeding
the initial width, is the only one the data supports**, and (1), freezing the
clamp threshold, remains worth doing alongside it as the correctness
simplification that lets `COARSEN_CLAMP_LIVE` be deleted. Pinning the two-tier
bar is now positively contraindicated rather than merely unproven.

Item 2's stopping rule was one bounded experiment. This was it, it is finished,
and it named a defect, a mechanism, a rule that predicts which graphs suffer,
and which of two plausible repairs to build.
