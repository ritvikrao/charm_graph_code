# ACIC configuration registry

*Current on 2026-09-23. This file records the configurations that may be used
for new results. Older build names and settings remain in the experiment
records only.*

## Identity rule

A binary name is a convenience, not a reproducible identity. Every result
must record this tuple:

```
application revision + htram revision + binary sha256 + Charm++ revision
+ Reconverse revision/build options + machine/toolchain + runtime arguments
```

Keep the generated manifest beside the binary and copy the tuple into the job
summary. Do not replace a binary in place during a campaign. `config.mk` is a
local convenience file and is never evidence of how a campaign binary was
built.

## Source and runtime pins

| Component | Current campaign pin | Notes |
|---|---|---|
| ACIC application | `5ab6d5b` | Source of the accepted mesh and current road candidates. |
| htram | `7db9c0a` | Must be rebuilt with the same graph and wire defines as the application. |
| Charm++ | `f6c74074f`, branch `reconverse-specific-build` | Production build with tracing and shared memory enabled. |
| Reconverse | `123313027faa` | Run performance jobs with `+old-scheduler`; see below. |
| LCI | `ca88ce2c` | Frontier campaign pin. |
| GAPBS | `2972aeb` | Includes the repository's input adapter and correctness checks. |
| RIKEN Graph500 SSSP | `552f156` | External distributed CPU baseline. |
| Galois | `b67f942` | External shared-memory baseline. |

The earlier v0916 control runtime uses Reconverse `33b8c36be239`. It is kept
only to attribute the scheduler regression; it is not the runtime for new
candidate results.

### Installed runtime trees used by recent jobs

| Machine / role | Runtime tree | Distinguishing setting |
|---|---|---|
| Anvil production | `~/charm_reconverse/reconverse-linux-x86_64-mpicxx-v0921-shm` | Reconverse `1233130`; cached `SPANTREE=0`; performance runs require `+old-scheduler`. |
| Anvil spanning-tree screen | `~/charm_reconverse/reconverse-linux-x86_64-mpicxx-v0923-span` | Same Reconverse source, configured with `SPANTREE=ON`; `acic_span` carries its own RPATH. |
| Anvil/Delta v0916 control | `~/charm_reconverse/reconverse-linux-x86_64-mpicxx-v0916-shm` | Reconverse `33b8c36`; attribution only. |
| Frontier production | campaign-local `reconverse-linux-x86_64` described in `scripts/frontier/README.md` | Charm++ `f6c74074f`, Reconverse `1233130`, LCI `ca88ce2c`, cached `SPANTREE=ON`; performance runs require `+old-scheduler`. |
| Frontier flat-broadcast control | campaign-local `reconverse-linux-x86_64-flat` (`./build ... --suffix=flat --with-cmake-args="-DSPANTREE=OFF -DAUTOFETCH_RECONVERSE_TAG=123313027faa..."`) | CMake cache differs from production only in `SPANTREE` and the pinned tag; used only by `acic_flat` for the spanning-tree screen. |

The repository may be newer than the application revision embedded in a
binary. For example, the documentation and Frontier harness are at repository
revision `48b6c2b`, while the accepted `acic_slice` application snapshot is
`5ab6d5b`. This is expected; cite both when the harness revision matters.

## Build profiles

| Profile | Binary names seen in logs | Definition and use |
|---|---|---|
| Production candidate | `acic_slice`, `sssp_smp` | Application `5ab6d5b`, htram `7db9c0a`, compact wire, `-O3`, production Reconverse `1233130`. Use for time. Frontier `acic_slice` has SHA-256 prefix `460c7b15`. Record the full local hash in every manifest. |
| Structural diagnostics | `acic_slice_diag`, `sssp_smp_diag` | Same sources and runtime, compiled with `ACIC_DIAG` and `VCOUNT`. Use for structural counters and per-round traces, not candidate timing. |
| Work-cost diagnostics | `acic_slice_cost_diag` and campaign-specific names | Same sources with `ACIC_WORK_COST` and communication counters. Use for attempts, queue operations and conservation checks. It is distinct from the structural diagnostic build. |
| Spanning-tree screen | `acic_span` | Byte-identical application sources and htram to the production candidate, linked by its own RPATH to Reconverse `1233130` configured with `SPANTREE=ON`. This is a pending mechanism screen, not an accepted build. |
| Frozen R0 control | `acic_r0_control` | Application source `598f13b`, rebuilt against the current campaign runtime and htram for regression attribution. Frontier SHA-256 prefix `3991df3d`. Do not treat it as the original R0 environment. |
| v0916 runtime control | names recorded in the layout experiment | Application and htram held fixed, linked to Reconverse `33b8c36`. Use only to check the scheduler effect. |

