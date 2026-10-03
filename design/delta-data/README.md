# Delta per-solve data

Every solve recorded in a structured log on NCSA Delta (2026-09-13 to
2026-10-02), as CSV for plotting. Regenerate with

    python3 benchmarks/export_delta_csv.py [OUTDIR]

which reads the campaign directories under `/work/hdd/mzu/rao1/acic-*` and
`/u/rao1/.tmp/ipdps27-onenode`, then queries `sacct` and the design documents
for the job table.

## Files

- `delta_solves.csv`: one row per solve (warmups included), every harness
  normalized to one schema.
- `delta_jobs.csv`: one row per Slurm job: what it ran, where and when, the
  design document that reports it, and whether a later job supersedes it.
  Join it to `delta_solves.csv` on `job`.

## `delta_solves.csv` columns

| Column | Meaning |
|---|---|
| `campaign` | Campaign directory (`acic-…`, or `ipdps27-onenode-tmp` for `/u/rao1/.tmp/ipdps27-onenode`) |
| `job` | Slurm job ID |
| `harness` | Record format: `onenode_ab` (`benchmarks/onenode_ab.py`), `run.py` (`benchmarks/run.py`), `launch_ab` (`scripts/delta/d7_launch_ab.sbatch`), or the directory prefix of a per-study harness (`compare`, `layout`, `queue`, `time`, `probe`, `features`, `wasp-boundary`, `GAP-R0`) |
| `record_dir` | Directory holding the record and the solve's own log |
| `graph`, `source` | Input and source vertex |
| `role` | `tune`/`train` or `test` (held-out) where the harness recorded it |
| `phase`, `rep` | Harness phase name and repetition; `rep` −1 is a warmup in most harnesses |
| `warmup` | 1 if `rep` is −1 or the phase name contains "warmup"; exclude these for timing |
| `engine` | `acic` (and `run.py`'s `acic-width`, `acic-progress`, `acic-quiet`, `acic-shm` builds), `gap`, `wasp`, `wasp-features`, `gluon`, `riken` |
| `arm` | The variant, label or configuration name the harness used |
| `binary` | Executable name (campaign `bin/`); ACIC builds are listed in each campaign's manifests |
| `nodes` | Nodes in the launch |
| `rpn` | Processes (ranks) per node |
| `workers` | ACIC worker PEs per node (16 × 7 = 112 on the 2026-09-25 onward one-node runs) |
| `threads` | OpenMP threads (GAPBS, Wasp) or threads per rank (RIKEN, Gluon) |
| `cpus` | `srun -c` of the launch |
| `omp_bind` | `close` (packed) or `spread`, where known |
| `delta` | GAPBS/Wasp/Gluon/RIKEN Δ |
| `bucket_width`, `heap_slice`, `chunk_band`, `leaf_prune`, `process_queue` | ACIC flags; blank means the build default |
| `flags` | Every `--`/`+` flag of the launch, as passed |
| `seconds` | Solve time as the code reports it (graph loading excluded; GAPBS and Wasp include distance initialization) |
| `valid` | The solve's digest matched the independent reference |
| `outcome` | `ok`, `hang` or `crash` where the harness recorded it |
| `rounds` | ACIC controller rounds (`reductions` in `run.py` records) |
| `edge_attempts`, `attempts_per_edge` | ACIC edge relaxation attempts, total and per graph edge (`scan_factor` in older harnesses) |
| `wire_bytes_per_edge`, `node_bytes_per_edge` | ACIC D2 counters, where built in |
| `host` | Node(s) the solve ran on |
| `selection_job` | For `run.py` baseline runs at frozen settings, the tuning job whose selection they used |

## Loading

```python
import pandas as pd
solves = pd.read_csv('design/delta-data/delta_solves.csv')
jobs = pd.read_csv('design/delta-data/delta_jobs.csv', dtype={'job': str})
timed = solves[(solves.warmup == 0) & (solves.valid == True)]
med = timed.groupby(['job', 'graph', 'engine', 'arm', 'source']).seconds.median().reset_index()
d7 = med[med.job.isin([22625422, 22624600, 22623920, 22627666, 22623916, 22623971, 22624107, 22624395])]
```

## Notes for plotting

- **Timed solves:** `warmup == 0` and `valid == True`. The paper's statistic is
  the median per (job, arm, source), and comparisons are within one job (one
  allocation, one node).
- **Speedup convention:** baseline time / ACIC time; above 1 means ACIC is
  faster.
- **Current results:** filter `delta_jobs.csv` on an empty `status`. D7, the
  paper's one-node comparison, is current-state §41; its compare jobs are
  22625422 (`mesh26-z`), 22624600 (`mesh28-z`), 22623920 (`mesh30-z`),
  22627666 (`grid3-30-z`), 22623916 (`terrain30-c-z`), 22623971
  (`road-usa-z`), 22624107 (`road-eu-z`) and 22624395 (`rmat25`). Their tune
  jobs are in the same file (phases `external-joint`, `external-confirm`).
- **Node variation:** baseline times differ up to 1.8× between Delta nodes with
  identical hardware (§41: cn099 against cn022). Use `host` before comparing
  absolute times across jobs.
- **Older campaigns** (2026-09-13 to 09-21, `acic-comparison-20260913`,
  `ipdps27-onenode-tmp`) used earlier code and layouts (1 × 16, 8 × 120);
  `reported_in` and `first_commit` in `delta_jobs.csv` point to where each was
  discussed.

## Not included

Runs that left only `.out` logs and no structured record: the R0/R1/D0
diagnostics in `ipdps27-onenode-tmp` (their compact summaries are
`design/onenode-data/r0-*`, `r1-*`), the step 6 campaigns (`acic-step6*`),
`sssp-2node` and the Projections traces. Frontier and Anvil data are not in
this directory.
