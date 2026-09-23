# Frontier campaign setup

Scratch root: `/lustre/orion/csc710/scratch/$USER/acic` (home quota is too small
for the inputs). Layout:

| Path | Contents |
|---|---|
| `deps/` | pinned sources (GAPBS `2972aeb`, Graph500-SSSP `552f156`, Galois `b67f942`), LLVMSupport 19.1.7 and fmt 10.2.1 in `prefix/`, the Wasp SC25 artifact (`wasp-ae/`, zenodo 15872863), `bin/` + `baselines-manifest.txt` |
| `venv/` | cray-python 3.11 + numpy/scipy (system python3 is 3.6; no module has scipy) |
| `campaign/raw/` | DIMACS USA and NY roads (`.gr`, `.co`), SNAP orkut and youtube; `download-sha256.txt` |
| `campaign/bin/` | graph tools and baseline binaries used by jobs |
| `campaign/graphs/` | `.wsg`, `.meta`, `.reference.txt`, `-z` relabelings (`.perm`), Galois `.gr`; `sha256.txt` |

## Steps

```sh
# 1. Sources and raw inputs (login node; see the commands in the setup session,
#    or clone the pinned revisions above into deps/ and download raw/ files).
# 2. Tools and baselines. Toolchain: PrgEnv-gnu 8.6.0, gcc-native 13.2,
#    Cray MPICH 8.1.31, boost/1.85.0 (gcc-13.2 build), no LibSci.
ACIC_BENCH_DEPS=$SCRATCH_ACIC/deps bash scripts/frontier/build_baselines.sh
cp $SCRATCH_ACIC/deps/bin/* $SCRATCH_ACIC/campaign/bin/
# 3. Inputs: step-7.5 graphs are checked against design/step75-data/graphs.json.
sbatch scripts/frontier/prepare_inputs.sbatch $SCRATCH_ACIC/campaign [names] [parallel]
# 4. Every baseline returns the reference digest, one and two nodes.
sbatch scripts/frontier/smoke_baselines.sbatch $SCRATCH_ACIC/campaign
```

Wasp and ds-rho are built in place under `deps/wasp-ae/wasp-ae/impl/`
(`-march=native` replaced by `znver3`). Both read the same `.wsg` files, but
neither prints the harness `BENCH` digest, and ds-rho cannot take a fixed
source. Each needs a small adapter, like `benchmarks/gap_driver.cpp`, before
it can join a timed, held-out-source comparison.

## Differences from Delta that the harness must absorb

- **Cores:** a Frontier node has eight 8-core L3 regions; Slurm reserves the
  first core of each (PUs 0, 8, ..., 56), leaving 56, with SMT off.
  `benchmarks/machine.py` holds the layout for both machines, and
  `launch_acic.sh`, `run.py`, `onenode_ab.py`, `onenode_gap_tune.py` and
  `work_cost_gap.py` take `-c`, `+ppn`, `+pemap` and thread counts from it.
  The reserved core is the OS core that Delta left free by hand, so Delta's
  8 x 15 becomes **8 x 7**: rank r gets `+pemap 8r+1-8r+7`. 4 x 14 and 2 x 28
  span whole regions (`1-7,9-15`), and 8 x 6 leaves one more core per process
  free. RIKEN runs a thread on every owned core and Gluon keeps one for its
  communication thread. Ranks per node must be 1, 2, 4, 8 or 56. Delta
  layouts are unchanged. `ACIC_MACHINE=delta|frontier` overrides detection
  (`LMOD_SYSTEM_NAME`). `scripts/frontier/check_affinity.sbatch` checks the
  binding on a compute node.
- **GAPBS settings** are machine-specific: re-select threads (at most 56)
  and delta with `onenode_gap_tune.py`, and pass them to `work_cost_gap.py
  --settings`. Its default is still Delta's frozen 128/64-thread choice, which
  it rejects here.
- **Memory:** 512 GB per node (Delta 256 GB).
- **Launch:** Cray MPICH wants each rank confined to one NUMA domain, or
  `MPICH_OFI_NIC_POLICY=ROUND-ROBIN`. LCI needs `PMI_MAX_KVS_ENTRIES` raised
  above Cray PMI's default of 30 for more than about five processes, and
  `--network=single_node_vni` on one node.
- **Charm++:** `$SCRATCH_ACIC/charm` is charmplusplus/charm at
  `reconverse-specific-build` (`f6c74074f`, the Delta pin), built as
  `CC=gcc CXX=g++ ./build charm++ reconverse-linux-x86_64 --with-production
  --enable-tracing --enable-shmem -j32` under PrgEnv-gnu, gcc-native 13.2,
  craype 2.7.33, cray-mpich 8.1.31, libfabric 2.3.1, xpmem, hwloc 2.13.0 and
  cmake 3.31.11. It autofetched LCI `ca88ce2c` (the Delta pin) and Reconverse
  `123313027faa` (current main, *not* Delta's `33b8c36be239`). The tree is
  separate from `~/charm_reconverse`, so the GPU builds are untouched. Point
  `CHARMC=` at `reconverse-linux-x86_64/bin/charmc`. Single-node LCI runs need
  `--network=single_node_vni`, which `machine.acic_srun_extra` adds.
- **Time limit:** one- to 91-node jobs are capped at two hours; the `debug`
  QOS allows one queued job per user.