The Makefile defaults to the compact eight-byte update wire format. Compact
wire currently requires vertex IDs below `2^31` and distances below `2^32`.
The GAPBS reader also uses a signed 32-bit destination. These are hard scale
limits until the formats and readers are extended.

## Required runtime settings

- Add `+old-scheduler` to every performance arm built on Reconverse
  `1233130`. The registered scheduler changes road polling/order and adds
  30–45% work and time. Omitting the flag changes the algorithm's observed
  execution, so such a run cannot be pooled with the campaign.
- `benchmarks/launch_acic.sh` supplies `+ppn`, `+pemap` and, unless a probe
  overrides it, `+lci_ndevices 4`.
- Reconverse has no separate communication thread. The spare Anvil core is an
  OS core; `+commap` never assigned an ACIC communication thread.
- Frontier must use Cray PMI. Do not set Anvil's `ACIC_SRUN_MPI=pmi2` there.
  A one-node Frontier LCI run also needs `--network=single_node_vni`.

## Accepted and diagnostic algorithm profiles

All profiles below use process sharing `auto`, reader tiling `auto`, live slack
`off`, process queue `nearest`, queue batch 8 and `+old-scheduler`, unless the
row says otherwise. They are graph-class configurations, not a universal
default.

| Profile | Additional settings | Layout | Status |
|---|---|---|---|
| Mesh candidate | `--heap-slice 8` | Anvil: 8 nodes, 16 processes/node × 7 workers/process. Frontier: 16 nodes, 8 × 7. Both total 896 PEs. | Accepted on `mesh26-z`: four held-out sources, two allocations on each machine. |
| Road ordered | `--heap-slice 8 --bucket-width 131072` | Anvil: 8 nodes, 16 × 7. Frontier: 8 nodes, 8 × 7. | Best ordering mechanism measured; fixed-width diagnostic choice. Speedup over GAPBS 0.72–0.84× at 16 Frontier nodes (held-out), about 0.67× at 8 nodes. |
| Road capped | `--process-drain-cap 7`; no heap slice requirement | Anvil: 8 nodes, 8 × 15. Frontier has no exact 15-worker analogue; 4 × 14 was screened. | Strong fixed alternative. Similar time to ordered road with substantially more work. |
| Dense scale-free regression | High-diameter auto mechanisms must print inactive | Anvil: use the frozen gate's recorded layout. Frontier diagnostic suite used 8 nodes, 8 × 7. | Formal frozen-binary gate pending. Frontier 8-node timing is bimodal; classify launches with `benchmarks/launch_modes.py`. |
| Scale-free comparison (Frontier) | Same flags as the mesh candidate; everything high-diameter resolves inactive | 16 nodes: 4 × 14 for `rmat25`, `uniform25`, `rmat26`, `rmat27`; 8 × 7 for orkut, selected on training sources (job 5536460) | Held-out against RIKEN in jobs 5538389–5538392. Launches are almost all one mode at 16 nodes; 4 × 14 repeats spread about ±6%. |

Never promote bucket width 131072 to a global default. It was selected for
road's distance scale, and the current adaptive histogram does not derive it.

## Machine layouts

| Machine | Node view | Valid campaign layouts | Meaning |
|---|---|---|---|
| Delta / Anvil | 128 usable cores, eight 16-core NUMA domains | 8 × 15 and 16 × 7 are the main profiles | Each process owns a core stride and leaves one core for the OS. |
| Frontier | 64 physical cores in eight 8-core L3 regions; Slurm reserves PUs 0, 8, ..., 56 | 8 × 7 and 4 × 14; ranks/node must be 1, 2, 4, 8 or 56 | There are 56 usable cores. The reserved core in each region supplies the OS-core allowance. A 16 × 7 layout cannot fit. |

The authoritative layout code is `benchmarks/machine.py`. The Frontier
affinity gate is `scripts/frontier/check_affinity.sbatch`.

## Campaign locations and manifests

