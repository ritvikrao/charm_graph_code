# charm_graph_code

Asynchronous distributed graph algorithms in Charm++, over the
[htram](https://github.com/UIUC-PPL/htram) message-aggregation library.

Currently one kernel: **ACIC single-source shortest path** (`sssp_smp`). The
algorithm and the plan for the rest are in [design/sc27-plan.md](design/sc27-plan.md).

A non-SMP line, four CSV read-timing prototypes, and a stale copy of htram lived
here until September 2026; see [design/graphlib.md](design/graphlib.md) §1 for what
went and why. Recover any of it from history if it is ever needed.

## Build

`charmc`'s location is machine-specific, so put it in an untracked `config.mk`:

```
echo 'CHARMC_SMP = /path/to/charm_reconverse/bin/charmc' >  config.mk
echo 'HTRAM_DIR  = /path/to/htram'                       >> config.mk
make sssp_smp
```

Anything in `config.mk` can be overridden on make's command line. `make tools`
builds `graph_digest` and `graph_convert`, which need no Charm++ at all.

## Run

```
./sssp_smp <vertices> <path|edge count> <seed> <source> <mode> <p_tram> <p_pq> \
           [--verify] [--timeout <seconds>] [--bufsize <items>] \
           [--bucket-width <units>] [--round-delay <ms>] \
           [--flush-interval <rounds>] [--partition-jitter <percent>] \
           [--diag <prefix>] +ppn <threads>
```

| mode | input | argument 2 |
|---|---|---|
| 1 | uniform random | edge count |
| 2 | 2-D mesh | ignored; the edge count follows from the side length |
| 3 | RMAT / Kronecker | edge count. Needs a power-of-two vertex count |
| 4 | GAPBS `.sg` / `.wsg` | path. The vertex count is read from the file |
| 0 | comma-separated edge list | path. Legacy, read serially on PE 0 |

Modes 1, 2 and 3 generate the graph in memory, identically at any PE count and on
any machine, so nothing has to be staged. `--verify` solves the same graph with
serial Dijkstra in-process and compares an order-independent digest; it covers
every mode but 0.

Weights are integers in [1, 1000], a hash of the ordered endpoint pair and the
seed, so they do not depend on the order edges are read or generated in. An
unweighted `.sg` gets weights the same way.

## Graph inputs

`graph_convert` writes GAPBS `.wsg` files from the generators, and migrates the
legacy CSV files:

```
./graph_convert gen    3 16384 262144 1 rmat14.wsg
./graph_convert csv    graphs/mid_graph.csv 0 1 mid_graph.wsg
./graph_convert stat   rmat14.wsg
./graph_convert source 3 16384 262144 1      # a source vertex worth using
```

`source` matters more than it sounds. A third of an RMAT graph's vertices have no
out-edges, and a source with none never satisfies the convergence test — the run
sits until `--timeout`. It prints the lowest-numbered vertex of at least mean
out-degree, and the graph's degree summary alongside.

The files it writes are real GAPBS files, so GAPBS's own kernels can be run on the
identical input. Use GAPBS's `converter` for SNAP, DIMACS and MatrixMarket text.

## Gates

```
scripts/verify.sh                      # 18 configurations, one node
scripts/check_generator_portability.sh # graphs identical across toolchains
sbatch scripts/verify_2node.sh         # message-envelope invariants, two nodes
```

`scripts/verify.sh --update-golden` re-records `scripts/golden_digests.txt` after a
deliberate change to the graphs. The first two run in CI on every push.

## Measurements

```
scripts/diagnose.sh <h1|h2|h3|h4|all> [outdir]   # the step 6 experiment matrix
scripts/diag_report.py <outdir>                  # turn it into tables
sbatch scripts/diagnose_delta.sbatch all 20      # how the reported runs were taken
```

The four knobs above exist for these A/Bs and are inert at their defaults.
`--diag` writes the controller's own round-by-round series; `make sssp_smp_diag`
adds counters on the relaxation path — bucket occupancy, per-vertex arrivals,
reject rates by destination degree, per-PE idleness — which is why they are a
separate binary. Every wall-clock number comes from `sssp_smp` and every
structural number from `sssp_smp_diag`, deliberately.

What this was for, and what it found, is in
[design/scale-free-diagnosis.md](design/scale-free-diagnosis.md).
