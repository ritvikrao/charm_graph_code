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

## Failures and resolution floors (step 7.6h)

A median over valid runs scores "stops sometimes" as a clean win, and a
`control` arm's ratio against its twin -- the resolution floor -- was ~1.00x at
one node and 1.16x-1.28x at sixteen. So no ratio in any table is printed
without both.

`outcomes.py` classifies every record as `ok`, `hang`, `wrong` or `crash`, and
counts rescued runs separately (a rescued run finished, but would have hung).
Records written since 7.6h carry `hung`, `outcome`, `stall_rescues` and
`skew_top_arrivals` from the solver's own output; older ones are classified by
the harness kill (-999) or by a compute time that reached their `--timeout`.

`report.py` splits every `FAIL` by kind and adds a *graph / allocation* floor
column to the primary table. `report_arms.py` renders any campaign made of
named arms:

```sh
report_arms.py CAMPAIGN/logs OUT --pattern 'policy-*.jsonl' --phase policy \
    --baseline adaptive --control control
report_arms.py CAMPAIGN/logs OUT --pattern 'width-*.jsonl' --phase width \
    --baseline logv-frozen --control control
```

Every cell is a paired median (baseline time / arm time, above 1 favours the
arm) with its win count and outcome counts, judged against floors measured in
*that* allocation: unmarked clears the allocation floor (worst graph); `~`
clears only the graph's own; `·` is within the floor; `†` means the cell or its
baseline did not always finish, so the ratio is over survivors and is not a
speedup. `arms.md` ends with a failure-rate table by arm and allocation;
`arms.csv` has every number.

## Admission x delivery (step 7.6f2)

`--mode policy --workers 120 --acic-rpn 8` runs, per allocation:

* `global-fixed`: four fixed candidates (width rule `logv|weight` x flush
  interval `1|5`, no coarsening, no idle flush), chosen once on the tuning
  sources of `--dev-graphs` (default `mesh20,rmat20,uniform20`) and frozen;
* `tuned-fixed`: the step 7.5 fixed search space, chosen per graph on that
  graph's own tuning sources;
* the 2 x 2 matrix, every arm on the shipped width rule and per-graph map:
  `adaptive` (shipped), `adapt-admission` (coarsening on, flush every round, no
  idle flush), `adapt-delivery` (no coarsening, shipped flush policy),
  `fixed-both`; and `control`, `adaptive` under another name.

All seven run in random order on each held-out (source, rep). Failures are
recorded and the campaign continues. Results on the development graphs are
in-sample for `global-fixed`.

## Anvil

Anvil CPU nodes have Delta's shape (128 cores, eight 16-core NUMA domains), so
the 8 x 15 layout is unchanged; `scripts/anvil/compare.sbatch` and
`scripts/anvil/repro_stall.sbatch` carry the differences (account, modules,
`SLURM_MPI_TYPE=pmi2`). All nine prepared graphs were regenerated there and
match the SHA-256 in `design/step75-data/graphs.json`, so the recorded
reference digests apply unchanged. Anvil's messaging path has been seen to
slow ~100x for minutes at a time independent of configuration; interleave
arms and read paired medians, never single runs.

## External re-take on Anvil (step 7.6f1)

`scripts/anvil/build_baselines.sh` builds RIKEN, GAPBS and Gluon at the
revisions above, with the same adapters and flags, into
`$ACIC_BENCH_DEPS/bin`. Every system uses ACIC's toolchain there: GCC 11.2
and OpenMPI 4.0.6 (UCX, `OMPI_MCA_pml=ucx`, launched through `srun` with
PMI2). Anvil has no LLVM or fmt module, so the script builds LLVMSupport 19.1.7
(RTTI on) and fmt 10.2.1 from release tarballs whose hashes it records.
`scripts/anvil/prepare_gluon.sbatch` writes the `.gr` copies.

`--mode external` lets each system choose its own layout on equal nodes,
using one sizing rule: a process gets its 128/rpn-core stride and runs one
fewer thread, which is ACIC's 8 x 15.

| System | Layout candidates | Parameters, at the chosen layout |
|---|---|---|
| ACIC adaptive | 8 x 15, 16 x 7 (`--acic-layouts`) | none |
| RIKEN | 4, 8, 16 ranks/node (`--riken-layouts`) | four deltas |
| Gluon-Async | 1, 8 ranks/node x {oec, cvc} beyond one node | priority {0, d/16, d} |
| GAPBS (one node) | 16, 64, 127 threads (`--gap-threads`) | four deltas |

`tuned-fixed` (7.6f2's question) and Gluon-Sync (behind Async in every 7.5
cell) are available through `--external-arms`, off by default. ACIC processes
stay inside a 16-core NUMA domain; the baselines may span domains. The search
has two stages per system: every layout at default parameters (mid delta for
the baselines) on the first tuning source, then the parameter grid at the
winning layout on both. It is a coordinate search with the same shape for
every system, not an oracle. The chosen configurations then run on the
held-out sources in random order, with `control` (the chosen adaptive
configuration again) as the floor: about 40 launches per graph, 53 on one node.
A system with no valid candidate is recorded in `*-selected.json` and left
out, as RIKEN is on road-usa, whose distances exceed its exact binary32 range.
`--mode external_smoke` runs every layout once and stops at the first invalid
digest.