| Machine | Campaign root | Build record |
|---|---|---|
| Anvil | `/anvil/scratch/$USER/acic/campaign` | `bin/variants-manifest.txt` for private variants; `bin/build-manifest.txt` and `bin/baselines-manifest.txt` for the original campaign builds |
| Frontier | `/lustre/orion/csc710/scratch/rrao/acic/campaign` | the corresponding files under `bin/`, plus per-run `manifest.json` files under `logs/` |

`scripts/anvil/build_variant.sh` creates isolated application and htram copies,
installs the named binary, and appends its revisions, runtime tree, variables
and hash. `benchmarks/archive.py` exports log hashes and the initial build
manifest. Use these records rather than filesystem timestamps.

### Toolchains

- **Frontier:** PrgEnv-gnu 8.6.0, gcc-native 13.2, Cray MPICH 8.1.31,
  libfabric 2.3.1 and CMake 3.31.11. Charm++ configure options are
  `--with-production --enable-tracing --enable-shmem`.
- **Anvil campaign scripts:** GCC 11.2.0, Open MPI 4.0.6, libfabric 1.12.0,
  numactl 2.0.14 and hwloc 1.11.13. Record the loaded module list in each
  final campaign manifest instead of relying on this summary.

## Baseline settings currently accepted

| Machine / graph | GAPBS setting | Selection |
|---|---|---|
| Anvil `mesh26-z` | 64 threads, delta 4096 | Fastest held-out reference across the frozen training-selected settings. |
| Frontier `mesh26-z` | 56 threads, delta 4096 | Joint thread × delta search on training sources. |
| Frontier `road-usa-z` | 56 threads, delta 32768 | Joint thread × delta search on training sources. |
| Frontier `mesh24-z` | 56 threads, delta 4096 | Same search, job 5538410. |

| Machine / graph | Wasp setting (`bin/wasp_sssp`) | Selection |
|---|---|---|
| Frontier `mesh26-z` | 56 threads, delta 4096 | Joint thread × delta search on training sources, job 5536541 (`onenode_gap_tune.py --engine wasp`). |
| Delta `mesh28-z` | 128 threads, delta 4096 | Bounded 64/96/128 × 1024/4096/16384 training search, job 22378381; top-two confirmation, four held-out sources, three repeats. Thread-boundary winner. |
| Frontier `road-usa-z` | 56 threads, delta 32768 | Same job and search. |
| Frontier `mesh24-z` | 56 threads, delta 4096 | Same search, job 5538411. |

| Machine / graph (16 nodes) | RIKEN setting | Selection |
|---|---|---|
| Frontier orkut, `uniform25` | 8 ranks/node × 7 threads, delta 64 | `run.py --mode external`, layout then delta on training sources; same choice in jobs 5536474/5536475. |
| Frontier `rmat25`, `rmat26`, `rmat27` | 8 ranks/node × 7 threads, delta 16 | Same jobs. |
| Frontier `mesh26-z` | 8 ranks/node × 7 threads, delta 1024 | Job 5536476. Road-usa-z exceeds RIKEN's exact-distance range. |

Anvil road reference times are 0.117–0.142 seconds; consult the GAPBS tuning
record for the exact selected grid cell before another road acceptance run.
Do not infer baseline settings from the ACIC bucket width.

**Known RIKEN failure.** RIKEN aborts (an uncaught internal exception) at
delta equal to the weight denominator (`d1024` on the campaign graphs), at
whichever layout the search chose: 16 ranks per node on Delta and Anvil
(steps 7.6f1, 7.6n and 8), 8 on Frontier (job 5536474). It fails in seconds and
is not a hang. `run.py --mode external` records it as an invalid candidate and
selects among the valid deltas; keep those records rather than removing the
setting from the grid.

## Result checklist

Before accepting a cell, store:

1. Full component revisions, binary hash, runtime build options and module list.
2. Graph SHA-256, graph metadata, source role and reference digest.
3. Nodes, processes/node, workers/process, `+pemap`, scheduler and all ACIC
   policy arguments.
4. Warmup/repetition counts, arm ordering, job IDs, allocation count and every
   failed or excluded run.
5. Raw logs plus the parser version and a machine-readable summary.

If any item is missing, the cell is exploratory rather than paper evidence.


## Delta runtime refresh, September 24

The road round-cost investigation uses a new isolated production runtime;
it does not change the historical Frontier/Anvil identities above.

