# Frontier per-solve data

Every solve recorded in a structured log on OLCF Frontier (2026-09-22 to
2026-10-03), as CSV for plotting, in the schema of `design/delta-data`.
Regenerate with

    python3 benchmarks/export_frontier_csv.py [OUTDIR]

(Python 3.7 or later; the campaign venv,
`/lustre/orion/csc710/scratch/rrao/acic/venv/bin/python`, works), which reads
the campaign's `logs/` under `/lustre/orion/csc710/scratch/rrao/acic/campaign`,
then queries `sacct` and the design documents for the job table. It uses
`benchmarks/export_delta_csv.py` for the parsing, so the two machines' files
load the same way.

## Files

- `frontier_solves.csv`: one row per solve (warmups included).
- `frontier_jobs.csv`: one row per Slurm job: what it ran, where and when, the
  design document that reports it, and whether it is partial. Join it to
  `frontier_solves.csv` on `job`.

## Columns

As in `design/delta-data/README.md`, with these Frontier specifics:

| Column | On Frontier |
|---|---|
| `harness` | `onenode_ab` (ACIC arms, `logs/AB-<graph>-<N>n-<job>/runs.jsonl`, from `benchmarks/onenode_ab.py` as run by `scripts/frontier/gluon_compare.sbatch` and `onenode_ab.sbatch`) or `run.py` (the baselines, `logs/external-<N>n-56w-<job>.jsonl`, from `benchmarks/run.py --mode external`) |
| `campaign` | always `frontier-campaign` |
| `rpn`, `workers` | ACIC processes per node and worker PEs per node: 8 × 7 = 56 per node unless the arm names another layout |
| `threads`, `cpus`, `delta` | Baseline threads per rank, `srun -c` and Δ (from the run.py configuration) |
| `chunk_band` | For the chunk-queue builds, the band compiled into the binary (`acic_frz_c256`, `acic_c256` and `acic_c256_cost`: 256) |
| `flags` | ACIC flags as passed. Baseline launches leave it blank: their settings are in `arm` (for example `gluon-async-r1-oec-d1024`: Gluon Async, 1 rank per node, partition `oec`, Δ 1024) |
| `outcome` | `ok`, `hang` (stopped at its cap), `crash`, `wrong` (wrong digest), or `failed-check` (below) |
| `launch_wall_seconds` | Frontier only: the wall time of a baseline launch. A launch stopped at its cap (`hang`) has no `seconds`; the paper bounds its solve below by this time less 300 s of graph load (`results_by_dataset.py`), shown as "> T" and "≥ S×" |
| `host` | The node list of the allocation |

**`failed-check` rows** come from a `logs/AB-…-<job>.failed` directory. When a
check failed (a stall or an arm exceeding its step limit),
`gluon_compare.sbatch` set that attempt aside and reran the frozen arm alone
in the normal directory. The `.failed` rows were never audited; exclude them
unless you need the failed arm.

## Loading

```python
import pandas as pd
solves = pd.read_csv('design/frontier-data/frontier_solves.csv', low_memory=False)
jobs = pd.read_csv('design/frontier-data/frontier_jobs.csv', dtype={'job': str})
timed = solves[(solves.warmup == 0) & (solves.valid == True) & (solves.outcome != 'failed-check')]
med = timed.groupby(['job', 'graph', 'nodes', 'engine', 'arm', 'source']).seconds.median().reset_index()
```

## Notes for plotting

- **Timed solves:** `warmup == 0`, `valid == True`, `outcome != 'failed-check'`.
  The statistic is the median per (job, arm, source); the paper gives the range
  over held-out sources.
- **Speedup convention:** baseline time / ACIC time; above 1 means ACIC is
  faster.
- **The paper configuration** (freeze 601697e):
  - Meshes, 3-D grids and terrain: F8w, `arm == 'frozen'` (heap, width ln V / 8)
    and `'c256_s64'` (chunk queue).
  - Roads: F8, `arm == 'frozen'` (heap, width 131072).
  - Scale-free inputs: F8w's 16- and 64-node jobs, where `'frozen'` is the
    ln V / 8 width and `'rule'` the ln V width the paper keeps for them.
- **Which jobs feed which figure** (the full list of table jobs is
  `ACIC_JOBS`, `EXTERNAL_JOBS` and `ONE_NODE_JOBS` in
  `benchmarks/results_by_dataset.py`):

  | Figure | Jobs |
  |---|---|
  | Fig. 4, scaling 1–64 nodes | F8w one node 5569518 (meshes), 5569698 (larger inputs); 4/16/32/64 nodes 5569507–5569510; `terrain30-l-z` 32/64 nodes 5569511/5569512. F8 roads one node 5568963; 4/16/32/64 nodes 5568630/5568632/5568634/5568636. 8 nodes: 5592439 (F8w inputs), 5592440 (`terrain30-m-z`, `grid3-33-z`), 5592441 (roads), queued 2026-10-03 and not yet in these files |
  | One-node GAPBS and Wasp lines | 5558223–5558226 (series), 5565476/5565797 (`road-planet-z`), and the other `ONE_NODE_JOBS` |
  | Figs. 1 and 3, distributed codes at 16 and 64 nodes | Gluon 5558207–5558214, 5558382; Gemini and HavoqGT 5560447–5560462; RIKEN 5565469/5565470, 5569517; scale-free ACIC 5569513/5569514; Gemini and HavoqGT on RMAT 5561482/5561483; HavoqGT with delegates 5566503, 5566974 |
  | Fig. 5, ablation at 64 nodes | F10 5565468 (16 nodes: 5565467), F10b 5568643, F11 5569516 (16 nodes: 5569515), F13 5570489 |

- **Older builds:** before the freeze, the binaries are `acic_slice`,
  `acic_hint*`, `acic_tls*`, `acic_scale64b` and others. `reported_in` and
  `first_commit` in `frontier_jobs.csv` point to where each job was
  discussed; the implementations table in `design/current-state.md` gives
  each build's commit.
- **Partial jobs** (`status` in `frontier_jobs.csv`): 5570584 (F14b at 64
  nodes, timed out) and 5573561 (F14c, 8 × 7 only).

## Not included

Runs that left only `.out` logs and no structured record: the audit outputs
(`logs/onenode-audit-*.json`, which check the solves listed here), HavoqGT
ingest probes, launch-mode and wire tests, and the Projections traces. Delta
data is in `design/delta-data`.
