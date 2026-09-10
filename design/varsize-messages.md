# Step 4 — genuine varsize messages

Plan step 4: *"Genuine varsize messages (`char buffer[]`); `setUsersize` on all send
paths. Re-run the buffer-size sweep, now co-varied with `LCI_ATTR_PACKET_SIZE`."*
Gate: *"Verify identical; wire bytes drop at `bufSize < 2048`."*

The verify gate passes byte-identical, and the digest is additionally invariant across
`--bufsize` 1, 7, 63, 256, 512, 1024, 2048. **The gate's second clause turned out to be
based on a wrong premise, and the correction matters more than the fix.** See §2.

Two of this note's conclusions have since been overturned by measurement, and both
corrections are the interesting part:

- §2, the gate's "wire bytes drop at `bufSize < 2048`" clause, which rested on a claim about
  padding that turned out not to hold.
- §3, which reported the old `setUsersize` formula as an active out-of-bounds read. It was
  not: `setUsersize` rounds up to 16 bytes, which covered the shortfall exactly. The formula
  *would* have become a real out-of-bounds read the moment this step moved the payload
  offset, which is the reason both changes had to land together. Measured on two nodes
  in §7.

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

## 3. The four existing `setUsersize` calls

All four sites computed the envelope size as

```cpp
setUsersize(sizeof(int) + sizeof(itemT) * next);
```

That is a formula about the struct's first field, not about where the payload is. The
payload's offset is chosen by the generated allocator, nothing tied the two together, and
the two are not equal: with `int next` at offset 0 and `itemT` requiring 8-byte alignment,
`buffer` started at offset 8, so the expression undercounts by the 4 bytes of padding.

An earlier draft of this note concluded from that arithmetic that every partially-flushed
message was truncating its last item and the receiver was reading four bytes out of bounds.
**That conclusion was wrong, and it was wrong because the arithmetic stopped one step too
early.** `setUsersize` does not store the size it is given; it stores
`CkMsgAlignLength(s)`, which is `ALIGN_DEFAULT(s)` — a round up to 16 bytes. For
`sizeof(itemT) == 24` that rounding hands back exactly the 4 bytes the formula dropped:

| `next` | needed (`8 + 24n`) | asked for (`4 + 24n`) | envelope actually carried | margin |
|---|---|---|---|---|
| 1 | 32 | 28 | 32 | 0 |
| 2 | 56 | 52 | 64 | 8 |
| 8 | 200 | 196 | 208 | 8 |
| 9 | 224 | 220 | 224 | 0 |

The margin is 8 bytes on even counts and **exactly zero** on odd ones, and never negative.
This is measured, not derived — see §7. So the pre-existing code was not reading out of
bounds; it was sitting on the boundary with no bytes to spare, held there by an alignment
rule it did not know about.

What makes that worth fixing anyway is what §1 does to it. Making the message genuinely
varsize moves the payload from inside the object to after it: the generated allocator places
it at `ALIGN_DEFAULT(sizeof(HTramMessage))`, which is 16, not 8. The same formula then
undercounts by 12 rather than 4, and a 16-byte round-up cannot cover 12. **The old formula
carried into the new layout under-sizes every odd-count send by 8 bytes** — the last item's
`distance` field lands entirely outside the transmitted region. That is a real out-of-bounds
read on the receiver, and it has been reproduced on two nodes (§7).

So this was not a live bug that step 4 fixed. It was a latent one that step 4 would have
activated, and the two changes had to land together.

`usedBytes()` therefore does not restate the allocator's formula either. It reads the
payload offset back off the pointer the allocator installed:

```cpp
size_t payloadOffset() const { return buffer - (const char *)this; }
size_t usedBytes() const { return payloadOffset() + sizeof(itemT) * next; }
```

A restatement can drift, and had drifted. A read-back cannot: whatever charmc decides about
alignment, and whatever fields are added to the class later, `buffer` points at the payload
by construction.

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

## 7. The two-node check

Run 2026-09-10 on two exclusive Delta CPU nodes (cn084, cn133), Reconverse over LCI/OFI,
one process per node so that `CkNumNodes() == 2`. Scripts and logs:
`/work/hdd/mzu/rao1/sssp-2node`. Fabric settings are the ones the Barnes-Hut work on these
nodes established (`FI_CXI_RX_MATCH_MODE=hybrid`, `+lci_ndevices 4`, `srun --unbuffered
--kill-on-bad-exit=1`); nodes must be requested `--exclusive`, or Slurm's cpuset does not
contain the cores `+pemap` asks for and every PE aborts in `CmiSetCPUAffinity`.