| Component | Revision |
|---|---|
| Charm++, latest `reconverse-specific-build` | `f6c74074f0c505d21d1c898836c1509d951dd44a` |
| Reconverse, latest `main` | `0c97c4d1f58dc5599b15a687cf328a7e05d60f90` |
| SSSP, upstream before attribution instrumentation | `7a4da59ebbf6094e716e327af34b3c6bbc98f33d` |
| htram, current upstream `master` | `7db9c0af69777b43e17dec110e6298fc09c91c54` |
| LCI, Reconverse's dependency pin | `ca88ce2c4b429ce72dfc3233fa21f8833299440c` |

Campaign: `/u/rao1/.tmp/road-rounds-20260924`. Runtime compiler:
`CAMPAIGN/charm/reconverse-linux-x86_64/bin/charmc`. Clean worktrees preserve
the original Reconverse checkout's uncommitted dependency edit. Configuration
is Release/production, tracing, shared memory, affinity, `SPANTREE=ON`, LCI
on and LCW off; GCC 14.2.1, CMake 3.30.1. All runs use `+old-scheduler`.
The exact command, cache settings and library hashes are in
`build/runtime-manifest.json`; each application's source snapshot and hash
are in `bin/*.manifest` and `build/*/source-files.sha256`.

`acic_latest` is the unmodified upstream application. `acic_quiet_rounds`
disables only two per-round progress-print blocks. `acic_round_profile_v2`
adds phase timers; `acic_cost` counts work; `acic_round_trace` traces the
solve. The unsubmitted `acic_round_profile` is superseded by v2, which resets
its counters for every source. `round_trip` measures an unloaded collective
cycle. `scripts/delta/build_round_probe.sh` reproduces the instrumented set
with an explicitly supplied runtime and source revision. Diagnostic timing
is not substituted for production timing.

The workspace `sssp_smp` is installed from `acic_latest`, and the ignored
`config.mk` points to the new compiler. Previous copies are preserved as
`bin/acic_previous_workspace` and `build/previous-workspace-config.mk`.


### Delta runtime refresh — September 25

Charm++'s current `reconverse-specific-build` tracking branch remains at
`f6c74074f0c505d21d1c898836c1509d951dd44a` after pull. Reconverse `main` was
fast-forwarded to `b30ad319503220bd1a2c0b5db38bd73dbe449c34`. No Charm++
branch switch was assumed; the newer `reviewed-with-reconverse` branch is
separate from this checkout's upstream.

A fresh build lives at
`/u/rao1/.tmp/runtime-refresh-20260925/charm/reconverse-linux-x86_64`.
The command used `--with-production --enable-tracing --enable-shmem`, with
Release, `SPANTREE=ON`, CPU affinity on, LCI on and LCW off. Generated headers
confirm `CMK_ERROR_CHECKING=0`, `CMK_TRACE_ENABLED=1`, `CMK_USE_SHMEM=1`;
release flags are `-O3 -DNDEBUG`. GCC is 14.2.1 and CMake is 3.30.1.

The existing Reconverse `CMakeLists.txt` edit was restored after pull: LCI
stays pinned to `dfb924cf3ee25aece37b85473b41d406ad810252`, rather than the
upstream default `ca88ce2c4b429ce72dfc3233fa21f8833299440c` used in the
September 24 campaign. This local dependency difference is recorded in the
manifest; do not describe this runtime as an unmodified upstream dependency
configuration. Reconverse also now defaults `+backend_poll_freq` to 64
(previously 4), including under `+old-scheduler`.

`/u/rao1/charm_reconverse/bin/charmc` and SSSP's ignored `config.mk` now point
to this build for subsequent compilation. The previous campaign runtime
and frozen binaries retain their original paths. This refresh only installs
the runtime compiler; the compatibility SSSP binary is isolated in the new
refresh directory.

Validation: 1,000 local two-PE collective rounds with the full 267-long
payload; CPU-affinity setup and Projections output; four tiny-graph serial
Dijkstra checks plus four distributed-certificate checks in local-only mode,
including a repeated source and a disconnected source. All use
`+old-scheduler`. These are local smoke checks, not new distributed performance
runs. Exact revisions, build command, CMake settings, preserved patch and
library hashes are in `runtime-refresh-20260925/runtime-manifest.json` under
`/u/rao1/.tmp/`; logs and validation commands are in its `checks/` directory.
The old compiler links and SSSP configuration are backed up there as well.
