# Step 7.6f2 — admission × delivery, first wave (Anvil)

*2026-09-15. Item 4 of the 7.5 next work, and the research claim Gate A turns
on. One allocation per node count; an independent replication at 1/2/8 nodes
and a first 16-node allocation are queued (jobs 20744731-34). Nothing below is
established until it repeats there.*

## What was run

`benchmarks/run.py --mode policy --workers 120 --acic-rpn 8`, jobs 20744389 (1
node), 20744392 (2) and 20744393 (8), on Anvil, one binary for every arm
(`bin/acic` sha256 `657ff378...`, the 7.6g repair included, manifest in
`bin/policy-manifest.txt`). Eight processes of fifteen per node throughout.

| arm | admission | delivery | width |
|---|---|---|---|
| `adaptive` | coarsening on (shipped) | shipped flush policy + starvation-gated idle flush | shipped rule + per-graph map |
| `control` | `adaptive` under another name: the floor | | |
| `adapt-admission` | coarsening on | flush every round, no idle flush | rule + map |
| `adapt-delivery` | no coarsening | shipped | rule + map |
| `fixed-both` | no coarsening | flush every round, no idle flush | rule + map |
| `global-fixed` | chosen once per allocation from {`logv`, `weight`} × flush interval {1, 5}, no coarsening, no idle flush, on the tuning sources of mesh20, rmat20 and uniform20 | | |
| `tuned-fixed` | chosen per graph from the 7.5 fixed search space on that graph's two tuning sources | | |

Every arm runs on each of four held-out sources twice, in random order within
each (source, rep). Selections: `global-fixed` chose `weight`, interval 1, at
all three allocations; `tuned-fixed` chose interval 1 with width 1024 (65,536
for road-ny) almost everywhere, interval 5 for the uniform graphs at two nodes
and width `logv` for uniform/youtube at one.

**1,512 timed runs, none hung, wrong, crashed or rescued**, which is the first
campaign table since 7.6g reopened item (1) that can say so. Two runs at eight
nodes took more than 3x their cell median (the Anvil messaging slowdown is a
known hazard); neither changes a median of eight pairs.

## Read this first: these inputs do not scale to eight nodes

The shipped configuration solves every graph here in 0.07-0.44 s, and it gets
*slower* from one node to eight (mesh22 0.35 → 0.37 s, rmat22 0.35 → 0.44 s,
uniform20 0.11 → 0.22 s). At 960 workers these graphs are communication- and
control-dominated. The eight-node column measures policy in that regime, not
at the scale the paper is about, and its largest ratios -- 20x on road-ny --
are ratios between sub-second and several-second runs of a 264,346-vertex
graph on 960 PEs. The right follow-up is larger inputs, not more repeats of
these.

## Results

Paired medians of `adaptive` time / arm time over (source, rep); above 1 the
arm is faster, **bold** where it is at least 1.3x slower. `·` within the
allocation floor, `~` clears only the graph's own floor, then pairs won out of
eight. Development graphs are marked `(dev)`: `global-fixed` is in-sample on
them.

#### `adapt-admission`: adaptive admission, fixed delivery

| graph | 1 node | 2 nodes | 8 nodes |
|---|---|---|---|
| mesh20 (dev) | 1.04x slower ~ 3/8 | **1.49x slower** 0/8 | **3.44x slower** 0/8 |
| mesh22 | 1.07x slower · 3/8 | 1.19x slower 2/8 | **2.89x slower** 0/8 |
| road-ny | **1.41x slower** 0/8 | **2.54x slower** 0/8 | **20.42x slower** 0/8 |
| rmat20 (dev) | 1.00x · 4/8 | 1.02x slower · 3/8 | 1.11x · 5/8 |
| rmat20-s2 | 1.09x ~ 5/8 | 1.01x slower · 4/8 | 1.03x · 4/8 |
| rmat22 | 1.01x slower ~ 3/8 | 1.04x · 5/8 | 1.11x ~ 5/8 |
| uniform20 (dev) | 1.06x ~ 5/8 | 1.10x 5/8 | 1.14x ~ 7/8 |
| uniform20-s2 | 1.03x slower · 4/8 | 1.31x 7/8 | 1.08x slower · 3/8 |
| youtube | 1.07x ~ 5/8 | 1.12x 7/8 | 1.15x slower 3/8 |

#### `adapt-delivery`: fixed admission (no coarsening), adaptive delivery

