# SSSP comparison pilot on Delta

This harness compares ACIC, RIKEN Graph500-SSSP, and GAPBS SSSP on identical
weighted inputs. It is a research pilot, not an official Graph500 or GAP score.
GAPBS is run only on one node. Gluon-Async and Gluon-Sync use separate paired
allocations; RIKEN supplies the distributed delta-stepping comparison.

## Dependencies and storage

`build.sh` reuses the installed Charm++ runtime and htram configured in the
application's `config.mk`. It expects a GAPBS checkout in
`$ACIC_BENCH_DEPS/gapbs` (default `~/acic-comparison-deps`) and archives RIKEN
commit `552f156297d921856b74f5c238a93bcbd361bb95` from `~/Graph500-SSSP` into a
small, separate directory. It preserves changes in the existing RIKEN checkout.
The RIKEN compatibility changes are `<limits>`, an explicit `<cinttypes>`
include, and the 4096-byte OS page size. Solver kernels are unchanged.

The GAP driver includes upstream `sssp.cc` and calls `DeltaStep`; the renamed
upstream main is unused. The RIKEN adapter calls the original graph constructor,
`prepare_sssp`, and `run_sssp`. Its driver exposes optional preprocessing.
Builds use GCC 14, `-O3`, and OpenMP for the baselines, and Cray's `CC` MPI
wrapper. No fast-math flag is used. The build manifest records source revisions
and binary hashes; published results also archive the harness source hashes.

Keep graphs and raw results under one scratch campaign, with a single snapshot
of the executable files. Raw process output is appended to one compressed file
per allocation; structured results use one JSONL file per allocation. There is
no runtime checkout per job. The nine prepared graphs occupy about 2.3 GiB.

## Common weighted inputs

`prepare_graph.cpp` writes GAPBS binary `.wsg` CSR files. ACIC and GAPBS consume
those files directly. RIKEN receives one copy of each undirected edge and its
constructor inserts both directions. All input topologies are canonicalized:
self-loops are removed, directions are merged, and parallel edges retain the
minimum weight. Neighbors are sorted by vertex ID. Thus the synthetic inputs
are **different from the directed generators used in previous step 7 tables**.

* Uniform and RMAT use graphlib with a target average out-degree of 16,
  integer weights 1–1000, and seeds 1 and 2. Uniform draws each out-degree
  uniformly from 0–32; RMAT draws 16V edges before filtering. The RMAT generator
  uses probabilities (0.57, 0.19, 0.19, 0.05), permuted labels, and no per-level
  noise; it is not the official Graph500 generator. Mesh uses the weighted grid generator.
