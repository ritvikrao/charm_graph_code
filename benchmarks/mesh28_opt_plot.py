#!/usr/bin/env python3
"""Plot matched held-out solve times from mesh28_opt_summary.py's JSON."""
import argparse
import json
from pathlib import Path
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument("summary", type=Path)
ap.add_argument("output", type=Path, help="Output prefix for PNG and PDF")
a = ap.parse_args()
s = json.loads(a.summary.read_text())
rows = s["sources"]
fig, ax = plt.subplots(figsize=(10.4, 5.5), constrained_layout=True)
x = np.arange(len(rows))
width = .19
variants = [("base", "Original ACIC", "#8b98a5"),
            ("chunks8", "Private chunks, slice 8", "#64a5cf"),
            ("chunks64", "Private chunks, slice 64", "#156795"),
            ("wasp", "Wasp, 128 threads", "#d69c35")]
for i, (key, label, color) in enumerate(variants):
    med = [r["wasp_median"] if key == "wasp" else
           r["variants"][key]["median"] for r in rows]
    raw = [r["wasp_seconds"] if key == "wasp" else
           r["variants"][key]["seconds"] for r in rows]
    err = [[m - min(v) for m, v in zip(med, raw)],
           [max(v) - m for m, v in zip(med, raw)]]
    bars = ax.bar(x + (i - 1.5) * width, med, width,
                  label=label, color=color, yerr=err, capsize=2)
    ax.bar_label(bars, labels=[f"{v:.2f}" for v in med], fontsize=8, padding=4)
ax.set_xticks(x, [r["source"] for r in rows])
ax.set_xlabel("Held-out source vertex")
ax.set_ylabel("Solve time (seconds; lower is better)")
ax.set_ylim(0, max(max(r["variants"]["base"]["seconds"]) for r in rows) * 1.22)
ax.set_title("Delta mesh28-z: cheaper queues and longer bounded work slices")
ax.spines[["top", "right"]].set_visible(False)
ax.set_axisbelow(True)
ax.yaxis.grid(True, alpha=.22)
ax.legend(frameon=False, ncol=2, loc="upper right", fontsize=9)
fig.supxlabel("One exclusive 128-core node; ACIC 16 × 7, +old-scheduler. "
              "Medians and min–max of 3 solves per source.\n"
              "Wasp rerun in the same allocation after ACIC; graph reading excluded. "
              f"Job {s['job']}.", fontsize=9)
a.output.parent.mkdir(parents=True, exist_ok=True)
for ext in ("png", "pdf"):
    fig.savefig(a.output.with_suffix("." + ext), dpi=180)
