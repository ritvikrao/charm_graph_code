# R1 nearest priority: eight-node sanity check

2026-09-21. Jobs **22281608 and 22281609 completed successfully**, in 261 and
267 seconds respectively. All **160 solves**, **32 diagnostic records** and
**80 launches** pass the strict raw-log audit. These are the frozen **unbatched**
R1 binaries, not the newer queue-batching implementation.

The audit verifies complete experiment matrices, source ordering, reference
digests, effective policy/layout, binary hashes, queue conservation,
direct-versus-production edge counts and offered-update topology sums. No new
runtime errors, stalls or rescues were found; the known UCX registration-cache
warnings remain. Both allocations used 64 processes and 960 worker threads,
eight processes of fifteen workers per node. Together they consumed **150.187
allocated CPU-hours**. They share six of eight physical nodes, so they are
separate allocations but not independent hardware samples.

## Performance and work

Times are medians of source medians, two training sources with three timed
repetitions each. Warmups are checked but excluded. Work means attempts per
stored directed edge. All ratios are formed on paired source medians.

| Graph / allocation | Frozen R0 time | Nearest time | Nearest / frozen time | Frozen attempts/edge | Nearest attempts/edge |
|---|---:|---:|---:|---:|---:|
| mesh26-z A | 0.955 s | 0.771 s | 0.818 | 21.986 | 2.783 |
| mesh26-z B | 0.961 s | 0.797 s | 0.843 | 21.951 | 2.832 |
| road-usa-z A | 0.690 s | 1.255 s | 1.823 | 51.346 | 19.042 |
| road-usa-z B | 0.741 s | 1.289 s | 1.737 | 53.372 | 19.399 |

Nearest priority cuts attempts about **87% on mesh and 62–63% on road** against
frozen R0. Mesh time improves **16–18%**, but road becomes **74–82% slower**.
Against same-binary local priority, mesh improves 17–20% and road is 78–80%
slower. Repeated local controls vary by 0–6.1% at the paired aggregate level;
the largest source discrepancy is 12.5% on road A. That variation cannot
explain the road regression. Diagnostic/production time ratios are 0.998–1.061;
diagnostic work ratios are 0.988–1.051. No outliers or slower arms are removed.

Comparing the exact same nearest-policy binary and flags with **both** previous
one-node allocations gives:

| Graph | Eight / one time | Eight / one attempts | Implication |
|---|---:|---:|---|
| mesh26-z | 0.318–0.331 | 1.950–1.990 | About 3.0–3.1x speedup with about twice the work |
| road-usa-z | 1.073–1.104 | 8.656–8.846 | No speedup; work grows more than worker count |

Road's one-node nearest policy performs about 2.18 attempts/edge; at eight
nodes it performs about 19.2. This is an immediate scaling obstacle: growing
work by roughly 8.7x consumes the ideal gain from eight times as many workers.
Process-local priority does not establish global distance order. These results
show that it fails to contain road work growth; they do not isolate which
remote-delivery or controller effect causes that growth.

## Cost and interpretation

Nearest diagnostics measure **722–756 ns of solver work per attempt** on mesh
and **908–916 ns** on road. Previous one-node values were 711–713 and
1,014–1,019 ns respectively. Per-attempt cost does not grow in proportion to
road's work count. Queue time remains about **72% / 75%** of solver work,
and removal remains about **75% / 72%** of sampled queue time for mesh/road.
Failed try-lock probes are about 13% on mesh and 26% on road. This continues
to support the batching intervention, but cheaper removals alone do not
guarantee control of distributed redundant work.

Measured idle-window shares rise to about **23% on mesh** and **8–9% on road**.
Offered inter-node updates account for about 0.33% and 0.42% of attempts.
Those are neither network-byte measurements nor evidence that remote delay
cannot affect priority and repeated work. Sampled aggregate timers are not
an additive critical-path decomposition.

**Decision:** the unbatched R1 policy fails as a general performance solution:
the severe road regression is reproducible. The newer one-node batching
result remains valid, but its scaling must be measured separately. Do not
apply mesh's unbatched speedup to the batched one-node times or present a
cross-binary ratio as strong scaling.

The author authorized proceeding with the recommended two-node test. The
[new experiment decision](ipdps27-r1-batch-scaling.md) fixes batch 8 across
graphs, retains batch 32 as an ablation, and measures work growth as well as
runtime. R2 remains conditional; R3 is still reserved for the author's decision.

Evidence: [full eight-node audit](onenode-data/r1-eight-node-22281608.json),
[same-binary scaling and Slurm accounting](onenode-data/r1-eight-node-scaling-22281608.json).
Raw logs and frozen configuration remain in
`/u/rao1/.tmp/ipdps27-onenode`, using `configs/r1-priority-v2.json`.
