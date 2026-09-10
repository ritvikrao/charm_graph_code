# Step 3: defect fixes

*Step 3 of [sc27-plan.md](sc27-plan.md). Gated by
[verify-harness.md](verify-harness.md). Started 2026-09-10.*

The plan says to own this section in the paper: "we found and fixed a control-path
pathology; all numbers below are against the fixed baseline" is a much better
sentence than a reviewer finding it. This note is the record of what was wrong,
how it was confirmed, and what changed as a result.

Every fix below passes the ten-configuration verify gate.

---

## 1. Out-of-bounds histogram read on an empty window

`reduce_histogram` leaves `first_nonzero` at `-1` when no bucket in the reduced
window is occupied, then broadcasts `first_nonzero - 1`. `contribute_histogram`
adds one back, starts its copy loop at `i = -1`, and reads `histogram[-1]`.

**Confirmed, not inferred.** Instrumenting the loop shows it firing on every
run, once per PE — 4 reads at ppn 4 on both mesh and random inputs — in the
round just before the termination check trips. Zero after the fix.

The value read is whatever precedes the array in the heap, and it is summed into
`info_array[0]`, which the reduction folds into the next global histogram. So
the final histogram of every run has had garbage in its first bucket.

An empty window means the run has either converged or pushed all remaining work
past the window's right edge; either way the window should stay put, so
`first_nonzero` falls back to `last_first_nonzero`. The bounds test in
`contribute_histogram` now rejects negative indices too.

---

## 2. `rand()` gating the aggregation flush

`current_thresholds` ended with `if (rand() % 5 == 0) tram->tflush();`. `rand()`
keeps process-global state, which every worker thread in an SMP process shares:
a contention point where it is thread-safe at all, and an uncontrolled random
factor in every measurement.

Three replacements were measured, 8 runs each, `V=10000 E=160000 ppn=4`,
reporting wasted updates:

| Flush gate | mean | stdev | spread |
|---|---|---|---|
| `rand() % 5` (shared, racy) | 152,593 | 559 | 1,945 |
| exactly periodic, one shared phase | 155,223 | **38** | 124 |
| exactly periodic, phase staggered by chare | 155,940 | 1,652 | 4,596 |
| **per-chare RNG draw (chosen)** | 153,910 | 508 | 1,873 |

Two things fall out of this.

**Periodic flushing is worse, and the reason is instructive.** Putting every
chare on the same round makes communication bursty and costs 1.7% more wasted
updates. Staggering the phase by chare index is worse still on both mean and
variance. `rand()` was drawing per chare, so flushes were already decorrelated
across chares; that decorrelation is doing real work and should be kept.

The chosen fix therefore keeps an independent per-chare draw — the semantics all
prior measurements were taken under — using the portable `VertexRng` seeded by
`(chare index, seed)`. Thread-local, reproducible, same distribution.

**Flush timing dominates run-to-run variance.** The synchronized variant's
stdev is 38 against ~500 for both randomized variants, an order of magnitude.
Whatever else is nondeterministic in this asynchronous algorithm, when to flush
is the dominant term. That is worth knowing before quoting any single-run
number, and it argues for reporting distributions rather than best-of-n.

The cadence is now the named readonly `flush_round_interval` (still 5) instead
of a literal buried in an expression. Step 7's adaptive tail cadence takes it
from there; the periodicity question above is a cluster measurement, not a
laptop one.

---

## 3. A 30-second timeout that reported partial results as converged

`begin()` armed `CcdCallFnAfter(fast_exit, this, 30000.0)` unconditionally — the
comment said "end after 5 s" — and `fast_exit` printed the distances and called
`CkExit(0)`. Any run longer than 30 seconds therefore produced a full, ordinary
statistics block and a success exit status, distinguished from a real result
only by one extra line of output.

That is precisely the failure mode a scaling campaign cannot afford: the runs
most likely to exceed 30 seconds are the large ones the paper depends on, and a
batch log would not show anything wrong.

Demonstrated on a 200k-vertex graph with `--timeout 0.02`: the truncated run
reports 191,831 reachable vertices against a true 200,000, with a distance sum
of 168.7M against a true 154.7M. Unconverged distances are overestimates, so
the numbers are not merely incomplete, they are wrong in a consistent direction.

Now:

- the timeout is `--timeout <seconds>` (or `--timeout=<seconds>`), **off by
  default**, so no run is silently cut short;
- when it fires the output carries a `TIMEOUT: ... PARTIAL result` banner;
- the process exits nonzero, and `--verify` reports `VERIFY FAIL` for a
  truncated run even if the digests happen to agree.

---

## 4. `processHeapShared`: dead code, a wrong-PE atomic, and an O(N²) array

`processHeapChunk` ended with:

```cpp
shared_local->chunks_remaining[send_chare][bucket]--;
if (shared_local->chunks_remaining[send_chare][bucket] == 0)
    whole_bucket->clear();
```

`shared_local` is the *executing* PE's branch of the `SharedInfo` group, but
`processHeapOtherCaller` is a nodegroup entry method, so it runs on whichever PE
in the node the message lands on — not `send_chare`'s PE. The decrement lands in
a different PE's copy of the counter, and the `== 0` test is therefore
meaningless: the buffer is cleared on a condition that has no relationship to
whether the chunks are done.

None of this ran. The path sits inside `#ifdef NODE_LOAD_BALANCE` inside
`#ifdef PQ_HOLD_ONLY`, and both are commented out.

**The reason to delete rather than leave it is the memory.**
`SharedInfo::chunks_remaining` is `N` arrays of `HISTO_BUCKET_COUNT` atomics —
allocated **per PE**, sized by the **total** PE count. Per-PE cost grows
linearly in N, so aggregate cost is quadratic:

| PEs | per PE | aggregate |
|---|---|---|
| 64 | 0.5 MB | 32 MB |
| 1,024 | 8 MB | 8 GB |
| 32,768 (512 nodes x 64) | 256 MB | 8 TB |

At the scale this paper targets it is not a waste, it is a wall. Measured RSS
confirms the shape even on four cores — the saving is 0.3 MB at ppn 1, 2.6 MB at
ppn 4 and 15.2 MB at ppn 8, growing faster than the PE count.

`hold_to_process` went with it: 2048 vectors each `reserve(4096)`, 128 MB of
allocation per chare. It barely shows in RSS because the pages are never
touched, but it is real address space, and on a system with overcommit disabled
or a strict memory cgroup it is charged in full.

Removed: `processHeapShared` (class, nodegroup, readonly proxy),
`processHeapChunk`, `generateUpdatesOtherPe` (its only caller),
`chunks_remaining`, `hold_to_process`, `chunk_size`, the two trace-event ids
used only here, and the `NODE_LOAD_BALANCE` branch of `process_heap`. The
surviving `#else` branch is the code that actually runs. Confirmed to still
compile under `-DPQ_HOLD_ONLY`, which the default build does not exercise.
