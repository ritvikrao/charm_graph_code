# SSSP comparison pilot on Delta

This harness compares ACIC, RIKEN Graph500-SSSP, and GAPBS SSSP on identical
weighted inputs. It is a research pilot, not an official Graph500 or GAP score.
GAPBS is run only on one node. The distributed asynchronous Gluon baseline is
a separate follow-up; RIKEN supplies the distributed delta-stepping comparison.

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

* Uniform and RMAT use graphlib with 16 generated arcs per vertex, integer
  weights 1–1000, and seeds 1 and 2. Mesh uses the same weighted grid generator.
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
workers/node plus its communication core; OpenMP baselines use 16 workers/node.
RIKEN tries one rank × 16 threads and four ranks × 4 threads per node. ACIC
workers bind to cores 0–15 and its communication thread to core 16. Slurm binds
baseline ranks to cores; `OMP_PLACES=cores`, `OMP_PROC_BIND=close`, and
`NO_AFFINITY=1` keep upstream RIKEN affinity code from overriding that binding.
Record higher occupancy separately using `--workers`; the node budget remains
equal even though active communication cores differ.

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
downloaded or rebuilt. `to_galois.py` translates the canonical CSR into Galois
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
