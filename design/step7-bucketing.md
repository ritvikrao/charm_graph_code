# Step 7.3 — adaptive bucketing

**Result: `--bucket-policy adaptive --bucket-target 8`, now the default, is
0.88× on the mesh and 0.90× on RMAT at 2^20 on one node, and is not measurably
different from the fixed width at 2^22 on two nodes or on the uniform graph
anywhere.** It never measured slower than the harness's own noise. The gain is a
tenth of what the plan carried forward from step 6 for the mesh ("1.7×"), and
that number did not survive step 7.1: it came from switching the controller off
on a run that was waiting on flush cadence.

## Step 6's width sweep, rerun on top of 7.1

Step 6 swept the bucket width as a multiple of the rule the code uses (log V for
the random graphs, sqrt V for the mesh) and found the mesh's best point at the
finest width it tried, 1/16 of the rule, worth 1.70×. Rerun with the same
graphs and the step 7.1 default flush policy (`sweep-*.out`, `coarse-*.out`;
compute time against the rule, same job):

| width × rule | mesh 1 node | mesh 2 nodes | RMAT 1 node | RMAT 2 nodes | uniform 1 node | uniform 2 nodes |
|---|---|---|---|---|---|---|
| 1/16 | 1.02× (rej/\|E\| 5.1) | 1.37× (8.8) | 2.05× | 3.39× | 1.84× | 3.08× |
| 1/4 | 1.55× | 1.34× | 1.19× | 1.62× | 1.27× | 1.41× |
| 1 | 1.00 | 1.00 | 1.00 | 1.00 | 1.00 | 1.00 |
| 4 | **0.82×** | **0.92×** | 0.83× | 0.90× | 0.97× | 1.02× |
| 16 | 0.88× | 1.01× | **0.72×** | 0.83× | 0.97× | 0.97× |
| 256 | – | – | 0.75× | **0.77×** | 0.90× | 0.78× |

(The x16 and x256 random-graph rows come from two jobs, `sweep-rand-*` and
`coarse-*`; x16 is in both and agrees to 0.72/0.80 and 0.83/0.86 on RMAT.)

**The step 6 mesh result was the controller switching off.** The mesh's
distances reach about 490,000; at width 64 that is bucket 7,600 of a 2,048-bucket
array, so everything past distance 131,000 lands in the clamp bucket and is
admitted together. The rejected updates say so: 5.4 per edge in step 6 against
1.1 at the rule. A controller that stops ordering the work is a gain only while
the run is waiting on flush cadence, which step 7.1 removed. Now the same width
is flat on one node and 1.37× slower on two.

**On the random graphs coarser is better, all the way to one bucket.** At
256 × log V (3,550 distance units) RMAT's and the uniform graph's whole distance
range fits in a single bucket, which is the controller switched off, and it is
the fastest or tied-fastest point on both node counts. Rejected updates per edge
do not move (RMAT 1.15 → 1.17). That is step 6's H1 finding again, from the
other side: on these graphs the ordering does not prevent any work, so its only
effect is the rounds it takes to step the threshold through the range (RMAT: 298
rounds at the rule, 43 at x256).

**The mesh is the class where ordering pays, so it has an interior optimum.**
Rejected updates per edge rise from 1.05 at the rule to 1.37 at x4 and 2.52 at
x16, while the rounds fall only 3,727 → 3,116. The optimum is about 4,096
distance units, a few times the maximum edge weight (1,000).

## What was built

**The rule** (`Main::choose_coarsening`). Each round, Main finds the band holding
the middle 90% of the reduced histogram's mass. When the band spans at least
`2 × target` buckets, every `k = band / target` adjacent buckets are merged into
one. Rounds in the two-tier branch (too little in flight to describe a
distribution) are skipped, and so is any round with something in the clamp
bucket. A wide band means the controller is ordering work finer than the work is
spread, which the sweep says is a loss on every class. The rule only coarsens.
The histogram counts updates that are in flight, so no PE holds the items needed
to split a bucket again.

