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