| graph | 1 node | 2 nodes | 8 nodes |
|---|---|---|---|
| mesh20 (dev) | 1.08x ~ 7/8 | 1.01x ~ 4/8 | 1.23x slower 0/8 |
| mesh22 | 1.03x · 6/8 | 1.05x ~ 6/8 | 1.09x slower ~ 1/8 |
| road-ny | 1.02x slower · 3/8 | 1.01x slower · 4/8 | 1.02x · 5/8 |
| rmat20 (dev) | 1.12x slower 0/8 | 1.24x slower 0/8 | **1.38x slower** 0/8 |
| rmat20-s2 | 1.14x slower 1/8 | 1.25x slower 0/8 | 1.24x slower 1/8 |
| rmat22 | 1.06x slower ~ 1/8 | 1.14x slower 1/8 | **1.34x slower** 0/8 |
| uniform20 (dev) | 1.24x 6/8 | 1.05x slower ~ 3/8 | **1.38x slower** 2/8 |
| uniform20-s2 | 1.28x 7/8 | 1.05x · 6/8 | **1.67x slower** 0/8 |
| youtube | 1.28x slower 1/8 | **1.57x slower** 0/8 | **1.85x slower** 1/8 |

#### `fixed-both`: both axes fixed

| graph | 1 node | 2 nodes | 8 nodes |
|---|---|---|---|
| mesh20 (dev) | 1.21x slower 1/8 | **1.74x slower** 0/8 | **4.17x slower** 0/8 |
| mesh22 | 1.03x · 5/8 | **1.40x slower** 0/8 | **3.98x slower** 0/8 |
| road-ny | **1.38x slower** 0/8 | **2.42x slower** 0/8 | **20.06x slower** 0/8 |
| rmat20 (dev) | 1.13x slower 2/8 | 1.26x slower 0/8 | **1.35x slower** 0/8 |
| rmat20-s2 | 1.07x slower ~ 3/8 | 1.18x slower 1/8 | 1.17x slower 1/8 |
| rmat22 | 1.06x slower ~ 2/8 | 1.01x slower · 3/8 | 1.07x slower · 1/8 |
| uniform20 (dev) | 1.29x 7/8 | 1.21x 5/8 | **1.55x slower** 0/8 |
| uniform20-s2 | 1.24x 7/8 | 1.45x 7/8 | **1.92x slower** 0/8 |
| youtube | 1.22x slower 2/8 | **1.58x slower** 0/8 | **2.54x slower** 0/8 |

#### `global-fixed`: one fixed setting, chosen on the development graphs

| graph | 1 node | 2 nodes | 8 nodes |
|---|---|---|---|
| mesh20 (dev) | 1.06x slower ~ 2/8 | **2.06x slower** 0/8 | **3.75x slower** 0/8 |
| mesh22 | 1.03x · 5/8 | **1.35x slower** 0/8 | **3.87x slower** 0/8 |
| road-ny | 1.38x 6/8 | 1.04x slower ~ 3/8 | **2.68x slower** 0/8 |
| rmat20 (dev) | 1.06x slower ~ 3/8 | 1.09x slower 1/8 | 1.08x slower · 3/8 |
| rmat20-s2 | 1.03x slower ~ 3/8 | 1.10x slower 0/8 | 1.31x 6/8 |
| rmat22 | 1.07x slower ~ 2/8 | 1.05x slower · 2/8 | 1.21x 6/8 |
| uniform20 (dev) | **1.87x slower** 0/8 | **2.23x slower** 0/8 | 1.09x ~ 5/8 |
| uniform20-s2 | **2.17x slower** 0/8 | **1.94x slower** 0/8 | 1.16x slower 1/8 |
| youtube | 1.06x slower ~ 2/8 | 1.07x ~ 6/8 | 1.01x · 5/8 |

#### `tuned-fixed`: best fixed setting per graph, chosen on its own tuning sources

| graph | 1 node | 2 nodes | 8 nodes |
|---|---|---|---|
| mesh20 (dev) | 1.10x slower 3/8 | **1.68x slower** 0/8 | **3.59x slower** 0/8 |
| mesh22 | 1.01x slower · 3/8 | **1.34x slower** 0/8 | **3.93x slower** 0/8 |
| road-ny | 1.50x 6/8 | 1.05x slower ~ 3/8 | **2.33x slower** 0/8 |
| rmat20 (dev) | 1.06x slower ~ 2/8 | 1.02x slower · 4/8 | 1.04x · 7/8 |
| rmat20-s2 | 1.05x ~ 5/8 | 1.09x slower 2/8 | 1.26x 7/8 |
| rmat22 | 1.13x slower 2/8 | 1.11x slower 1/8 | 1.13x ~ 5/8 |
| uniform20 (dev) | 1.24x 5/8 | 1.15x 5/8 | 1.08x ~ 5/8 |
| uniform20-s2 | 1.15x 7/8 | 1.13x 6/8 | 1.18x slower 0/8 |
| youtube | 1.20x slower 0/8 | 1.08x ~ 4/8 | 1.02x · 5/8 |