What it picks (`bucket_scale` in the tables below): the random graphs about 10×
at target 8, which is inside their flat region, and the mesh 2–4× at 2^20. At
2^22 it picks 1–2× for the mesh, whose rule width is already 2,048 there. Both
land the mesh at an absolute width of 2,000–8,000 distance units, the same place
the sweep put its optimum. The rule sees the distribution, not |V|.

**The merge** (`SsspChares::coarsen_buckets`, `HTram::coarsenBuckets`). It is
exact because the bucket is now an integer quotient: `(int)(d × m) / scale`, and
`floor(floor(x) / k) = floor(x / k)`. A merged multiplier would round
differently at bucket boundaries. So an update counted into bucket `i` by its
creator before the merge, and uncounted by its receiver after, is uncounted from
`i / k`, which is where the merge put the count. That holds whichever PE merges
first. Every per-PE array indexed by bucket merges in ascending order
(histogram, vertex counts, the diagnosis profile, `pq_hold`). htram merges its
per-destination hold queues the same way. It also adds the items that the
divided threshold now admits (old buckets `t+1 .. k(t/k+1)−1`) to its admitted
counters.

The flag is refused with `--combine hold`: CombiningHold's per-bucket reference
lists cannot be merged in place. Combining is off by default and a loss, so by
default the combination falls back to the fixed width; asking for both is an
error.

## Checks beyond the gate

Two invariants the gate cannot see, added to `sssp_smp_diag` and printed under
`--diag`:

- **Every bucket of the global histogram ends at zero** (bucket 0 at −1, the
  source's update, which is processed without being created). A merge that
  placed an update's two ends in different buckets leaves a nonzero bucket; the
  window sum `rounds.csv` records would hide a +1/−1 pair. Zero in every run at
  targets 1, 3 and 4 on all three graphs.
- **htram's admitted counters match a recount** of the held items at or below
  the threshold plus the buffered ones (`HTram::admittedDrift`), taken every
  round. Zero in every run. Deleting the counter update from `coarsenBuckets`
  makes it read 136,183 on RMAT while `--verify` still passes, which is why the
  check exists: a wrong counter changes when buffers ship, not what is computed.

## Measurements

2^20 and 2^22, 16 PEs per node, exclusive Delta CPU nodes, `+setcpuaffinity`,
variants interleaved within each repetition, ratios against `fixed` in the same
job. `policy-*.out`, five repetitions:

| | mesh 1n | mesh 2n | mesh 2^22 1n | RMAT 1n | RMAT 2n | RMAT 2^22 1n | uniform 1n | uniform 2n | uniform 2^22 1n |
|---|---|---|---|---|---|---|---|---|---|
| target 1 | 0.90× | **1.14×** | 0.96× | 0.87× | 0.89× | 1.03× | 0.98× | 0.87× | 1.10× |
| target 2 | 0.89× | 1.00× | 0.95× | 0.87× | 0.89× | 1.06× | 1.01× | 0.88× | 1.00× |
| target 4 | 0.81× | 0.96× | 0.96× | 0.94× | 0.88× | 1.04× | 1.07× | 0.89× | 1.08× |
| target 8 | 0.85× | 0.94× | 0.96× | 0.92× | 0.91× | 1.00× | 1.03× | 0.83× | 1.05× |

**The harness has a position bias, and it is about 5%.** In the 2^22 mesh
column, targets 4 and 8 never coarsened: same bucket scale, same rounds and same
rejected updates as `fixed`, and 0.96×. The confirmation jobs therefore run
`fixed` twice, first and last in each repetition, with ten repetitions
(`confirm-*.out`):

| | fixed-again (control) | target 4 | target 8 |
|---|---|---|---|
| mesh 2^20, 1 node | 0.97× | **0.86×** | **0.88×** |
| RMAT 2^20, 1 node | 1.05× | **0.92×** | **0.90×** |
| uniform 2^20, 1 node | 1.05× | 1.06× | 1.06× |
| mesh 2^22, 2 nodes | 1.00× | 0.99× | 0.98× |
| RMAT 2^22, 2 nodes | 0.97× | 0.95× | 0.95× |
| uniform 2^22, 2 nodes | 1.05× | 1.08× | 1.05× |

Only the first two rows clear the control. That is also the reading of
`policy-*.out` once differences under 5% are discounted. The one loss above the
noise is target 1 on the mesh on two nodes, whose 32× coarsening costs 3.2×
the rejected updates.

**Why target 8.** Targets 4 and 8 are indistinguishable where either gains.
Target 4 read 1.08× on the uniform graph at 2^22 in both jobs, at the edge of the
bias, and keeps less resolution on the mesh (1.36 rejected per edge against
1.12). Nothing measured favours 4.

**Why the default changes at all.** It gains 10–12% on two of three graph
classes at the scale every step 6 and 7 number uses, and loses nowhere
measurably. The alternative is a fixed width known to be too fine on every class.

## What is not explained

**The gain does not survive to 2^22 on the random graphs.** The width sweep at
2^20 cut RMAT's rounds 295 → 53 and its time by 28%; at 2^22 the same policy cuts
rounds 319 → 84 and the time by nothing. The obvious candidate is that the
saving is per round (each round draws partial flushes, one chare in five, and
steps the threshold), and so a fixed cost that a larger problem dilutes. RMAT at
2^22 sends 34,000 messages against 10,700 at 2^20 for a similar number of
rounds. That is a hypothesis; nothing here isolates it. If it is right, the
width is a small-problem effect on the random graphs, and the paper should
present it on the mesh, where the sweep has an interior optimum for a structural
reason.

## Consequences for the plan

- **Replace "worth 1.7× on the mesh and 9% on RMAT"** with about 12% on the mesh
  and 10% on RMAT at 2^20, and nothing resolvable at 2^22. Step 6's 1.7× was
  measured in the cadence-bound regime and is gone.
- **Ratios under 5% from this harness are noise**, including the 7.1 and 7.2
  ratios that close to 1.00. The conclusions drawn from those were
  "no slower" statements, which this does not change. Anything the paper claims
  at that size needs a control variant like `fixed-again`.
- **`HISTO_BUCKET_COUNT` and `histo_reduction_width` can stay.** Step 6 noted
  both were oversized. After coarsening the random graphs span tens of buckets
  and the mesh a few hundred, so neither the clamp nor the window edge is
  approached. The coarsening rule refuses to run while anything is clamped.
- **The scale-free deficit is still open.** Coarser buckets are a gain on RMAT
  for the same reason `p_heap` 0.999 was in step 6: its controller does no
  useful ordering, so the less of it the better. That is not closing a gap.

## Reproducing

```
sbatch [-N 2] scripts/ab_delta.sbatch scripts/ab/7.3-width-sweep-random.txt 20   # SSSP_GRAPHS="uniform rmat"
sbatch [-N 2] scripts/ab_delta.sbatch scripts/ab/7.3-width-sweep-mesh.txt 20     # SSSP_GRAPHS=mesh
sbatch [-N 2] scripts/ab_delta.sbatch scripts/ab/7.3-width-sweep-coarse.txt 20   # SSSP_GRAPHS="uniform rmat"
sbatch [-N 2] scripts/ab_delta.sbatch scripts/ab/7.3-bucket-policy.txt 20|22
SSSP_REPS=10 sbatch [-N 2] scripts/ab_delta.sbatch scripts/ab/7.3-bucket-confirm.txt 20|22
SSSP_EXTRA_ARGS="--bucket-policy fixed" scripts/verify.sh
./sssp_smp_diag ... --diag <prefix>     # prints the two invariant checks
```
