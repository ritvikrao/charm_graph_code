# Step 7.6i: where ACIC's time goes on rmat25 (Projections)

*2026-09-15. Why [7.6f1](step76-external.md) found ACIC 6.9x behind RIKEN on
rmat25. Traces of one configuration, taken with the runtime's own
instrumentation and read without the Projections GUI.*

## What was run

Job 20751377, two Anvil nodes, `rmat25`, source 23077392, 8 processes x 15
workers per node -- the layout ACIC chose for itself in 7.6f1, and the same
launcher, flags and environment `run.py` uses. The allocation ran the same
source six times: a warmup, then untraced, traced, untraced, traced, untraced.

- **Build.** `make sssp_smp_projections` relinks the timed binary's objects
  with `-tracemode projections`; the sources are the 7.6f1 ones
  (`sssp_smp.cpp` b732dbdb..., htram f463940). `charm_reconverse` is a
  production build: `-optimize -production`, CMake `Release` (`-O3 -DNDEBUG`),
  `CMK_ERROR_CHECKING 0`, and Reconverse itself `-O3 -DNDEBUG`. Tracing is
  compiled in but inert until a `-tracemode` library is linked.
- **Window.** The solver already calls `traceBegin()` when compute starts and
  `traceEnd()` when it ends, so with `+traceoff` the logs cover the solve and
  not the load.
- **Cost of tracing.** Solve seconds in allocation order: 1.419 untraced,
  **1.387 traced**, 1.379 untraced, **1.360 traced**, 1.301 untraced. The
  traced runs sit inside the untraced spread, so the breakdown below is not
  measuring its own instrumentation. Every run matched the reference digest.
- **Reading the logs.** Anvil has no Java, so
  [`benchmarks/projections_report.py`](../benchmarks/projections_report.py)
  parses the text logs directly. Each Reconverse process starts its own clock
  (`Cmi_startTime`), so it estimates per-process offsets from the smallest
  message delay in each direction before reporting cross-process latency.

## Where the time goes

240 PEs x 1.386 s = 332.7 PE-seconds (trace A; trace B is the second traced
run, 326.2 PE-seconds over 1.359 s).

| activity | A: PE-s | A: share | B: share | calls (A) | mean (A) |
|---|---:|---:|---:|---:|---:|
| `SsspChares::process_heap` | 221.6 | 66.6% | 69.1% | 656,103 | 338 us |
| idle | 46.5 | 14.0% | 9.9% | 775,813 | 60 us |
| `HTram::receivePerPE` | 27.8 | 8.4% | 8.9% | 12,143,145 | 2.3 us |
| `HTramRecv::receive` | 18.7 | 5.6% | 5.9% | 809,543 | 23 us |
| outside any entry method | 12.9 | 3.9% | 4.0% | | |
| `SsspChares::current_thresholds` | 4.7 | 1.4% | 1.9% | 52,560 | 89 us |
| everything else | 0.6 | 0.2% | 0.2% | | |

The two traces agree on every row within a point and a half, so the shape is
not one run's accident.

**Nothing is stalling and nothing is skewed.** Busy time per process spans
16.5-17.6 PE-s (max/median 1.03); per PE it is 0.94-1.29 s. Idle is 51% of the
first 20 ms bin, under 1% from 0.24 s to 1.0 s, and rises again over the last
0.2 s: it is the frontier's ramp and drain, not waiting mid-run. The
controller's round trip (`current_thresholds` plus its reduction) costs 1.4-1.9%
over 219 rounds (159 in B).

**Aggregation is working.** 809,543 TRAM messages of about 43 KB carried 1.44
billion updates -- 1,780 items per message, 118 per receiving PE. Median
send-to-execute latency is 19 ms within a node and 19 ms across nodes, which is
hold-and-flush delay rather than network time, and it costs little because
there is always other work: the PEs are 82% busy over the whole window, and
over 95% busy through the middle second of it.

## The gap is per-edge cost, not communication

The run scanned 1.442 billion edges -- 1.38 per edge in the graph -- and made
one update per scan, of which 1.425 billion (98.8%) were rejected at the
destination and 60.8 million improved a distance.

Dividing the trace by those 1.442 billion updates:

| stage | ns per update |
|---|---:|
| `process_heap`: pop, read the edge, charge the bucket, insert into TRAM | 154 |
| `HTram::receivePerPE`: compare against `distances[]`, retire or queue | 19 |
| `HTramRecv::receive`: sort a message's items by destination PE | 13 |
| idle (ramp and drain) | 32 |
| scheduler and network progress | 9 |
| controller | 3 |
| **total** | **231** |

Per edge in the graph, that is 318 ns of hardware time. RIKEN solved the same
graph on the same 256 cores in 0.20 s: 49 ns per edge, 6.5x cheaper. GAPBS did
it on 128 cores in 1.19 s: 145 ns per edge, still 2.2x cheaper than ACIC with
half the hardware. **ACIC's deficit is the cost of moving one update through
its pipeline, and two thirds of that is spent before the update is ever sent.**

What the pipeline charges that a shared-memory relaxation does not:

- **Every scanned edge becomes a message item**, 24 bytes of payload, and
  98.8% of them are discarded on arrival. 34.6 GB crossed the aggregation
  layer; half the messages left their node, which is about 6.2 GB/s per node,
  roughly half of HDR100's line rate.
- **Each item is copied twice on arrival.** `HTramRecv::receive` allocates a
  sorted node message (24.2 GB over the run) and only then does each PE scan
  its slice.
- **The fan-out is 15.** Each arriving message is handed to every PE in the
  process: 809,543 arrivals become 12.1 million `receivePerPE` calls. Here that
  is not waste -- each call still carries 118 items -- but it fixes the cost of
  an arrival at 15 scheduler dispatches.

## What this settles, and what it does not

Settled: on this input and layout the loss to RIKEN is not latency, not
imbalance, not idle time and not the controller. It is the per-update cost of
the admission-controlled update pipeline, and the fact that the algorithm
delivers 24 updates for every distance change it keeps.

Not settled: how the 154 ns inside `process_heap` divides between the heap pop,
the edge read and `sendItemPrioDeferredDest`'s per-destination hold. Projections
cannot see inside an entry method, and at 1.4 billion events bracket events
would cost more than they measure. The next measurement is either hardware
counters (the `sssp_smp_papi` build, whose PAPI path is still Cray-only) or a
run with the insert replaced by a counting stub, which gives up correctness for
a ceiling.

Traces, reports and run output: `campaign/traces/rmat25-2n-8x15-20751377`
(`traceA-report.md`, `traceB-report.md`). About 20 SU.