```sh
sbatch -N 1 --time=01:30:00 scripts/anvil/compare.sbatch CAMPAIGN --mode external \
    --workers 120 --timeout 180 --sources 4 --reps 1 \
    --graphs mesh24,rmat25,road-usa,orkut
report_arms.py CAMPAIGN/logs OUT --pattern 'external-*.jsonl' --phase external \
    --baseline adaptive --control control
```

In `arms.md` an external arm's ratio is adaptive time / system time, so a value
above 1 means the external system is faster.

## Projections traces (step 7.6i)

`make sssp_smp_projections` relinks the solver with `-tracemode projections`;
only the link changes, so the traced binary runs the same objects as the timed
one. Stage it as `CAMPAIGN/bin/acic_prj`. The solver calls `traceBegin()` when
compute starts and `traceEnd()` when it ends, so `+traceoff` restricts the logs
to the solve, and `+traceroot` keeps them on scratch -- 240 PEs of rmat25
write about 370 MB.

```sh
make sssp_smp_projections && cp sssp_smp_projections CAMPAIGN/bin/acic_prj
sbatch -N 2 scripts/anvil/trace_projections.sbatch CAMPAIGN rmat25 23077392 8
```

The job runs the same source untraced and traced in one allocation (U T U T U)
so tracing overhead is visible, and then reports both traces. Anvil has no Java
for the Projections GUI, so `projections_report.py` reads the text logs: time
per entry method (exclusive of nested calls), idle, pack/unpack and the
remainder outside any entry method; the spread across PEs and processes; a
utilization timeline; and message counts with send-to-execute latency by
locality. Each Reconverse process starts its own clock, so per-process offsets
are estimated from the smallest delay in each direction before latencies are
reported; treat a cross-process median as hold-and-flush delay, not as network
time.

```sh
projections_report.py CAMPAIGN/traces/RUN/traceA/acic_prj \
    --pes-per-process 15 --processes-per-node 8 --jobs 64
```

## Per-edge cost: counters and interleaved A/Bs (step 7.6j)

```bash
module load papi/6.0.0.1
make sssp_smp                    # WIRE=compact is the default; WIRE=wide for the old layout
make sssp_smp_papi               # acic_prof.h: counters and cycle sampling
cp sssp_smp sssp_smp_papi /anvil/scratch/$USER/acic/campaign/bin/   # rename as the scripts expect

# Profile (LIBPFM_FORCE_PMU is set inside; papi/6.0.0.1 predates Zen 3)
EXTRA="--bufsize 6144 --send-filter-bits 17" RUNS="warmup plain sampled plain" \
  sbatch -N 2 scripts/anvil/papi_profile.sbatch $ROOT rmat25 23077392 8 BASE PROFBIN

# Interleaved variants; FLAGS use commas, NAME=VALUE tokens become environment
VARIANTS="a=acic b=acic_j:--bufsize,6144,--send-filter-bits,17" ROUNDS=3 \
  sbatch -N 2 scripts/anvil/ab_compare.sbatch $ROOT rmat25 23077392 8
WORKERS=112 ... ab_compare.sbatch $ROOT mesh24 14369093 16   # 16 x 7 layouts
```

Counters make this runtime switch context ten times as often and solve two to
three times slower: use a profile for where cycles go, never for how long a run
takes. `benchmarks/lci_stats_report.py` reads the per-thread `LCI_STATS` lines
of an instrumented `libreconverse.so` preloaded with `LD_PRELOAD` (the binaries
use `DT_RPATH`, so `LD_LIBRARY_PATH` cannot substitute it).

## Buffer size, shared memory and the 7.6n re-take (steps 7.6k-n)

Solver defaults since 7.6k ([design/step76-klm.md](../design/step76-klm.md)):

- `--bufsize-policy acceptance`: the run starts at 256 items per unit of
  average degree (within `--bufsize-range`, default 512:6144), and the
  controller moves the size when the share of arrivals that improve a
  distance puts it 2x away (`--bufsize-acc-items`, default 288). An explicit
  `--bufsize N` without a policy still means a fixed size.
- `--send-filter auto`: the 7.6j filter at 17 bits, on while the buffer size
  is at least 2048. `--send-filter-bits N` keeps it on for the whole run, and
  `--send-filter-bits 0` or `--send-filter off` turns it off.

ACIC runs from 7.6l on use a Charm++ tree built with `--enable-shmem`:

```sh
cd ~/charm_reconverse
./build charm++ reconverse-linux-x86_64 mpicxx --with-production --enable-shmem --suffix=v0916-shm -j16
```