Two nodes is the whole point: inside one process a send hands over a pointer and the
receiver reads the entire allocation whatever the envelope says, so an under-sized envelope
is *unobservable* at `CkNumNodes() == 1` at any PE count. Only a message that leaves the
address space is truncated to its declared size.

### The instrument

htram now carries a receive-side check, on unless `-DHTRAM_NO_ENVELOPE_CHECK`. At every
landing point it compares the envelope that arrived against `usedBytes()` and aborts if the
envelope is shorter. One comparison per message, not per item. It is the invariant asserted
where it is observable, instead of left to whichever run happens to notice a corrupted
value — which, for this payload, no run would: `cost` values here fit in 32 bits, so the
truncated half is a zero high word and the result is unchanged.

That last point is why `--verify` alone cannot close this item. It passed on the buggy
build too.

### Three builds

| build | send-side formula | payload offset | result on 2 nodes |
|---|---|---|---|
| **fixed** (HEAD) | `payloadOffset() + 24n` | 16 | VERIFY PASS everywhere |
| **legacy** | `4 + 24n`, current layout | 16 | check fires immediately, every configuration |
| **historical** | `4 + 24n`, pre-varsize layout (htram `4028bcf`, app `ef32d43`, unmodified but instrumented) | 8 | VERIFY PASS, **min margin exactly 0** |

Fixed build, all `VERIFY PASS` — digests match serial Dijkstra in-process:

- `--bufsize` 1, 7, 63, 256, 1024, 2048 on the 10k/160k random graph
- `LCI_ATTR_PACKET_SIZE` 4096 / 16384 / 65536 at `--bufsize 256`. An htram message is
  `16 + 24·bufSize` bytes, so this spans messages that fit in one eager packet and messages
  that do not.
- the 40k 2-D mesh at bufsize 63 and 2048, and a 200k/3.2M random graph at ppn 15

Legacy build, every configuration:

```
htram: HTramRecv::receive got a message declaring 9 items, which need 232 user bytes,
in an envelope carrying only 224. The sender under-sized it; reading the last item
would run past the received buffer.
```

`232 = 16 + 24·9`; the sender asked for `4 + 24·9 = 220`, which rounds to 224. Eight bytes
short, exactly as §3 predicts, and it fires on the first odd-count partial flush at every
buffer size including 2048.

Historical build, instrumented to report the tightest margin it saw rather than to abort:

```
[htram-envcheck] HTramRecv::receive items=9   need=224  have=224   min_slack=0
[htram-envcheck] HTramRecv::receive items=8   need=200  have=208   min_slack=8
[htram-envcheck] HTramRecv::receive items=119 need=2864 have=2864  min_slack=0
```

Odd counts land exactly on the boundary, even counts have 8 bytes to spare, nothing is ever
short — over random and mesh graphs at ppn 4, 8 and 15. That is the measurement that
retracts the original claim.

### What this closes

The step-4 gate's remaining item — "needs a 2-node check on the cluster" — is closed: the
varsize messages are correct across the address-space boundary at every buffer size and
packet size tried, and the correctness of the size formula is now asserted by the code
rather than by a comment.

The step-4 gate's *second clause*, "wire bytes drop at `bufSize < 2048`", remains
withdrawn for the reason given in §2, and §6's byte table stands: aggregation changes the
number of messages, not the payload volume.

### Reproducing

The fixed-build half of this is now a committed gate:

```
sbatch scripts/verify_2node.sh          # SSSP_BIN / SSSP_LOGS / SSSP_NDEV override
```

Run against HEAD on cn048+cn084, all 13 configurations `PASS`:

```
random_bs1 … random_bs2048          PASS   (bufsize 1, 7, 63, 256, 1024, 2048)
random_bs256_ps4096/16384/65536     PASS
mesh_bs63, mesh_bs2048              PASS
big_bs256, big_bs2048               PASS   (200k/3.2M, ppn 15)
2-NODE GATE PASSED
```

The two counterfactual builds are not committed — they exist to answer a question that has
now been answered, and both are one edit away from HEAD. They live in
`/work/hdd/mzu/rao1/sssp-2node`: `htram-legacy/` is HEAD with `trimHTramMessage` reverted to
the old formula (`usedBytes()` deliberately left correct, so the receive-side check is a real
comparison and not a restatement of what the sender computed), and `hist-htram/` + `hist-app/`
are `git worktree` checkouts of `4028bcf` and `ef32d43` with nothing changed but the
instrument. `scripts/run2node.sh` there drives all three.