* [DIMACS New York road distances](https://www.diag.uniroma1.it/~challenge9/download.shtml)
  provide genuinely weighted road topology. Download `USA-road-d.NY.gr.gz`
  from the official `data/USA-road-d/` directory. Preserve integer distance
  weights, merging opposite arcs by their minimum when they differ.
* [SNAP YouTube](https://snap.stanford.edu/data/com-Youtube.html) supplies the
  undirected social graph. Sort the distinct original labels and relabel them
  consecutively. Assign weights 1–1000 by the deterministic unordered-endpoint
  hash in `prepare_graph.cpp`, before relabeling. These weights are synthetic.

RIKEN expects floating-point weights in [0,1]. Divide the common integer weights
by the least power of two at least as large as the maximum weight. This scaling
is exact in binary32. Delta is specified in the original integer units and
scaled by the same denominator. Do not treat this argument as a license to
accept approximate answers: every output distance must map back to an integer
below 2^24, and every run must pass the independent digest comparison. Inputs
whose reference maximum distance reaches 2^24 are rejected by this pilot.

`reference_gap.cpp` uses GAPBS's independent CSR reader and a separate serial
64-bit Dijkstra implementation. It hashes every vertex and distance, normalizing
unreachable sentinels, and records reachable vertices/arcs, distance sum, and
maximum distance. Two 64-bit hashes plus counts and sum are probabilistic
checks, not a mathematical certificate. They avoid materializing multiple full
distance vectors per run on scratch.

Each input has ten deterministic distinct sources selected with seed 20260913
from vertices with positive degree. The first two are reserved for tuning;
the other eight are test sources. Source eligibility never depends on timing.
The separate Python fixture gate covers empty graphs, isolated sources, small
components, and a path crossing ownership partitions, with independent Python
Dijkstra and ACIC's in-process `--verify` both enabled.

## Running

```sh
bash benchmarks/build.sh
# Copy deps/bin/{acic,riken_sssp,gap_sssp,prepare_graph,reference_gap,
# build-manifest.txt} to a fresh campaign/bin on scratch.
sbatch --output=/path/to/campaign/logs/prepare-%j.out \
  benchmarks/prepare.sbatch /path/to/campaign
sbatch -N 1 benchmarks/compare.sbatch /path/to/campaign --mode tiny
sbatch -N 2 benchmarks/compare.sbatch /path/to/campaign --mode smoke \
  --graphs rmat20,mesh20,road-ny
# Start timing jobs after correctness and input preparation succeed.
sbatch -N 1 benchmarks/compare.sbatch /path/to/campaign
sbatch -N 2 benchmarks/compare.sbatch /path/to/campaign
```

`compare.sbatch` requests exclusive CPU nodes. Default occupancy is 16 ACIC
workers/node, with one further core per process left free for the OS; OpenMP
baselines use 16 workers/node. RIKEN tries one rank × 16 threads and four
ranks × 4 threads per node. ACIC workers bind to cores 0–15. Reconverse has no
communication thread (`reconverse/src/cpuaffinity.cpp`: “also no commap, we
have no commthreads”), so LCI progress is made by the worker threads in the
scheduler loop; the free core is for the OS, not for ACIC. The recorded
campaign passed a `+commap` naming it, which the runtime warned about and
ignored; the flag has since been removed and the core budget is unchanged. Slurm binds
baseline ranks to cores; `OMP_PLACES=cores`, `OMP_PROC_BIND=close`, and
`NO_AFFINITY=1` keep upstream RIKEN affinity code from overriding that binding.
Record higher occupancy separately using `--workers`; the node budget remains
equal even though the number of OS cores left free differs with the process
count.

Baseline delta candidates are denominator/{64,16,4,1}. Fixed ACIC tries flush
intervals {1,5} with bucket widths {default, ceil(training max distance/1024),
denominator}; combining, idle flush, and bucket adaptation are off. This is a
bounded fixed-policy search, not an oracle over every possible parameter.
Select the smallest geometric-mean time on the two tuning sources separately
for each graph/node/occupancy setting, then freeze the choice. Record every
tuning failure; it must never become a speedup.

The main matrix disables RIKEN's optional edge-removal presolver, so those
numbers must be labeled accordingly. `--mode presolve` separately checks
4000 requested preprocessing rounds with upstream `PRESOL_SECONDS=30`, on both
tuning sources, against no preprocessing at the same layout and delta. The
upstream time limit is checked at round boundaries and can overshoot. This
sensitivity probe is not the uncapped upstream default or a tuned presolver.

Test variants are current defaults, an identical duplicate control, repaired
pre-step-7 settings (`old-fixed`), tuned fixed settings, and relaxed admission
(`open`, both percentiles 1). The last still has ACIC's bucket window, initial
thresholds, priority queues, and controller; it is not a pure unordered solver.
There are two randomized passes over the eight held-out sources. Each
implementation gets a discarded warmup per graph before tuning. Every query
starts a new process. Timing follows each implementation's kernel entry point:
GAPBS allocates its distance/frontier vectors inside that call; ACIC constructs
its per-vertex state with the input graph before starting the solve timer.

Solve times include control and termination and exclude graph input, graph
construction, optional RIKEN preprocessing, and digest verification. ACIC uses
its existing `Compute time`; GAPBS times `DeltaStep`; RIKEN reports the maximum
rank time around `run_sssp`, following an MPI barrier. Launcher wall time is
also retained, but includes Slurm step overhead and must not be described as
kernel or application-only end-to-end time. ACIC read/total times and RIKEN
construction/preprocessing times are retained separately. TRAM payload bytes
are not measured wire traffic. ACIC's noted/rejected/distance-change counters
are not interchangeable with baseline edge-examination counts.

For ratios, first combine repeats within a source, then use paired source
ratios with uncertainty. State the direction: baseline/ACIC above one favors
ACIC. Treat graphs and allocations as separate experimental units; repeated
sources within one allocation do not quantify allocation-to-allocation noise.
Use the duplicate control to assess what differences the experiment resolves.

`--mode confirm --selection-job JOB` repeats frozen choices in an independent
allocation and can try additional RIKEN layouts on the tuning sources (for
example `--ranks-per-node 2` gives a square four-rank grid on two nodes).
Confirmation records are separate from the primary matrix. Scaling and high
occupancy jobs can use `--ranks-per-node 1,2,4`; the original one/two-node pilot
searches 1 and 4. Record the actual search rather than implying exhaustive tuning.

`report.py CAMPAIGN/logs OUTPUT` exports compressed records, CSV, and Markdown
tables. It requires completed jobs, matched variants, eight test sources, and
two repeats. `--allow-partial` is only for monitoring an unfinished campaign.
An unsuccessful held-out query suppresses the timing and speedup for that
entire variant/cell and appears as `FAIL`; the result is never averaged over
only successful queries. `--mode finish --selection-job JOB --graphs GRAPH`
can complete unattempted slots using the original frozen choices. It retains
the original failure and separately replays the failing source five times
alongside tuned fixed. The reports identify the independent completion job.

`--mode numa --workers 120 --graphs rmat22,mesh22` compares ACIC using one
process with 120 workers, four with 30, and eight with 15 on one physical node.
`launch_acic.sh` gives each process a contiguous core region one core short of
its stride, leaving that core to the OS, so all three layouts run 120 worker
threads on 120 cores and leave 1, 4 or 8 free. Both current
defaults and the specified fixed policy (flush 1, width 1024, no idle flush or
bucket adaptation) run on all layouts. This fixed policy is a diagnostic
choice, not the per-case tuned winner. All eight held-out sources run twice
in random order. **Changing the process count also changes Charm++'s SMP-node
count used by the controller:** this is a deployment-geometry sensitivity
experiment, not an isolated NUMA-affinity ablation. Its results use a separate
allocation and are reported separately from the main occupancy sweep.
The four LCI devices per process also mean different total endpoint counts.
`--mode layout_confirm --workers 120 --selection-job JOB` then pairs the
selected 8 × 15 layout with the frozen RIKEN/GAPBS choices in a new allocation.
It transfers the fixed policy chosen at 1 × 120 without retuning it. Since the
layout was selected after inspecting the geometry probe on these same sources,
this is deployment confirmation, not a new held-out policy-generalization test.

For the current binary, `Read time` is **not assigned for GAPBS file mode**;
its printed zero is not a valid measurement. Do not publish it as input time.
The retained ACIC `Total time` includes setup and the final statistics reduction,
but there is no matching end-to-end timer for all baselines in this pilot.
RIKEN's separate construction timer is rank zero's graph-constructor time,
excluding the adapter's file read and other preparation. In particular, a
fast repeated-query kernel does not imply cheap fresh-process construction.

## Gluon comparison

The supplemental campaign uses [Galois](https://github.com/IntelligentSoftwareSystems/Galois)
commit `b67f94206a8c47fd414446621f6633a31c49fd98`. Its distributed SSSP push
application implements both `-exec=Async` and `-exec=Sync`.
`build_gluon.sh` applies `gluon.patch`, which adds a timer and a digest of master
vertices after the solver returns. It changes no relaxation, scheduling,
partitioning, or termination code. The checksum accumulators must be explicitly
reset before use; their upstream constructor does not initialize the cached
scalars. The initial adapter omitted this reset and failed verification; those
attempts are retained separately and contribute no performance results.

The build reuses installed LLVM 19.1.7 (RTTI enabled), Boost 1.73, fmt, libnuma,
and Cray MPI, with GCC 14 `-O3 -march=znver3`. No LLVM or Boost source tree is
downloaded or rebuilt. The final build excludes the unused Cray LibSci BLAS
dependency: the two-node adapter's shutdown SIGSEGV was traced to its
`__crayblas_shutdown` → profiling → `getenv` path (debug job 22033720).
The one-node results used the prior link and exited successfully; each
supplemental query records its actual binary hash. Neither change alters the
SSSP kernel. `to_galois.py` translates the canonical CSR into Galois
`.gr` version 1, preserving every vertex, arc, and integer weight. This adds
only four graph files to scratch. The unchanged Galois file reader plus the
independent output check validates the conversion for the measured queries.

```sh
bash benchmarks/build_gluon.sh
# Copy deps/bin/gluon_sssp into campaign/bin, then convert the four inputs
# with to_galois.py on a compute node.
sbatch -N 1 benchmarks/compare.sbatch /path/to/campaign --mode gluon \
  --graphs rmat20,mesh20,road-ny,youtube
sbatch -N 2 benchmarks/compare.sbatch /path/to/campaign --mode gluon \
  --graphs rmat20,mesh20,road-ny,youtube
```

Each mode independently tunes priorities {0, denominator/16, denominator} on
the two training sources. Zero is the upstream unprioritized setting. One node
uses outgoing edge cut; two nodes also try Cartesian vertex cut. Each rank
requests 16 Galois workers and 17 Slurm cores to allow communication, with one
rank per node. The allocation is exclusive. Galois applies its own thread
binding within the job's resource budget. The synchronous iteration cap is
raised so it must converge, and every measured result is checked.

After the discarded warmups and tuning, run both frozen Gluon modes plus ACIC
and its duplicate control on eight test sources, twice, in randomized order.
Report this as a separate paired experiment: its ACIC times come from the same
allocations as Gluon. `report_extra.py` exports these results, the independent
RMAT confirmation, and the preprocessing probe. It preserves incomplete or
failed adapter attempts in the compressed records rather than presenting them
as solver speedups. Full process logs remain on scratch; commit their hashes
and the compact structured measurements.

## Timed-output sensitivity

The primary ACIC build retains its existing `INFO_PRINTS`, including one
console line per controller round inside the timer. `build_quiet.sh` creates
a separate source copy and binary with only that definition removed, using
the same compiler flags and htram archive. It preserves the application
source and primary binary. After copying `acic_quiet` into the campaign bin
directory, run `--mode quiet --workers 16 --selection-job JOB` on one node.
Use a completed one-node, 16-worker primary job for the frozen RIKEN/GAPBS
choices. Both ACIC builds and both external baselines run on the same eight
sources with two randomized repetitions. This output sensitivity includes
asynchronous scheduling/work changes, so it cannot be subtracted as a constant
overhead from other allocations. `report_extra.py` emits `quiet.md`.

If the layout follow-up fails, `--mode finish_layout --selection-job JOB`
accounts for its remaining slots using the same frozen configurations. After
a variant fails validation, its remaining slots are explicitly `skipped`
without execution or a timing; other variants continue. Reporting preserves
actual allocation IDs, suppresses the failed variant's timing, and excludes
skipped slots from attempted-query totals. This cannot rehabilitate a failed
configuration or imply a measured failure rate over the unrun sources.

After all jobs finish, `archive.py CAMPAIGN OUTPUT` exports compact provenance
and hashes the full scratch logs. `render_report.py OUTPUT REPORT.md` refreshes
the generated tables in the decision report. Graphs, executable snapshots,
and full raw solver logs remain on scratch rather than entering Git.
