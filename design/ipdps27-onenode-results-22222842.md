# Completed one-node campaign: jobs 22222842–22222847

Reviewed 2026-09-19. All six jobs are `COMPLETED`, exit `0:0`; the queue is
empty. **The measured candidate fails the high-diameter GAPBS target.** It
improves substantially over frozen ACIC and scales from one to eight nodes.
The complete two-allocation acceptance protocol remains incomplete.

## Provenance and validation

Campaign: `/work/hdd/mzu/rao1/acic-ipdps27-onenode-20260919`, with logs and
binaries linked to `/u/rao1/.tmp/ipdps27-onenode`. These are Delta measurements
on EPYC 7763 nodes; do not merge absolute times with the earlier machine's
208xxxxx jobs. ACIC uses eight processes of fifteen workers per node, 120
workers at one node and 960 at eight nodes.

| Job | Measurement | Allocation | Elapsed |
|---|---|---|---|
| 22222842 | Four high-diameter graphs, eight-node candidate/frozen/control | cn[030-031,048,053,080,091,100,110] | 21m20s |
| 22222843 | Matching one-node comparison | cn065 | 43m25s |
| 22222844 | Second GAPBS allocation, selections frozen from 22218622 | cn043 | 1m32s |
| 22222845 | GAPBS joint tuning and held-out road-usa-w4-z | cn029 | 15m56s |
| 22222846 | RMAT auto/off isolation | cn041 | 3m28s |
| 22222847 | Independent allocation of RMAT isolation | cn083 | 3m31s |

Candidate: `acic_reader_final`, SHA-256
`b6239bf7ce75fc41de1fd768f1991cbe606778833e36c84858c1cdb76a809619`, flags
`--process-share auto --reader-tile auto --slack-control off`.
Frozen and repeated control: `acic_frozen`, SHA-256
`5913beb1949a9274beb7c7b6ebe279f6f1fd6211fe8fd138a1bccb46226f4abd`.
The candidate binary, flags, and per-node layout match between node counts.
No default has been changed.

All **512 ACIC solves**, including warmups, were rechecked against independent
reference digests (both hashes, reachable count, distance sum), raw solve
times, unique source/repetition cells, saved medians, and binary hashes.
No stall, rescue, conservation, truncation, or verification failure was found.
Each graph/variant has four held-out sources, three timed repetitions and
one warmup per source. The A/B runs use separate launches, not batch mode.

The compressed GAPBS raw logs were matched to every JSON record, raw time,
source, and digest: 549/549 valid in 22218622, 48/48 in 22222844, and
178/183 in 22222845. The five unsuccessful weight-scaled road runs exceeded
the 60-second launch cap during training at delta 2097152 with 1, 2, or 4
threads. The collector labels these `hang`; a timeout does not establish
deadlock. They remain recorded as failed search cells. All 48 held-out timed
runs in the two new GAPBS jobs passed, as did their 16 warmups.

Per-source medians, ratios, configuration manifests, record counts and input
JSON hashes are preserved in
[followup-22222842.json](onenode-data/followup-22222842.json).
Original ACIC data are `logs/AB-GRAPH-Nn-JOB/{manifest.json,runs.jsonl,summary.json}`
and their `.out` files. GAPBS data are `logs/external-1n-120w-JOB.jsonl`,
the corresponding `.log.gz`, and `*-GRAPH-selected.json`.
`benchmarks/onenode_assess.py` reproduces the within-allocation paired A/B
statistics; `check_onenode_digest.py` checks each ACIC raw log.

## High-diameter result

Seconds below are medians of the four per-source medians. Ratios divide
corresponding physical-source medians before taking the median or maximum
across sources. They need not equal the ratio of the displayed aggregate
times. Warmups are excluded. Lower ratios are better.

