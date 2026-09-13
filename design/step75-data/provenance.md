# Provenance for the 2026-09-13 Delta pilot

The decision report is [step75-comparisons.md](../step75-comparisons.md).
Campaign: `/scratch/mzu/rao1/acic-comparison-20260913`; `/work/hdd/mzu/rao1`
aliases the same storage. Dependencies: `/u/rao1/acic-comparison-deps`.
Generation, references, verification, and measurements ran in CPU allocations
under account `mzu-delta-cpu`.

## Software and binaries

| Component | Revision |
|---|---|
| Measured ACIC solver source | `f29245e267abb44c898c774812a76d9a8110514d` |
| htram | `3ac5169c8b856c583e9d68c1d9ab49bb31045ae5` |
| Charm++ root | `360ee698bca0126ec802b07009ab53d954c40f04` |
| Reconverse nested checkout | `38091f1e950446ce6668560db5211ee356b5c2d7` |
| LCI configured runtime source under `_deps/lci-src` | `dfb924cf3ee25aece37b85473b41d406ad810252` |
| Separate auxiliary `charm_reconverse/lci` checkout (not the configured source) | `b8069dd44e8350db10cebb819d44c598814efd4d` |
| [RIKEN](https://github.com/RIKEN-RCCS/Graph500-SSSP) | `552f156297d921856b74f5c238a93bcbd361bb95` |
| [GAPBS](https://github.com/sbeamer/gapbs) | `2972aeb2703165bafd921222f4ed7196f542d3a8` |
| [Galois/Gluon](https://github.com/IntelligentSoftwareSystems/Galois) | `b67f94206a8c47fd414446621f6633a31c49fd98` |

The initial build manifest reports ACIC HEAD `3db0a0f` because the source was
still uncommitted at build time. The measured `sssp_smp.cpp` exactly matches
`f29245e`, verified by SHA-256
`af5d73c35f0aa43c90847be52f1e8025b798fd6fd0f5437c785945e78f29f0e2`.
All primary jobs use the same immutable ACIC/RIKEN/GAPBS binaries:

| Binary | SHA-256 |
|---|---|
| ACIC | `efbb50c3ab715b1289b20784920629c00326dc02d284deef82b5422e1d40b822` |
| RIKEN | `a41338a3bd5e5960876dd72c45251ccb3967f86261bc0131eb147ef188e17e1a` |
| GAPBS | `58afc2401444f066c6770517a528bf31613af50d047bcd71cbdfc7d73ff860f8` |
| Gluon corrected checksum, original link (22033417) | `6bedd1e10030e72c571582390ddc847fde41da9d96e62760a077c49dac964243` |
| Gluon without unused LibSci (22033834) | `36a8047b4260f52680bceff4d7800077f837885fc6091d3a54f4bdb0a288e9f8` |
| Quiet ACIC copy (22033982) | `25c01370da02397e92ba40c0b080dddff3d6ea17cc98d8d10e61b41c88a551f0` |

The quiet copy comes from [build_quiet.sh](../../benchmarks/build_quiet.sh),
which removes only `#define INFO_PRINTS`. Its source hash is
`1bfe4074e1534ae0bb16fe443aaf2b08c4b25777a9d3297518888541d61fd430`.
The production source and binary are preserved. Gluon's original valid binary
remains as `campaign/bin/gluon_sssp-v2`; the first rejected checksum adapter's
hash is retained in [gluon-adapter-v1.sha256](gluon-adapter-v1.sha256).

Builds use GCC 14.2.1, Cray MPI 9.1.0, libfabric 2.3.1, and CrayPE 2.7.36.
ACIC uses `charm_reconverse/bin/charmc`, resolving into
`reconverse-linux-x86_64-shmem`, with `-g -O3` and the existing graph/htram
defines. GAPBS uses `-O3 -fopenmp`; RIKEN uses Cray `CC`, `-O3 -fopenmp
-msse4.2`, vertex reordering 2, and the compatibility includes/page size in
[build.sh](../../benchmarks/build.sh). Gluon uses `-O3 -march=znver3`, installed
LLVM 19.1.7 support libraries, Boost 1.73, fmt, libnuma, and Cray MPI. Its final
link is [gluon-link-command.txt](gluon-link-command.txt). No fast-math flag is used.

The installed Charm/Reconverse runtime was reused, not rebuilt. Its CMake
cache explicitly points LCI at `reconverse-linux-x86_64/_deps/lci-src`, not
the separate `charm_reconverse/lci` checkout. Runtime checkout revisions and
[runtime-cmake-settings.txt](runtime-cmake-settings.txt) document those source
paths/settings; [acic-ldd.txt](acic-ldd.txt) and
[runtime-library-hashes.json](runtime-library-hashes.json) identify the actual
resolved Reconverse/LCI/LCT shared libraries. The executable hash alone does
not identify its dynamic runtime dependencies. The recorded runtime logs
report that the UCX registration cache is unavailable (`status=-22`) and
execution continues without that cache. This was preserved during the pilot;
there is no measured claim about the effect of enabling it.

`source-hashes.json` describes the final reproduction harness, not a claim
that every early job loaded that final Python revision. The harness was
extended in commits `f29245e`, `95f4b54`, and `68bd59b`, with subsequent commits
adding output sensitivity and reporting. The corrected primary timer parser
is already in `f29245e`. Every run preserves its actual command, source,
configuration, order, digest, and launch status. Later records include the
binary hash; earlier primary records use the initial manifest and immutable
binary snapshots for identity.

## Allocations

| Purpose | Job IDs |
|---|---|
| Input preparation, first failed / corrected | 22032511 / 22032565 |
| Python fixtures, one / two nodes | 22032608 / 22032690 |
| Two-node cross-implementation smoke | 22032609 |
| Existing 18 + 3 verification gate | 22032651 |
| Primary 16 workers/node, 1 / 2 / 4 / 8 / 16 nodes | 22032688 / 22032689 / 22032758 / 22032759 / 22032760 |
| Primary one-node occupancy, 64 / 120 workers | 22032761 / 22033411 |
| Bounded RIKEN presolve | 22032718 |
| Independent two-node RMAT confirmation | 22032818 |
| Initial Gluon build / graph conversion | 22032804 / 22032988 |
| Rejected Gluon checksum attempts | 22033282 / 22033283 |
| Gluon one-node valid / two-node teardown failure | 22033417 / 22033418 |
| Gluon debugger stalled / successful backtrace | 22033674 / 22033720 |
| Gluon two-node corrected link | 22033834 |
| ACIC one-node process geometry | 22033619 |
| Missing mesh query completion and failure replays | 22033692 |
| Full-node 8 × 15 comparison with frozen baselines | 22033887 |
| One-node timed-output sensitivity | 22033982 |
| Missing 120-worker RMAT slots and failure replays | 22034066 |
| Complete layout follow-up after fixed-policy failure | 22034120 |

Measurements request exclusive 128-core CPU nodes with all node memory.
The last short follow-up (22034120) was moved while queued from `cpu` to
`cpu-interactive`, retaining one exclusive CPU node and a 15-minute cap.
This is a supported queue in the [Delta running-jobs guide](https://docs.ncsa.illinois.edu/systems/delta/en/latest/user_guide/running_jobs.html).
Its final partition/resource accounting is retained alongside the other jobs;
the interactive partition has a higher billing weight, not additional workers.
Small build/preparation/verification and debugger jobs use shared partial
allocations; their times are not performance results. Core dumps are disabled.
Exact placement and `+lci_ndevices 4` are in each command. The harness sets
`PMI_MAX_KVS_ENTRIES=1000`, `FI_CXI_RX_MATCH_MODE=hybrid`, `NO_AFFINITY=1`,
`OMP_PLACES=cores`, and `OMP_PROC_BIND=close`; the rest is inherited from the
submission environment. Packet-size environment state was not archived per
early allocation; no packet-size sensitivity result is inferred.

Slurm accounting records physical resources and final states. A failed query
can fail its enclosing job; successful earlier cells remain valid and the
failed cell remains marked. Debugger job 22033720 exits zero because GDB
completed: its inferior's SIGSEGV is recorded in the output and is not a
successful solver run.

## Files and regeneration

- `runs.jsonl.gz` retains primary warmup/tuning/test records and the separately
  identified last query completed after the mesh timeout.
- `extra-runs.jsonl.gz` retains supplemental records, failed adapters, and
  replays. The completion query deliberately appears in both archives;
  count it once when totaling queries.
  The layout follow-up explicitly marks unrun slots of the failed variant as
  `skipped`; these are not attempted or timed queries. `comparison_job` groups
  completion records with the original experiment while preserving actual job IDs.
- `validation-runs.jsonl.gz` retains fixture/smoke checks. Smoke timing fields
  are not performance evidence because of the initial parser issue.
- CSV/Markdown tables reproduce estimates. `failed-test-runs.json` preserves
  the failure rather than silently averaging only successes. `counters.csv`
  gives medians of successful primary queries and explicitly records counts.
- Graph descriptions/hashes, references, source splits, frozen selections,
  job output/accounting, source/binary hashes, and revisions are committed.
  `scratch-log-manifest.json` locates and hashes full logs left on scratch.
  `build-output.jsonl.gz` additionally retains compiler/configuration logs
  from the dependency directory, including build job 22032804.

From the repository root:

```sh
python3 benchmarks/report.py /scratch/mzu/rao1/acic-comparison-20260913/logs design/step75-data
python3 benchmarks/report_extra.py /scratch/mzu/rao1/acic-comparison-20260913/logs design/step75-data
python3 benchmarks/counters.py /scratch/mzu/rao1/acic-comparison-20260913/logs design/step75-data/counters.csv
python3 benchmarks/archive.py /scratch/mzu/rao1/acic-comparison-20260913 design/step75-data
python3 benchmarks/render_report.py design/step75-data design/step75-comparisons.md
```

Primary reporting rejects incomplete cells and suppresses each failed variant
instead of computing a successful-only mean. Supplemental tables require
eight matched sources, two repeats, and the full variant set. Failed or
incomplete supplemental attempts remain archived separately; final validation
also checks that every planned successful supplemental table is present.
