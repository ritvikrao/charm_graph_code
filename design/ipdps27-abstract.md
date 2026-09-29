# IPDPS 2027 abstract (draft, 2026-09-29)

Submission limit: 500 words, due 2026-10-01 AOE. Numbers are from the current
evidence (current-state §33–§35). The F8 final matrix (freeze candidate
601697e) replaces them before the paper deadline; the abstract's ranges are
rounded so small shifts do not change it.

**Title (working):** Scaling Single-Source Shortest Paths Beyond Graph500:
Adaptive Asynchronous SSSP on Meshes, Terrain and Road Networks

---

Distributed single-source shortest path (SSSP) codes are designed, tuned and
ranked on the Graph500 benchmark: Kronecker graphs with a skewed degree
distribution, a small diameter and uniformly random edge weights. Many
graphs that need distributed memory look nothing like this. Simulation
meshes, 3-D grids, terrain models and road networks have low, uniform
degree, a diameter in the thousands to millions, and physically derived edge
weights. We show that on these graphs the Graph500-style distributed codes
do not scale: in our measurements, one Frontier node running a shared-memory
SSSP code is often faster than 64 nodes running a distributed one.

We present ACIC, an asynchronous SSSP algorithm built on a message-driven
runtime. ACIC replaces per-process priority queues with queues shared by all
the cores of a process, removes work from them nearest-bucket-first in
batches, and aggregates the resulting fine-grained updates into large
messages. Relaxation is asynchronous, with no bulk-synchronous supersteps;
lightweight reductions only monitor progress and detect termination. Several choices adapt to the input at
run time, among them whether processes share queues and how vertices are
placed. These mechanisms are straightforward to express in a
message-driven parallel programming model, and awkward in bulk-synchronous
MPI.

We evaluate ACIC against four distributed codes (Gluon, the RIKEN Graph500
SSSP code, HavoqGT and Gemini) and two tuned shared-memory codes (GAPBS and
Wasp) on up to 64 Frontier nodes (3,584 cores). The inputs are 2-D meshes, 3-D
grids, road networks of North America, Europe and Afro-Eurasia (distance and
travel-time weights), and Copernicus terrain with Tobler walking-time weights, up to 34
billion vertices and 275 billion edges. Every baseline is tuned on training
sources and measured on held-out ones, and every result is checked against a
reference solution.

On these graphs ACIC is 8–240× faster than Gluon and one to three orders
of magnitude faster than the RIKEN code, HavoqGT and Gemini. It is also the
only distributed code in the study that beats a tuned single node: at 64
nodes it is 5.0–14.5× faster than GAPBS and 2.3–7.9× faster than Wasp on large meshes,
grids and terrain. On the Kronecker graphs Graph500 uses, the RIKEN code
remains faster. The two classes of graph need different designs, and a
benchmark that measures only one misses the other.

An ablation at 16 and 64 nodes attributes the speedup. Queues shared within a
process give 5–15×, nearest-bucket removal 2.2–3.7×, and batched removal
1.5–1.6×. A chunked queue gives a further 2× on meshes and terrain. A
histogram-derived admission threshold, a dynamic-thresholding design,
admits all work in 89–99% of rounds and gives nothing. Recent work shows that
synthetic uniform weights misrepresent shared-memory SSSP. We extend that
finding to distributed memory, across weight ranges from [1, 10] to
[1, 65,536] and natural road and terrain weights. We also report ACIC's
limits: its speedup depends on a locality-preserving vertex order, and on
narrow weight ranges its bucket width costs up to 2×.

---

## Notes for revision

- Word count of the text between the rules: check with
  `awk '/^---$/{f=!f; next} f' design/ipdps27-abstract.md | wc -w` (limit 500).
- "One Frontier node … is often faster than 64 nodes running a distributed
  one": true for Gluon, RIKEN, HavoqGT and Gemini against GAPBS/Wasp on the
  meshes, grids and terrain (e.g. `grid3-30-z`: Wasp 1 node against Gluon at
  64 nodes). Recheck against F8 before submission.
- "awkward in bulk-synchronous MPI" is an argument, not a measurement. See
  the framing section of sc27-plan.md for the evidence we can add (an
  implementation comparison and an aggregation ablation).
- "adapt to the input at run time": `--process-share auto`, `--reader-tile
  auto`, `--hub-hints auto`. The reader-tiling rule mis-chooses on row-major
  meshes (O1b), which the limitations sentence covers.
- The weight-range sentence depends on the width-check job (5568640/5568642).
  If a width change is adopted, the "costs up to 2×" clause changes.
- Double-blind: no author names or institution; "our" never refers to prior
  papers; Charm++ may be named as a public system.