`build` repoints `~/charm_reconverse/{bin,lib,include}` at the tree it just
built; point them back if other binaries rely on them.
`scripts/anvil/build_variant.sh NAME TREE [make variables]` builds one ACIC
binary into `CAMPAIGN/bin/NAME` from private copies of this tree and htram,
so variants for `ab_compare.sbatch` do not overwrite each other, and records
it in `CAMPAIGN/bin/variants-manifest.txt`. Flags reach htram's compile only
through `CHARMC_SMP` (for example `CHARMC_SMP="TREE/bin/charmc -flto=8"`),
not `OPTS`.

7.6n reused the 7.6f1 command unchanged; only `bin/acic` changed (the
7.6f2 binary is kept as `bin/acic_7.6f2`):

```sh
report_arms.py CAMPAIGN/logs CAMPAIGN/report-external-76n \
    --pattern 'external-*-2077061[25].jsonl' --phase external \
    --baseline adaptive --control control
```

## Scaling and communication shares (7.6o, step 8)

rmat27 (134M vertices, 4.2B edges, about 86 GB to prepare) is built on
request, with its Galois copy:

```sh
OMP_NUM_THREADS=64 sbatch scripts/anvil/prepare_large.sbatch CAMPAIGN rmat27
```

`THEN_COMM_SHARE=N` makes `compare.sbatch` follow an external run with
`run.py --mode comm_share` in the same allocation. That mode reruns ACIC's
and RIKEN's chosen configurations on N test sources, plain and timed,
interleaved. The timed builds are:

- `bin/acic_comm`, built with `-DACIC_COMM_SHARE` in `CHARMC_SMP`. It prints
  `COMM_SHARE ... compute_share= send_share= other_share=`.
- `bin/riken_sssp_mpit`, the RIKEN driver with `MPI_Pcontrol` marks around
  the solve, run under `LD_PRELOAD=bin/mpi_share.so`
  (`benchmarks/mpi_share.c`). It prints `MPI_SHARE ... share=`.

Gluon's `Sync_SSSP_0` and `Timer_0` statistics are parsed from every run.
The 7.6o commands:

```sh
THEN_COMM_SHARE=2 sbatch -N 2 --time=01:45:00 scripts/anvil/compare.sbatch CAMPAIGN \
    --mode external --workers 120 --timeout 300 --sources 4 --reps 1 --graphs rmat26,rmat27
THEN_COMM_SHARE=2 sbatch -N 8 --time=01:45:00 scripts/anvil/compare.sbatch CAMPAIGN \
    --mode external --workers 120 --timeout 300 --sources 4 --reps 1 --graphs orkut,rmat25,rmat26,rmat27
report_scaling.py CAMPAIGN/logs 20770612 20770615 20776355 20776356 20776357
```

`--mode comm_share --selection-job JOB` reuses another allocation's choices
at the same node count; that is how the 1-node debug smoke test ran.

### Step 8 tools and switches

`scripts/anvil/step8a.sbatch` runs one allocation of any of three parts
(`PARTS`):

- **diag:** one run of a counter build per graph (`DIAG_BIN`, `DIAG_FLAGS`).
  `STEP8A` lines give expansions, heavy updates per heavy edge, and
  arrivals at final vertices, overall and by degree.
- **riken:** RIKEN's relaxation count (`bin/riken_sssp_verbose`, built by
  `build_baselines.sh` from `benchmarks/riken_count.patch`), which prints
  `RELAX_SENT`.
- **arms:** interleaved arms `LABEL:BINARY:TRAM:HEAP:FLAGS`. A binary named
  `riken_sssp` runs RIKEN, and every ACIC run writes a `rounds.csv`.

```sh
ARMS="new:acic:0.999:0.005: old:acic_76n:0.999:0.005: riken:riken_sssp:0:0:" \
GRAPHS="rmat25:26007212:16:16 orkut:2549343:8:64" PARTS=arms ROUNDS=3 \
  sbatch -N 8 scripts/anvil/step8a.sbatch CAMPAIGN
```

`papi_profile.sbatch` takes `WORKERS=112` for 16 × 7 layouts. The
communication-share build also prints `idle_share`.

The solver switches added in step 8 are below.
[design/step8-scaling.md](../design/step8-scaling.md) has the measurements
behind each default.

| switch | default | what it does |
|---|---|---|
| `--lazy-heavy off\|on\|auto\|L` | auto | light edges relax at once; heavier ranges wait as tokens queued at d + L G^j. auto: average degree ≥ 8 only |
| `--lazy-heap P` | 0.95 | heap percentile while lazy relaxation is active |
| `--lazy-growth G` | 2 | token range growth; 4–8 help at 2 nodes, 2 wins at 8 |
| `--idle-flush-interval auto\|us` | auto | least time between two idle flushes that sent something: 30 µs at degree ≥ 8, 100 µs below |
| `--control reduction\|node` | reduction | node: controller messages on the node queue (slower; kept for reference) |
| `--control-interval ms` | 0.25 | least time between broadcasts under `--control node` |
| `--warm-links on\|off` | off | one message to every other process before the solve |

htram additions: a per-destination hold bitmap (always on),
`setIdleFlushInterval`, and `setSkipEmptyDeliveries`. ACIC turns the
latter on at average degree ≥ 8.
