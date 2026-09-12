# H4: the tail advances only at reduction cadence

*Step 6 of `design/sc27-plan.md`. Method and instrument in
`design/scale-free-diagnosis.md`. Measurements: 2^20 vertices, 16 edges per
vertex, 16 PEs per node, one and two exclusive Delta CPU nodes, median of five.*

## Verdict

**Confirmed, far more strongly than the hypothesis stated — and it is not the
tail, and not RMAT.** On a mesh the *entire run* is cadence-bound, not its last
few percent: adding 4 ms to the round period multiplies compute time by 27.7×
on one node and 38.5× on two, with the round count unchanged. Flushing the
aggregation buffers every round instead of one round in five makes the mesh
**3.2× faster on one node and 3.4× faster on two**, and costs 4% in redundant
work. On RMAT the same knob is worth 4%, in the wrong direction.

So the mechanism is real, it is large, it is free to fix, and it belongs to the
graph class ACIC already wins on.

## The mechanism, which is structural rather than hypothetical

Three facts about how a buffered update gets out, none of which is a hypothesis:

1. htram's idle-triggered flush is compiled out. `IDLE_FLUSH` is commented out
   at `htram_group.h:7`, and `HTram::idleFlush()` consequently returns `true`
   having done nothing.
2. The periodic timer flush is off by default (`enable_buffer_flushing` is
   `false`).
3. `HTram::tflush()` is called from `current_thresholds()` on a per-chare draw,
   on average one controller round in five.

So a partly-filled aggregation buffer has exactly two exits: fill to `bufSize`,
or catch one of those draws. Whenever the frontier is not generating enough
traffic to fill a buffer — which on a mesh is *always*, since a 1024×1024 mesh
has a frontier of order 1024 vertices against a 2048-item buffer — progress is
gated by the draw.

## The cadence measurement

Adding a fixed delay between the end of one controller round and the start of
the next. `--round-delay 0` is the shipped behaviour: the cycle is closed, so a
round costs exactly a reduction plus a broadcast and nothing sets a period.

| delay | mesh 1 node | mesh 2 nodes | RMAT 1 node | uniform 1 node |
|---|---|---|---|---|
| 0 ms | 1.00× | 1.00× | 1.00× | 1.00× |
| 1 ms | 7.6× | 10.7× | 1.5× | 1.4× |
| 4 ms | **27.7×** | **38.5×** | 3.5× | 2.8× |

The mesh's round count is flat across the whole sweep — 17,960 at 0 ms, 17,856
at 4 ms. Its compute time at 4 ms is 74.3 s against a predicted
17,856 × 4 ms = 71.4 s. **The mesh run is the round period times the round
count, and nothing else.** Two nodes makes it worse, as it must: a round is a
reduction and a broadcast, and both get more expensive.

## The flush measurement, which is the actionable one

`--flush-interval` sets how often a chare flushes, in controller rounds. 5 is
the shipped value.

| flush every | mesh s (1 node) | mesh rounds | mesh rej/\|E\| | RMAT s | RMAT rounds | RMAT tram msgs |
|---|---|---|---|---|---|---|
| 1 | **0.804** | 3,755 | 1.047 | 0.213 | 239 | 13,102 |
| 2 | 1.314 | 7,330 | 1.056 | 0.220 | 255 | 11,978 |
| 5 *(shipped)* | 2.607 | 17,960 | 1.089 | **0.205** | 277 | 11,186 |
| 10 | 5.074 | 36,340 | 1.106 | 0.230 | 375 | 9,420 |
| 20 | 9.549 | 71,520 | 1.138 | 0.255 | 545 | 10,155 |

The mesh's round count is **inversely proportional to the flush rate over a
factor of twenty** — 3,755 rounds at 1, 71,520 at 20 — and its compute time
follows. That is the mechanism stated above, measured: the mesh advances one
flush at a time, so the number of rounds it takes is set by how often it
flushes. The uniform graph shows the same shape weakly (769 rounds at 20 against
307 at 1) and pays 1.20×. On RMAT the round count also rises, and the time does
not follow: buffers there fill on their own, so an extra flush only sends a
smaller message. RMAT at flush-every-round sends 17% more messages for 4% more
time.

Two nodes, same table: mesh **0.29×** at flush-every-round, RMAT 1.09×.

## What this is not

**It is not a tail effect.** Defining the tail as the rounds after 99% of
vertices have settled, it is 3.2% of mesh runtime, 2.2% of RMAT's and 9.3% of
the uniform graph's. The hypothesis as written — "the tail advances only at
reduction cadence" — understates the problem by about thirty times on the mesh
and would have sent step 7 after the wrong few percent. The whole run advances
at reduction cadence; the tail is simply the part where that is easiest to see.

**It is not the two-tier hack.** The `histogram_sum <= N*100` branch, which
abandons the configured percentiles for 0.9999, fires on 1,903 of the mesh's
17,954 rounds, 18 of RMAT's 239 and 7 of the uniform graph's 357. Replacing it
with a controller over histogram shape, as the plan proposes, is a change to
about 10% of mesh rounds and 3–7% of the others. Worth doing; not worth 3.2×.

## Consequences for step 7

- **The adaptive flush cadence is the single largest result in step 6**, and the
  policy it implies is legible: flush aggressively when buffers are not filling,
  lazily when they are. Both regimes are visible in the table above, on the same
  binary, from one knob. The decision variable is buffer occupancy at flush
  time, which htram already knows.
- **Re-enabling `IDLE_FLUSH` is the right instinct and needs its own
  measurement.** Everything above changes *how often* a flush is attempted, not
  what triggers it. An idle-triggered flush should subsume the mesh's case for
  free, but `PARTIAL_FLUSH` gates it on the buffer already being partly full,
  and nothing here says what that threshold should be.
- **Report the result on the mesh and say plainly that it is not the scale-free
  fix.** A 3.2× on high-diameter inputs is a strong result on its own terms. It
  widens ACIC's existing lead rather than closing the deficit the paper is
  trying to explain, and presenting it otherwise would be a misreading a
  reviewer with these tables would catch.
