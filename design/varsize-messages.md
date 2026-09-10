# Step 4 — genuine varsize messages

Plan step 4: *"Genuine varsize messages (`char buffer[]`); `setUsersize` on all send
paths. Re-run the buffer-size sweep, now co-varied with `LCI_ATTR_PACKET_SIZE`."*
Gate: *"Verify identical; wire bytes drop at `bufSize < 2048`."*

The verify gate passes byte-identical, and the digest is additionally invariant across
`--bufsize` 1, 7, 63, 256, 512, 1024, 2048. **The gate's second clause turned out to be
based on a wrong premise, and the correction matters more than the fix.** See §2.

## 1. What was actually wrong

`htram_group.ci` declared

```
message HTramMessage { int next; itemT *buffer; };
```

`Type *name` is not charmc's varsize syntax — varsize is `Type name[]` — so the generated
allocator ignored its `sizes` argument entirely and every message was allocated at the
fixed `itemT buffer[BUFSIZE]` declared in the C++ class. Same for `HTramLocalMessage` and
`HTramNodeMessage`.

All three are now genuine varsize messages with `char buffer[]` payloads (byte-oriented,
because step 8's `HTramCore` is) viewed through typed `items()` accessors. Allocation goes
through `newHTramMessage(capacity)` / `newHTramNodeMessage(capacity, noffsets)`, and every
send goes through `HTram::trim()`, which sets the envelope size and accounts for the bytes.

| | old allocation | new allocation |
|---|---|---|
| `HTramMessage` | 49168 B always | 32 + 24·capacity |
| `HTramNodeMessage` | 32800 B always | 32 + 16·items + 4·nodesize |
| `HTramLocalMessage` | 400 B always | 32 + 24·capacity |

## 2. Correction: the 2024 buffer-size study was not measuring padding

The plan asserted that because messages were not varsize, *"the 2024 buffer-size study
(512/1024/2048, Fig. 6) varied when messages were sent but not how many bytes crossed the
network,"* and that at `bufSize=512` roughly 4× of the wire bytes were padding.

**That is wrong, for a reason that is itself a defect.** The `buffer_size` argument to
`HTram`'s constructor was accepted and then never read; `bufSize` was unconditionally set
to `BUFSIZE`. `sssp_smp.cpp:65` said so out loud:

```cpp
int buffer_size = 1024;    // meaningless for smp; size changed in htram_group.h
```

So the only way to change the buffer size was to edit `BUFSIZE` in `htram_group.h` and
rebuild both libraries — which also resized the fixed `itemT buffer[BUFSIZE]` array. Buffer
size and message size moved together. The 2024 study did vary wire bytes, and its
conclusion stands on that count.

What the dead knob actually cost: a sweep needed a full rebuild of htram *and* the
application per data point, and `setBufferSize()` — an exposed entry method — would have
produced exactly the padding the plan described, had anyone called it. `histo.C` and
`smp_ig.C` are worse off: both take a `-S` buffer-size flag, and `histo.C` prints it back
(`TRAM Buffer Size (-S)= %d`), and it has never done anything.

`bufSize` is now live. `sssp_smp` takes `--bufsize <items>` (default 2048 = `BUFSIZE`, so
the out-of-the-box configuration is unchanged), and `-S` starts working in the other apps
— a behaviour change for them, since their default is 1024, not 2048.

## 3. A latent multi-node correctness bug in the four existing `setUsersize` calls

All four sites computed the envelope size as

```cpp
setUsersize(sizeof(int) + sizeof(itemT) * next);
```

The payload does not start at `sizeof(int)`. With `int next` at offset 0 and `itemT`
requiring 8-byte alignment, `buffer` started at offset 8, so this **undercounts by the
4 bytes of padding** and truncates the last item of every partially-flushed message.
Measured layout, `-DGRAPH`:

```
sizeof(CMessage_HTramMessage)=1  offsetof(next)=0  offsetof(buffer)=8  sizeof(itemT)=24
old setUsersize for next=1: 28    bytes actually needed: 32
```

The receiver would read `buffer[next-1].distance` four bytes past the transmitted region.
For this payload the damage is bounded — `cost` values in these runs fit in 32 bits, so the
truncated half is the (zero) high word — but the read is out of bounds regardless, and any
payload whose last field uses its upper bytes would be corrupted outright.

`usedBytes()` now uses `ALIGN_DEFAULT(sizeof(HTramMessage)) + sizeof(itemT)*next`, which is
the generated allocator's own offset formula, so the two cannot drift apart again.

**Not reproducible here.** Envelope sizes only matter when a message leaves the address
space, and `CkNumNodes()` is 1 on this laptop under Reconverse at every ppn. This needs a
2-node check on the cluster — it is the one item in this step that local runs cannot close.

## 4. `setUsersize` coverage

Was 4 sites, all on the timed/idle flush path. Now all 15 send paths trim, including the
11 size-triggered sends that never did. At `bufSize == BUFSIZE` a full buffer already
occupies its whole allocation, so this changes nothing today; it is what makes the byte
accounting exact and what keeps size-triggered sends correct once `bufSize < capacity`
becomes reachable (step 7's adaptive buffering).

## 5. Other things this closed

- **`HTramNodeMessage` leaked on every received message.** It held a `std::vector<int>
  offset`. Charm frees messages through the registered `dealloc`, which does not run a
  destructor, so the vector's heap block leaked — `CkNodeSize()·4` bytes plus allocator
  overhead per aggregated message received. It is now a varsize `int offset[]`.
- **`localMsgBuffer` was write-only**: assigned in both `HTram` constructors, never read.
  48 KB per PE. Deleted.
- **`setBufferSize()` would have overrun its buffers** now that they are sized exactly, so
  it reallocates, and refuses (`CkAbort`) if items are still buffered rather than dropping
  them silently.
- **`copyToNodeBuf` claims a slot index below `bufSize` and then writes `LOCAL_BUFSIZE`
  items from it**, so it can run up to 15 items past `bufSize`. Harmless while capacity was
  a fixed 2048 and `bufSize` was a multiple of 16; not harmless once capacity is exact.
  Node-group buffers carry `BUFSIZE + LOCAL_BUFSIZE` of slack. (PP mode only, which nothing
  currently reaches.)
- The second `HTram` constructor never initialised `bufSize` at all.

## 6. Measurements

200k vertices / 3.2M edges, uniform, seed 100, ppn 8, Reconverse on Apple Silicon.
7 runs per cell; compute time reported as median (min–max). "old" is `4028bcf` recompiled
with `BUFSIZE` edited per column, which is how the 2024 sweep had to be run.

### Wall clock — unchanged

| bufSize | old (s) | new (s) |
|---|---|---|
| 256 | 0.03504 (0.03288–0.04028) | 0.03460 (0.03401–0.03970) |
| 512 | 0.03109 (0.02962–0.03531) | 0.03100 (0.02868–0.03472) |
| 1024 | 0.02985 (0.02748–0.03846) | 0.02799 (0.02742–0.03146) |
| 2048 | 0.02818 (0.02507–0.02961) | 0.02885 (0.02695–0.04157) |

Every difference is inside the run-to-run range. Peak RSS is likewise indistinguishable
(155–159 MB in both, at every buffer size). **The varsize change buys no measurable local
time**, which is the expected result: on one node nothing serialises, so the only effect is
allocator churn, and this allocator absorbs it.

The curve's *shape* is also unchanged — 256 is worst, 1024–2048 best, in both builds. I had
expected the fixed 32 KB node-message allocation to penalise small buffer sizes and bias the
sweep toward large buffers; **it does not, at this scale on this machine.** That hypothesis
is not supported and should not be carried into the paper.

### Bytes

Same configuration, from the new `TRAM …` counters (`bytes sent` = envelope user bytes
handed to the send path; `bytes allocated` = what was allocated to carry them, which for
the source buffers is also exactly what the old code allocated):

| bufSize | messages | bytes sent | source alloc | node-msg alloc, new | node-msg alloc, old | node-msg ratio |
|---|---|---|---|---|---|---|
| 256 | 12894 | 76.9 MB | 79.4 MB | 51.95 MB | 422.9 MB | **8.14×** |
| 512 | 6571 | 76.8 MB | 80.8 MB | 51.56 MB | 215.5 MB | **4.18×** |
| 1024 | 3394 | 76.8 MB | 83.5 MB | 51.37 MB | 111.3 MB | **2.17×** |
| 2048 | 2049 | 76.7 MB | 100.7 MB | 51.26 MB | 67.2 MB | **1.31×** |

(old node-msg alloc = messages × 32800 B, the fixed allocation the old code made per
received message.)

Two things to read off this table:

- **Payload volume is flat at ~76.8 MB** regardless of buffer size, as it must be — the
  item count is a property of the algorithm, not the aggregation. Aggregation changes the
  *number* of messages, not the bytes. Any future claim that a buffer-size setting "reduces
  communication" has to be about message count or latency, not volume.
- **Receive-side allocation used to scale inversely with buffer size** and now does not.
  This is the one place where the fake-varsize declaration cost real resources.

At `bufSize = 2048` the source-side allocation exceeds bytes sent by 1.31× (100.7 vs 76.7
MB). That gap is the partial flushes, which allocate a full-capacity buffer and ship a
fraction of it. It is the headroom a per-destination adaptive buffer size would recover,
and it is the measurement step 7 should be judged against.

### Not measured here

- **Wire bytes.** There is no wire: one node. The `bytes sent` column is the exact count of
  bytes that *would* be serialised, which is the right proxy, but the packet-level
  interaction — the plan's `LCI_ATTR_PACKET_SIZE` co-variation — cannot be run on a laptop.
- **The `setUsersize` truncation in §3**, for the same reason.

Both are cluster items, and they are now cheap to run: the sweep is a command-line flag on
one binary instead of eight rebuilds.
