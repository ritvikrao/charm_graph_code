# C6 on mesh: 8-node ACIC against one-node GAPBS on held-out sources

2026-09-22, recorded before submission. The 8-node slice test
([pilot](ipdps27-pilot.md)) found 8-node ACIC 1.20–1.39× faster than
tuned one-node GAPBS on mesh26-z's two training sources. This run tests
that on the four held-out sources, which neither system was tuned on.

## Settings, frozen from training

- **ACIC:** `acic_slice` (sha256 recorded in the audit). Batch 8,
  `--process-share auto --reader-tile auto --slack-control off
  --process-queue nearest --process-queue-batch 8 --heap-slice 8
  +old-scheduler`, at 8 nodes with 16 × 7 per node.
  - Layout and slice were chosen on training sources only.
  - The pre-slice best, 8 × 7 without a slice, runs alongside for context.
  - A repeated 16 × 7 slice 8 control measures noise.
- **GAPBS:** `gap_sssp` (`2972aeb`, sha256 `0e164aa3…`), one node. The two
  settings the training grid selected in the two one-node allocations are
  used: 128 threads with Δ 8192, and 64 threads with Δ 4096.

## Jobs

- **ACIC:** two 8-node allocations, each running
  `scripts/anvil/layout_check.sbatch` with `GRAPHS=mesh26-z SOURCES=4
  ROLE=test`. Arms: `n8_16x7_s8`, `n8_8x7`, `n8_16x7_s8_control`
  ([config](../benchmarks/c6-mesh-8n-variants.json)). Each source gets one
  warmup and three timed repetitions, with rotating arm order, and every
  solve is digest-checked.
- **GAPBS:** two one-node allocations, submitted at the same time
  (`scripts/anvil/gap_fixed.sbatch`, `gap_anvil.py --settings`). Each times
  both settings on the four held-out sources, with one warmup and three
  repetitions, digest-checked.

## Decision rule

- **GAPBS reference:** for each held-out source, the fastest median over
  both settings and all four one-node allocations. Those are the two
  one-node pilot jobs (20853639/20853641), which already timed the held-out
  sources, and the two new ones. Taking the fastest favours GAPBS.
- **Pass:** the per-source median of ACIC's `n8_16x7_s8` arm is at or below
  the GAPBS reference on every held-out source, in both ACIC allocations.
- **Prediction:** it passes, with ACIC at 0.70–0.90 of GAPBS's time. The
  existing held-out GAPBS medians are 0.336–0.376 s. The training ratio was
  0.72–0.83.
- **Failure on any source** is reported as a failure. There is then no C6
  claim for mesh from this setting, and settings are not re-tuned on
  held-out sources.

## Scope

- This is one graph class: high-diameter, low-degree meshes. Road remains
  1.9× behind GAPBS.
- **RMAT regression suite:** the `--process-share auto` rule turns process
  sharing on only below 8 edges per vertex. The five regression graphs have
  31–76 arcs per vertex (`rmat25`, `orkut`, `uniform25`, `rmat26`,
  `rmat27`), so the slice and cap are inactive on all of them and their code
  path is unchanged. The 16 × 7 layout is a per-graph-class tuning choice,
  like GAPBS's thread count. The regression suite still has to be run with
  this binary before R3 acceptance.

## Budget

- **Reservations:** 8 nodes × 10 min, 171 SU per ACIC allocation; 1 node ×
  15 min, 32 SU per GAPBS allocation; 406 SU at the caps.
- **Expected use:** about 110 SU.
- **Spend so far:** about 700 SU on Anvil since 2026-09-21, plus about
  190 SU on Delta, of the 2,000-SU R0–R2 cap.

## GAPBS reference (jobs 20866515/20866516)

- **Validation:** both one-node jobs digest-check every run.
- **Cost:** 4.8 SU.

Held-out medians of three repetitions, in seconds. The pilot jobs
20853639/20853641 timed 128/8192 in the first allocation and 64/4096 in the
second.

| Source | 128/8192: 20853639 | 128/8192: 20866515 | 128/8192: 20866516 | 64/4096: 20853641 | 64/4096: 20866515 | 64/4096: 20866516 | **Reference** |
|---|---|---|---|---|---|---|---|
| 35305828 | 0.358 | 0.358 | 0.361 | 0.346 | 0.346 | 0.344 | **0.344** |
| 44514593 | 0.393 | 0.391 | 0.496 | 0.376 | 0.378 | 0.378 | **0.376** |
| 41458868 | 0.344 | 0.446 | 0.344 | 0.336 | 0.335 | 0.335 | **0.335** |
| 21824001 | 0.372 | 0.370 | 0.375 | 0.366 | 0.365 | 0.365 | **0.365** |

- 64 threads with Δ 4096 is the faster setting on every held-out source.
  It reproduces within 0.5% across three allocations.
- 128/8192 has two slow cells (0.446 and 0.496 s). They do not affect the
  reference, which takes the fastest median.

## Result: pass (jobs 20866513/20866514, 2026-09-22)

- **Validation:** both 8-node allocations validate 48 of 48 held-out
  solves, with layouts verified and no runtime warnings.
- **Cost:** 49 SU.

Per-source medians of three repetitions, in seconds, with the ratio to the
GAPBS reference in parentheses:

| Source | GAPBS reference | ACIC 16 × 7 slice 8, A | ACIC 16 × 7 slice 8, B | Control, A / B | 8 × 7, no slice, A / B |
|---|---|---|---|---|---|
| 35305828 | 0.344 | 0.276 (0.80) | 0.276 (0.80) | 0.271 / 0.273 | 0.367 / 0.373 |
| 44514593 | 0.376 | 0.299 (0.79) | 0.303 (0.81) | 0.296 / 0.303 | 0.403 / 0.403 |
| 41458868 | 0.335 | 0.244 (0.73) | 0.237 (0.71) | 0.238 / 0.263 | 0.330 / 0.337 |
| 21824001 | 0.365 | 0.285 (0.78) | 0.301 (0.83) | 0.282 / 0.291 | 0.386 / 0.386 |

- **The rule passes.** 8-node ACIC is below the GAPBS reference on all four
  held-out sources in both allocations, at **0.71–0.83 of GAPBS's time
  (1.21–1.41× faster)**. Every timed repetition is also below the
  reference; the slowest is 0.316 s, against 0.365 s.
- **Prediction met:** it was 0.70–0.90.
- **The held-out ratio matches training** (0.72–0.83), so choosing the
  setting on training sources did not overfit.
- **The control arm agrees within 0–4%** on three sources, and within 11%
  on 41458868 in allocation B (0.263 against 0.237 s). The control also
  passes on every source.
- **The slice is what makes the win.** Without it, the 8 × 7 arm is at
  0.98–1.08 of GAPBS on held-out sources: parity, as on training.

**Claim supported (mesh class only):** on Anvil, 8-node ACIC with a
training-selected layout and a 8-entry drain slice solves mesh26-z SSSP
1.2–1.4× faster than one-node GAPBS Δ-stepping tuned over threads and Δ.
The claim holds on held-out sources, in two allocations for each system.

**Not yet a C6 pass overall.** C6 also requires:

- the RMAT regression suite (R2) with this binary, before R3 acceptance;
- road, which remains 1.9× slower than GAPBS.