| | 1 node | 2 nodes | 8 nodes |
|---|---|---|---|
| allocation floor (worst graph, `control` vs `adaptive`) | 1.09x | 1.09x | 1.15x |

Full tables with outcome counts: `report_arms.py` over
`/anvil/scratch/x-rrao/acic/campaign/logs --pattern 'policy-*.jsonl'`.

## What holds in all three allocations

1. **Fixing delivery costs most on the high-diameter graphs, and the cost grows
   with node count.** `adapt-admission` on road-ny: 1.41x, 2.54x, 20.4x slower,
   0 pairs won in 24; mesh20 1.04x, 1.49x, 3.44x; mesh22 1.07x, 1.19x, 2.89x.
   On RMAT and uniform graphs the same change is within the floor or slightly
   faster.
2. **Removing coarsening costs most on the scale-free graphs.** `adapt-delivery`
   on rmat20 1.12x, 1.24x, 1.38x slower; rmat20-s2 1.14x, 1.25x, 1.24x; youtube
   1.28x, 1.57x, 1.85x; rmat22 at two and eight nodes. It does nothing on
   road-ny and little on mesh until eight nodes.
3. **No single fixed setting stands in for the adaptive one.** `global-fixed`
   loses 1.87x-2.23x on both uniform graphs at one and two nodes and 2.7x-3.9x
   on the meshes and road-ny at eight.
4. **The two axes look independent rather than co-designed.** Where both
   matter, `fixed-both` is close to the product of the two single-axis costs
   (mesh20 at eight nodes: 3.44x and 1.23x against 4.17x; youtube: 1.15x and
   1.85x against 2.54x). Each axis pays on a different graph class. That
   supports *adaptive admission* and *adaptive delivery* as separate results;
   it does not by itself support an interaction, which is what the co-design
   claim needs. Test it directly before writing it.

## What does not hold

- **Per-case tuning versus adaptive changes sign with node count.**
  `tuned-fixed` beats `adaptive` on road-ny at one node (1.50x) and loses at
  two and eight (1.05x, 2.33x); it beats it on rmat20-s2 and rmat22 at eight
  nodes (1.26x, 1.13x) and loses at one or two. A tuned fixed setting is chosen
  on two sources in the same allocation, so this is not a selection artefact of
  one allocation; it is the policy that is allocation-dependent.
- **Uniform graphs prefer no coarsening at one and two nodes and the opposite at
  eight** (`fixed-both` 1.29x, 1.21x, then 1.55x slower).
- The one-node column is the one Gate A's proximity target is closest to failing:
  `tuned-fixed` is 1.15x-1.50x faster than `adaptive` on road-ny, uniform20 and
  uniform20-s2 there, outside the 10% geometric target.

## Against Gate A, provisionally

- *Adaptive benefit over a frozen global fixed setting:* yes in all three
  allocations, on different graphs in each. The worst `global-fixed` cell is
  uniform20-s2 at one node, 2.17x slower than adaptive; its best is road-ny at
  one node, 1.38x faster.
- *Proximity to per-case tuning:* not met at one node (up to 1.50x behind);
  at two, adaptive is ahead or within the floor on six of nine and behind by 1.08x-1.15x on uniform20, uniform20-s2 and youtube; mixed at eight.
- *Credible external results:* not addressed here (7.6f1).
- *No unexplained stalls:* none in 1,512 runs.

## Next

1. Read the replication (20744731-34) before any of the above is quoted; the
   16-node job is the first allocation above eight, and also the scale at which
   rmat20's width question is parked.
2. Larger inputs for two nodes and up -- the present set is sub-second at every
   allocation.
3. An interaction test for co-design: the matrix above cannot distinguish two
   independent adaptive mechanisms from one shared signal. The candidate is
   feeding the admission signal to delivery (and back) against the two run
   independently; only if that beats the product should the claim say co-design.
4. One bounded optimization, if the replication agrees: the coarsening rule on
   uniform graphs at low node counts, where both fixed-width arms win.