| Graph | ACIC 1 node (s) | ACIC 8 nodes (s) | GAPBS 1 node (s), first / second allocation | ACIC 8 / GAPBS, first / second | Worst source, first / second | ACIC 8 / ACIC 1 |
|---|---:|---:|---:|---:|---:|---:|
| mesh24-z | 0.939825 | 0.368624 | 0.097570 / 0.141174 | 3.697 / 2.623 | 3.952 / 3.748 | 0.395 |
| mesh26-z | 3.199109 | 1.003378 | 0.415810 / 0.405541 | 2.481 / 2.477 | 2.689 / 2.629 | 0.308 |
| road-usa-z | 1.257330 | 0.730424 | 0.174827 / 0.199531 | 4.117 / 3.525 | 4.284 / 3.698 | 0.587 |
| road-usa-w4-z | 1.199264 | 0.744314 | 0.200055 / missing | 3.575 / missing | 3.879 / missing | 0.620 |

First GAPBS allocation is 22218622 for the first three graphs and 22222845
for the weight-scaled road; second is 22222844. Fixed selected settings are
120 threads/delta 4096 (mesh24), 128/4096 (mesh26), 64/32768 (road), and
64/8192 (weight-scaled road). The latter's joint optimum is interior to both
search axes. Its weights are scaled, so its solve times and distances are
not interchangeable with native-weight road results.

Mesh24 GAPBS's aggregate time increased about 45% between allocations, but
both comparisons miss the median <= 1.0 and worst-source <= 1.2 targets by
a wide margin. Neither averaging the allocations nor selecting the slower
one rescues the result. One-node candidate ACIC is also 5.6–9.7 times slower
than the first GAPBS allocation, by median paired ratio.

Relative to frozen ACIC in the **same allocation**, improvements are large:

| Graph | Candidate / frozen, 1 node | Candidate / frozen, 8 nodes |
|---|---:|---:|
| mesh24-z | 0.164 | 0.245 |
| mesh26-z | 0.091 | 0.146 |
| road-usa-z | 0.122 | 0.281 |
| road-usa-w4-z | 0.105 | 0.307 |

These A/B ratios first pair repetitions and then aggregate source medians,
as `onenode_assess.py` does. Repeated frozen controls show considerable
source-level variability, especially at one node (worst control/frozen
1.51 on mesh24). The improvements are much larger, but there is still only
one candidate allocation per node count. The two GAPBS allocations are not
two independent candidate allocations, and RMAT24 isolation does not replace
the five-graph, eight-node regression suite.

## Dense-graph isolation

Both RMAT jobs compare the same current binary with all three features
explicitly off or all three set to auto, plus frozen and repeated frozen
control. The logs confirm that sharing and slack resolve off; dense-graph
reader auto resolves to no tiling. This **all-auto arm differs from the
high-diameter candidate**, which explicitly disables slack.

| Allocation | Control / frozen | Current off / frozen | Current auto / frozen | Auto / off |
|---|---:|---:|---:|---:|
| 22222846 | 1.027 | 1.023 | 1.087 | 1.062 |
| 22222847 | 0.959 | 1.029 | 1.086 | 1.094 |

These are repetition-paired ratios. Dividing source medians instead gives
auto/off 1.076 and 1.067: the direction persists under either summary.
This is repeat evidence of a mode-dependent cost, not proof of its cause.
Source noise is nontrivial, and the all-off build has not earned a blanket
no-regression claim. In particular, one allocation's all-off worst paired
source is 1.069.

`process_share_active()` and `slack_control_active()` currently evaluate
mode and graph density when called, including hot solve paths. Resolving
these once is a concrete hypothesis to test. Isolate the flags (especially
the exact candidate flags) before assigning the cost to a particular check,
and require a before/after A/B before adopting a fix.

## What this changes

The implemented L1/L2/L3 sequence did not close the one-node gap. Earlier
diagnostics show participation improving sharply while substantial rework
remains. Those diagnostics had slack **on**, whereas this candidate has it
**off**: they motivate investigation but cannot explain this candidate's
remaining time quantitatively. Runtime overhead, queue/locking costs,
priority order and redundant relaxations remain entangled.

The current performance-led paper is **no-go as-is under the author's C6
requirement**. The next work is the bounded attribution and conditional
intervention in [the revised plan](ipdps27-onenode-gap.md), not automatic L4,
another broad sweep, or larger graphs substituting for these failures.
