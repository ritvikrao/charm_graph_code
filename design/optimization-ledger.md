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
| Process-wide nearest priority queue | Greatly reduces road/mesh redundant work, but one-at-a-time removals cost too much. | **Keep with batching.** Do not use the unbatched policy as a candidate. |
| Batched queue removal, batch 8 | Retains most ordering benefit and converts it to time improvement. It is in the accepted mesh candidate. | **Keep.** Freeze at 8 for the current paper. |
| Live slack controller | Its measured settings did not beat strong fixed policies. On Frontier road, adding it to width 32K restores high work and loses to fixed width 128K. | **Do not claim or enable in the candidate.** Redesign later if live adaptation is the contribution. |
| Fixed process drain cap | Controls speculation and restores road scaling, but hurts mesh. Cap 5–9 is robust; 7 is the recorded representative. | **Keep as a road diagnostic/alternative.** It is not a universal policy. |
| Heap slice 8 | Increases scheduler/message interleaving. It carries the 896-PE mesh win and helps distributed road, though it hurts one-node Frontier mesh by 5–7%. | **Keep in the mesh candidate.** Treat activation as graph/scale dependent. |
| Fixed bucket width 32768 | Cuts road work to about 1.5 attempts/edge but causes roughly 900–1,500 controller rounds. | **Mechanism evidence only.** Too fine for the candidate. |
| Fixed bucket width 131072 | Best road time measured, with roughly half the work of cap 7. Still 1.45–1.51× behind GAPBS and selected using road training data. | **Keep as the road study point.** Do not make it a global default. |
| Node-level controller | Did not reduce road rounds or improve the measured result. | **Stop.** No further work without a new mechanism. |
| Spanning-tree controller broadcast | Anvil's runtime cache has `SPANTREE=0`; early idle-round time grows roughly linearly with PE count. A matched `SPANTREE=ON` binary is built. | **One pending screen.** Accept only if two allocations show lower round cost without work/correctness changes. |
| Reconverse registered scheduler | Adds 30–45% road work/time relative to the old behavior. `+old-scheduler` on the same runtime reproduces the control. | **Reject for this campaign.** `+old-scheduler` is required. |
| Frontier send-cap, packet, matching, LCI-device, ASLR, huge-page, NUMA and fabric probes | None explains the whole-launch fast/slow modes. Work stays fixed and the slow tail trickles remote data. | **Closed negatives.** Do not repeat. |
| Frontier backend progress/polling | The remaining concrete hypothesis for launch bimodality. | **One bounded pending screen.** Test documented polling controls at 2 and 4 nodes; stop after the preregistered comparison. |
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
