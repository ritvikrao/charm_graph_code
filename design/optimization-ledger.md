# ACIC optimization ledger

*Status at 2026-09-23. “Candidate” means the mechanism may appear in a current
performance configuration; it does not mean every graph enables it.*

## Current ledger

| Mechanism | What the evidence says | Disposition |
|---|---|---|
| Correct controller state, exact termination and independent digests | Earlier defects could produce plausible times with wrong control state. Current gates check serial references, queue conservation and multi-node envelopes. | **Required foundation.** Never bypass the correctness gates. |
| Flat CSR graph storage and graph retention across sources | Removes avoidable input and allocation cost; no algorithmic tradeoff observed. | **Keep.** |
| Compact eight-byte wire update | Reduces message traffic relative to the old 24-byte item and is part of all current candidates. It imposes 31-bit vertex and 32-bit distance limits. | **Keep, with scale blocker.** Add a wider large-scale format before extreme-scale runs. |
| Sender filtering and buffer sizing | Reduces updates that cannot improve a local distance and avoids poor aggregation sizes. | **Keep.** Use frozen defaults unless a new study targets communication. |
| Charm++ shared-memory transport | Required for efficient process-local delivery in the campaign runtime. | **Keep.** Runtime build requirement. |
| Destination lookup on receipt | Supports compact wire by recomputing ownership instead of sending it. | **Keep.** |
| Lazy heavy-edge relaxation | Repaired scale-free work growth when degree skew makes it applicable. The auto gate also requires skewed degrees. | **Keep behind `auto`.** |
| htram hold bitmap, idle flush repair and empty-delivery skipping | Step 8 removes unnecessary bucket scans/messages and is part of the stable base. | **Keep.** |
| Reader tiling / relabel placement | Exposes more process-local parallelism on sparse high-diameter inputs. It is unnecessary on dense inputs. | **Keep behind `auto`.** |
| Process-shared distances and bucket work | Enables cooperation inside a process and supports the shared priority queue. Dense graphs resolve this path inactive. | **Keep behind `auto`.** |
| Process-wide nearest priority queue | Greatly reduces road/mesh redundant work, but one-at-a-time removals cost too much. At 16 Frontier nodes on mesh it cuts attempts per edge from 18.1 to 4.0, with a 1.75–1.82× speedup over the local queue. | **Keep with batching.** Do not use the unbatched policy as a candidate. |
| Batched queue removal, batch 8 | Retains most ordering benefit and converts it to time improvement. It is in the accepted mesh candidate. At 16 Frontier nodes on mesh it adds 25–35% work but has a 1.15–1.24× speedup over unbatched. | **Keep.** Freeze at 8 for the current paper. |
| Live slack controller | Its measured settings did not beat strong fixed policies. On Frontier road, adding it to width 32K restores high work and loses to fixed width 128K. | **Do not claim or enable in the candidate.** Redesign later if live adaptation is the contribution. |
| Fixed process drain cap | Controls speculation and restores road scaling, but hurts mesh. Cap 5–9 is robust; 7 is the recorded representative. | **Keep as a road diagnostic/alternative.** It is not a universal policy. |
| Heap slice 8 | Increases scheduler/message interleaving. It carries the 896-PE mesh win and helps distributed road, though on one Frontier node its mesh speedup is 0.93–0.95×. At 16 Frontier nodes it halves mesh work (5.1–5.4 to 2.8 attempts per edge) with a 1.47–1.56× speedup. | **Keep in the mesh candidate.** Treat activation as graph/scale dependent. |
| Fixed bucket width 32768 | Cuts road work to about 1.5 attempts/edge but causes roughly 900–1,500 controller rounds. | **Mechanism evidence only.** Too fine for the candidate. |
| Fixed bucket width 131072 | Best road time measured, with roughly half the work of cap 7. Its speedup over GAPBS is still 0.66–0.69× at 8 nodes on training sources and 0.72–0.84× at 16 nodes on held-out sources (0.39–0.50× over Wasp), and it was selected using road training data. | **Keep as the road study point.** Do not make it a global default. |
| Node-level controller | Did not reduce road rounds or improve the measured result. | **Stop.** No further work without a new mechanism. |
| Spanning-tree controller broadcast | Frontier's runtime has `SPANTREE=ON`. Against a matched `SPANTREE=OFF` build (jobs 5538468/5538469) the tree lowers road round cost in both allocations by only 0.002–0.027 ms; the tree's speedup over flat is 1.09–1.11× on `w32k`, 1.005–1.04× on `w128k` and 0.99–1.01× on cap 7. Anvil's runtime has `SPANTREE=0`. | **Keep `SPANTREE=ON`; closed.** The broadcast is not the round-cost limit. The Anvil screen is optional replication. |
| Reconverse registered scheduler | Adds 30–45% road work/time relative to the old behavior. `+old-scheduler` on the same runtime reproduces the control. | **Reject for this campaign.** `+old-scheduler` is required. |
| Frontier send-cap, packet, matching, LCI-device, ASLR, huge-page, NUMA and fabric probes | None explains the whole-launch fast/slow modes. Work stays fixed and the slow tail trickles remote data. | **Closed negatives.** Do not repeat. |
| Frontier backend progress/polling | The remaining concrete hypothesis for launch bimodality. The modes are rare at 16 nodes but recur on `rmat27` (current-state §10). | **Not run** under the 2026-09-24 machine decision; still the bounded hypothesis. |
| RMAT profile at 16 Frontier nodes (2026-09-24) | PC samples on the solve window (`acic_prof.h` timer mode) plus a Projections trace of `rmat26`, 4 × 14 (jobs 5538732/5538734), and RIKEN's relaxation count on the same source. RIKEN sends 1.22B relaxations in 0.125 s; ACIC creates 1.81B updates plus 0.19B lazy tokens in 0.357 s, so ACIC does about 1.5× RIKEN's work at about 1.9× its cost per relaxation. The structural build (job 5538765) finds 89% of arrivals (1.61B of 1.81B) at targets already final, 1.33B of them at degree ≥ 128. No single hotspot dominates the samples: htram holds about 10%, lazy-range binary searches 8.6%, heap sifts about 8%, destination lookup 6%, glibc's TLS slow path 4.7%, runtime polling and progress 22%; `current_thresholds` is 11% of traced PE time and idle 16%, mostly an 80 ms tail. | **Evidence.** Directed the fixes below. |
| Precomputed lazy-range boundaries (`token_bounds`) | Replaces two binary searches per token release with a table built between sources; exact. At 16 nodes (jobs 5538752/5538753) its speedup over frozen is 0.95–1.07× on the source where it was active, inside the control spread of 0.94–1.11×; the prediction (1.03–1.12×) is not resolved. | **Keep (exact, harmless); no claim.** Carried in the hub-hint builds. |
| htram hold queues as a vector FIFO | Same jobs: 0.92–1.07× over frozen, within the control spread. | **No measurable effect; not in the candidate.** Committed in htram `d877a09` for later reuse. |
| Hub-distance hints (`--hub-hints D`), v1 | Vertices of degree ≥ D publish their distance once per round to a per-process table; senders drop updates no better than the published distance (exact: distances only fall). At 16 nodes, two allocations, all digests valid (jobs 5538859/5538861): D = 256 has a 1.20–1.27× speedup over frozen on `rmat25`, 1.28–1.31× on `rmat26` and 1.22–1.26× on `rmat27`, and cuts `rmat26` updates from 1.81B to 0.61B (D = 64: 0.36B, below RIKEN's 1.22B relaxations). `uniform25` loses 8–11% at every D, because every edge probes the table. Predicted 1.3–2.0× on RMAT: met only on `rmat26`. | **Replaced by v2** (auto gate, smaller table). |
| Hub-distance hints v2 (`--hub-hints auto`) | D = 256 where lazy-heavy's auto rule holds, off elsewhere; table of \|V\|/32 entries per process; one outbox lock per PE per round. At 16 nodes, two allocations, 960 valid solves (jobs 5538953/5538954), speedup over frozen: `rmat25` 1.25–1.26×, `rmat26` 1.30–1.34×, `rmat27` 1.23–1.30×, orkut 1.01–1.07×, `uniform25` 0.99–1.00× (auto resolves off). `rmat26` updates 1.81B → 0.68B. Table size is not the limit: 2^23 entries match the default and 2^19 loses 10–20% to dropped hints. Predictions: RMAT 1.30–1.60× met on `rmat26` only; orkut 1.05–1.40× missed; `uniform25` within the floor met. | **Candidate for the scale-free profile.** Needs held-out confirmation and the mesh/road regression check (auto resolves off there, so the expected effect is none). |
| Initial-exec TLS Reconverse build (`reconverse-linux-x86_64-tlsie`) | Same runtime sources built with `-ftls-model=initial-exec`, removing every general-dynamic TLS relocation from `libreconverse.so`, `liblci.so` and `liblct.so`; glibc's `__tls_get_addr` slow path was 4.7% of `rmat26` samples. Same jobs, hints v2 on both: its speedup over the production runtime is 1.00–1.07× (frozen-relative 1.27–1.33× `rmat25`, 1.35× `rmat26`, 1.26–1.33× `rmat27`, 1.04–1.10× orkut, 1.01–1.07× `uniform25`). | **Candidate runtime change.** Needs a mesh/road check before it replaces the production tree. |
| Delta road round attribution (2026-09-24) | Job 22354907: root controller 18.5–20.6 us/round versus 0.55–0.64 ms real rounds; quiet speedup 0.980–1.009× is within controls. The empty 267-long cycle averages 0.080–0.090 ms; solver work occupies 73–77% of PE time. Four-node 22355241 retains 26 valid digests and 12 empty-cycle gates despite a later profile stdout/parser failure: production 0.219/0.267 s, quiet speedup 0.969/0.984×. Empty 267-long mean 165–229 us at 16 × 7 versus 97–101 us at 8 × 15; real rounds 330–402 us. | **Reject logging optimization; investigate distributed collective/layout cost.** Eight-node attribution 22354948 pending with corrected per-rank profile capture. Four-node phase/work/trace incomplete. |
| Coalesced shared-heap wakeups (`ACIC_COALESCE_HEAP`) | Each controller round currently injects a new self-rescheduling heap callback even when one is pending. The prototype reuses it, preserving thresholds and slice/batch size. Local smoke and 224 two-node serial/work solves pass (22355072). Road A/B 22355092 gives 0.988/1.011× speedup: rounds fall from about 400 to 104 us, but their count grows from 688/842 to 2940/3109 and work rises about 7%. Continuation 22355144 passes 40 mesh solves and two road traces; backlog falls from 94 to 1 and controller p90 wait from 181 to 31 us. Mesh speedup is 0.864/0.842× (controls 1.002/0.992×), with 15–16× more rounds. | **Reject as a general optimization; default off.** No one-node road gain and a resolved mesh regression. Existing eight-node job is distributed mechanism evidence only; skip second-allocation/held-out acceptance trials for this version. |
| Range extension and coarse weight-derived widths | Overshoot on road and do not provide a useful window. | **Reject in current form.** |

## What belongs in the current candidate

The stable base includes compact wire, filtering/buffering, shared-memory
transport, lazy heavy relaxation and the Step 8 hold/flush changes. Sparse
high-diameter graphs may additionally activate reader tiling and process-shared
state. The accepted mesh profile adds nearest priority, batch 8 and heap slice
8. Runtime scheduling is fixed with `+old-scheduler`.

No measured live controller is currently part of the winning configuration.
The paper must distinguish static metadata gates, fixed graph-class tuning and
live feedback.

## Rules for adding another optimization

Add a mechanism only when it has:

1. a measured bottleneck and a prediction stated before the run;
2. a matched control that changes one causal factor;
3. a correctness/work audit and two allocation repetitions for acceptance;
4. a decision to keep, reject or defer after the bounded test.

Parameter sweeps that only seek a faster constant do not qualify as a new
mechanism. Stop tuning an arm once it is within the control floor or after one
neighbor on each side establishes the local trend.
