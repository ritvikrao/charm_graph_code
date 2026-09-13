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
