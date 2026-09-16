# Steps 7.6k-m: buffer size from the controller, shared memory, CPU

*2026-09-16. Follows [7.6j](step76-perf.md), which left rmat25 at 121 ns per
graph edge at two nodes (RIKEN 49) and found the best buffer size depends on
the graph class. All runs: Anvil, two nodes, the layout 7.6f1 chose for ACIC
(8 x 15 for rmat25 and orkut, 16 x 7 for mesh24 and road-usa), variants
interleaved in one allocation (`scripts/anvil/ab_compare.sbatch`), every
digest checked. Binaries are built side by side by
`scripts/anvil/build_variant.sh`; `campaign/bin/variants-manifest.txt`
records each one's tree, sources and flags. The 7.6n re-take is in
[step76-external.md](step76-external.md#76n-re-take-after-76k-m).*

## Result

rmat25, source 23077392, job 20769565 (5 rounds, shared-memory tree):

| variant | median solve | ns/edge |
|---|---:|---:|
| fixed 6144 items + send filter (7.6j's best settings) | 0.479 s | 110 |
| same, no destination-lookup fast path | 0.609 s | 140 |
| the new defaults (acceptance policy, filter auto) | 0.507 s | 116 |
| 2048 items + filter (the wrong regime) | 0.847 s | 194 |

rmat25 is at 110-116 ns per edge, 2.2-2.4x RIKEN's 49 (7.6j: 121, 2.5x).
The allocation with and without shared memory (job 20767814) put the same
settings at 0.664 s on the 7.6j binary and 0.495 s with shared memory
(1.34x).

The new defaults against the best fixed size per graph, same allocation:

| graph | best fixed | defaults | the default chose |
|---|---:|---:|---|
| rmat25 (20769565) | 0.479 s (6144 + filter) | 0.507 s | 6144, filter on |
| mesh24 (20769540) | 1.091 s (1024) | 1.110-1.121 s | 1024, filter off |
| road-usa (20767817) | 2.270 s (1024) | 1.423-1.485 s | 512, filter off |
| orkut (20769609) | 0.157 s (2048 + filter) | 0.178 s | 6144, filter on |

The policy is within the floor of the best measured fixed size on rmat25
and mesh24, and on road-usa it found a size (512) that no fixed arm had
tried, 1.53x faster than 1024. **orkut is the miss**: its degree (76) puts it
at the 6144 ceiling, but it is fastest at 2048 with the filter on, and the
default loses 1.13x there. orkut has a quarter of rmat25's edges over the
same 3,840 destination streams, so a 6144-item buffer fills four times more
slowly; the degree start does not see that, and the share (0.003, as on
rmat25) cannot either.

## 7.6k: the buffer size follows the graph

`--bufsize-policy acceptance` (now the default):

- **Signal.** The share of arriving updates that shorten a distance. It
  separates the classes by 5-10x (mesh24 0.28, road-usa 0.43, rmat25 0.05,
  orkut 0.03) and did not move with the buffer size (mesh24 0.28 at 1024,
  2048 and 6144), so steering by it does not feed back on itself. A high
  share means long improvement chains, where an item held in a buffer delays
  the next link: mesh24 created 4.7x more updates at 6144 than at 1024. A low
  share means most arrivals are redundant, and fewer, larger sends pay.
- **Start.** 256 items per unit of average degree, in multiples of 256 and
  within 512..6144 (`--bufsize-range`). The share cannot choose the start:
  on rmat25 the first round with enough updates to judge arrives after 40% of
  the run's updates were created, so a first version that started at 2048
  and let the share decide was 1.38x slower than fixed 6144 (job 20767814).
  Degree and share put every graph measured in the same regime; within the
  scale-free regime neither separates orkut from rmat25.
- **Correction.** `--bufsize-acc-items` (288) / share, applied only when it
  differs from the current size by 2x or more, so the share corrects a wrong
  start rather than tuning a right one; the first samples of a scale-free
  ramp read 0.08. The controller broadcasts a change with its thresholds,
  and htram's `setBufferSize()` now resizes buffers mid-run: a buffer that
  is too small is copied into a larger one, one already past a smaller size
  is sent at once.
- **Send filter.** `--send-filter auto` (the default) turns the 7.6j filter
  on only while the size is at least 2048, the scale-free regime. It lost on
  both high-diameter graphs (0.79x road-usa, 0.80x mesh24 at 1024). Filtered
  updates count as rejected in the share; without that a filtered mesh reads
  0.99.
- **Compatibility.** An explicit `--bufsize N` without a policy means fixed,
  and an explicit `--send-filter-bits N` (0 included) replaces `auto`, so
  earlier command lines keep their meaning.

Is this co-design? The algorithm's own feedback steers the library's
buffering, and the setting it finds on road-usa beats the best fixed arm
tried. But the start comes from a static property, degree, and on the four
graphs the share never changed the regime it started in; where the start
was wrong (orkut) the share could not see it either. The claim that
survives is a regime rule, stated as such, with a feedback correction still
to be shown necessary.

## 7.6l: shared memory between processes

LCI's own shared memory carries only small messages, and 7.6c measured that
build 2x slower. The lever is Charm++'s `--enable-shmem`, which Reconverse
implements: a point-to-point send to another process on the same node goes
through a POSIX shared-memory pool (8 MB per process, 256 KB cutoff).
Built from the pulled trees (`charm_reconverse` `f6c74074f`, Reconverse
`33b8c36`) as `reconverse-linux-x86_64-mpicxx-v0916-shm`, beside a plain
`-v0916` tree.

| graph | shared memory vs plain, same allocation |
|---|---|
| rmat25 | 1.34x (0.664 -> 0.495 s), spread 0.53-0.84 s -> 0.48-0.50 s |
| orkut | about 1.8x over the 7.6j binary, whose runs spread 0.28-0.41 s |
| road-usa | 1.06x |
| mesh24 | 1.00x |

A 256 MB pool (`++ipcpoolsize`) changed nothing on rmat25. The build step
repoints `~/charm_reconverse/{bin,lib,include}` to the last tree built;
they were pointed back at the original tree afterwards.

## 7.6m: CPU

| change | rmat25 | kept |
|---|---|---|
| Destination lookup: a table that answers most lookups with one load | 1.27x (shared-memory tree; orkut 0.97x, within the floor) | yes |
| Write-prefetch in htram's insert | 0.93x rmat25, 0.83x orkut | no |
| LTO (htram inlined into `process_heap`) + `-march=znver3` | 0.94x | no |
| `-march=znver3` alone | 0.83x | no |
| Backlog sends (`RECONVERSE_LCI_BACKLOG=1`, patched library in scratch) | 0.78x | no |

The lookup table: partitions are contiguous and in PE order, so when the
PEs owning vertices j*M and (j+1)*M agree, every vertex between them is that
PE's. At most N of the V/M ranges fail that. The scheduler's 13% in the
7.6j profile is mostly idle polling (the idle branch reads the clock on
every pass), not work, and was left alone.

## Not settled

- **Whether the share is needed.** On all four graphs the degree start chose
  the right regime. A graph whose degree and acceptance disagree would test
  the correction.
- **orkut.** A start capped by edges per destination stream (about E / (30 x
  streams)) would give orkut 2048 and leave the other three graphs where they
  are, but it is a rule fitted to the one graph it fixes; it needs a graph
  outside these four before it goes in. The shared-memory CPU variants were
  within 1.1x of each other on orkut (LTO 1.09x, the lookup fast path 0.97x),
  inside the floor.
- **The remaining 2.2x on rmat25.** The next profile should be taken on the
  shared-memory build, since the progress and spin shares it measured have
  moved.
