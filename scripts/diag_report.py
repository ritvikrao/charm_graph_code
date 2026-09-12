#!/usr/bin/env python3
"""Turn the output of scripts/diagnose.sh into the tables the four step 6
design notes quote.

Nothing here decides anything: it reads the CSVs and logs the harness wrote and
prints them in the shape a reader needs. Every number traces back to a file in
the output directory, which is the point -- the notes cite the tables and the
tables cite the runs.

  scripts/diag_report.py <outdir> [h1|h2|h3|h4|ab]
"""
import csv
import glob
import os
import re
import sys


def read_csv(path):
    """Read a .csv or a .tsv. The harness writes both -- the per-run dumps come
    from the solver as CSV, the sweep summaries from the shell as TSV, where
    tabs save quoting."""
    if not os.path.exists(path):
        return None
    delimiter = "\t" if path.endswith(".tsv") else ","
    with open(path) as f:
        return list(csv.DictReader(f, delimiter=delimiter))


def table(header, rows):
    widths = [len(h) for h in header]
    rows = [[str(c) for c in r] for r in rows]
    for r in rows:
        for i, c in enumerate(r):
            widths[i] = max(widths[i], len(c))
    line = "  ".join(h.ljust(w) for h, w in zip(header, widths))
    print("    " + line)
    print("    " + "  ".join("-" * w for w in widths))
    for r in rows:
        print("    " + "  ".join(c.ljust(w) for c, w in zip(r, widths)))
    print()


def graphs_in(outdir, sub):
    names = set()
    for p in glob.glob(os.path.join(outdir, sub, "*.rounds.csv")):
        names.add(os.path.basename(p)[: -len(".rounds.csv")])
    return sorted(names)


# ----------------------------------------------------------------- H1 --------
def h1(outdir):
    print("H1  bucket resolution")
    print()
    rows = []
    for name in graphs_in(outdir, "h1"):
        buckets = read_csv(os.path.join(outdir, "h1", name + ".buckets.csv"))
        rounds = read_csv(os.path.join(outdir, "h1", name + ".rounds.csv"))
        if not buckets or not rounds:
            continue
        created = [int(b["created"]) for b in buckets]
        used = [i for i, c in enumerate(created) if c > 0]
        total = sum(created)
        # How concentrated the updates are: the smallest number of buckets
        # holding half of everything ever created.
        half, acc, nbuckets = total / 2.0, 0, 0
        for c in sorted(created, reverse=True):
            if acc >= half:
                break
            acc += c
            nbuckets += 1
        occupied = [int(r["occupied"]) for r in rounds]
        thresholds = {r["heap_threshold"] for r in rounds}
        # The controller's actual dynamic range in a round: how far above the
        # frontier the percentile cut lands. 0 means the cut is the frontier
        # itself -- the percentile had nothing to choose and the controller has
        # degenerated to "admit the lowest bucket and nothing else". That is
        # the precise form of H1, and it is a per-round fact rather than a
        # property of the bucket array.
        reach = [int(r["heap_threshold"]) - int(r["first_nonzero"])
                 for r in rounds if int(r["heap_threshold"]) >= 0]
        degenerate = sum(1 for x in reach if x <= 0)
        unreached = read_csv(os.path.join(outdir, "h1", name + ".vcount.csv"))
        unreachable = "-"
        if unreached:
            for row in unreached:
                if row["bucket"] == "unreached":
                    unreachable = row["vertices"]
        rows.append([
            name, total, len(used),
            "%d-%d" % (used[0], used[-1]) if used else "-",
            nbuckets,
            "%.1f" % (sum(occupied) / len(occupied)),
            "%.1f" % (sum(reach) / len(reach)) if reach else "-",
            "%.0f%%" % (100.0 * degenerate / len(reach)) if reach else "-",
            len(thresholds), len(rounds), unreachable,
        ])
    table(["graph", "updates", "buckets used", "range", "half in",
           "mean occ/round", "mean cut above frontier", "rounds with no reach",
           "distinct thr", "rounds", "unreached"], rows)

    pct = read_csv(os.path.join(outdir, "h1", "percentile.tsv"))
    if pct:
        print("    heap-percentile sweep. A controller with resolution to use")
        print("    should respond to this knob; one that has degenerated to")
        print("    'admit the frontier bucket' will not.")
        rows = []
        spread = {}
        for r in pct:
            rows.append([r["graph"], r["heap_percentile"],
                         r["compute_s"] or "FAILED", r["rejected_per_edge"],
                         r["threshold_changes"], r["reductions"]])
            if r["compute_s"]:
                spread.setdefault(r["graph"], []).append(float(r["compute_s"]))
        table(["graph", "p_heap", "compute_s", "rej/|E|", "thr changes",
               "rounds"], rows)
        for g, times in sorted(spread.items()):
            print("    %-8s slowest / fastest across the knob: %.2fx"
                  % (g, max(times) / min(times)))
        print()

    sweep = read_csv(os.path.join(outdir, "h1", "sweep.tsv"))
    if sweep:
        print("    bucket-width sweep (compute seconds, median of the reps)")
        rows = []
        for r in sweep:
            rows.append([r["graph"], r["multiplier"], "%.4g" % float(r["width"]),
                         r["compute_s"], r["rejected_per_edge"],
                         r["threshold_changes"], r["reductions"]])
        table(["graph", "x natural", "width", "compute_s", "rej/|E|",
               "thr changes", "rounds"], rows)
        best = {}
        for r in sweep:
            if not r["compute_s"]:
                continue
            t = float(r["compute_s"])
            if r["graph"] not in best or t < best[r["graph"]][0]:
                best[r["graph"]] = (t, r["multiplier"])
        for g, (t, m) in sorted(best.items()):
            base = next(float(r["compute_s"]) for r in sweep
                        if r["graph"] == g and r["multiplier"] == "1")
            print("    %-8s best at x%-7s %.4g s vs %.4g s at the current rule"
                  "  (%.2fx)" % (g, m, t, base, base / t))
        print()


# ----------------------------------------------------------------- H2 --------
def h2(outdir):
    print("H2  where the redundant traffic goes")
    print()
    for name in sorted({os.path.basename(p).split(".")[0]
                        for p in glob.glob(os.path.join(outdir, "h2", "*.degree.csv"))
                        } | {os.path.basename(p).split(".")[0]
                             for p in glob.glob(os.path.join(outdir, "h1", "*.degree.csv"))}):
        for sub in ("h2", "h1", "h4"):
            deg = read_csv(os.path.join(outdir, sub, name + ".degree.csv"))
            arr = read_csv(os.path.join(outdir, sub, name + ".arrivals.csv"))
            if deg:
                break
        if not deg:
            continue
        print("    %s: arrivals and rejects by destination out-degree" % name)
        rows = []
        tot_arr = sum(int(d["arrivals"]) for d in deg)
        tot_rej = sum(int(d["rejects"]) for d in deg)
        for d in deg:
            if int(d["vertices"]) == 0 and int(d["arrivals"]) == 0:
                continue
            a, rj = int(d["arrivals"]), int(d["rejects"])
            rows.append([d["min_degree"], d["vertices"], d["out_edges"], a, rj,
                         "%.1f%%" % (100.0 * rj / a) if a else "-",
                         "%.1f%%" % (100.0 * rj / tot_rej) if tot_rej else "-"])
        table(["deg >=", "vertices", "out-edges", "arrivals", "rejects",
               "reject rate", "share of rejects"], rows)

        if arr:
            # Walk down from the busiest class: what share of all arrivals
            # lands on what share of the vertices.
            tot_v = sum(int(a["vertices"]) for a in arr)
            tot_a = sum(int(a["arrivals"]) for a in arr)
            cv = ca = 0
            rows = []
            for a in reversed(arr):
                if int(a["vertices"]) == 0:
                    continue
                cv += int(a["vertices"])
                ca += int(a["arrivals"])
                rows.append([a["min_arrivals"], a["vertices"], a["arrivals"],
                             "%.3f%%" % (100.0 * cv / tot_v),
                             "%.1f%%" % (100.0 * ca / tot_a)])
            print("    %s: arrival concentration, busiest vertices first" % name)
            table(["arrivals >=", "vertices", "arrivals",
                   "cum. share of vertices", "cum. share of arrivals"], rows)

    absorb = read_csv(os.path.join(outdir, "h2", "absorb.tsv"))
    if absorb:
        print("    batch-local combining ceiling against aggregation buffer size")
        rows = []
        for r in absorb:
            items = int(r["batch_items"] or 0)
            ab = int(r["batch_absorbable"] or 0)
            rows.append([r["graph"], r["bufsize"], r["edges"],
                         r["rejected_per_edge"], items, ab,
                         "%.1f%%" % (100.0 * ab / items) if items else "-"])
        table(["graph", "bufsize", "edges", "rej/|E|", "batch items",
               "absorbable", "absorb rate"], rows)


# ----------------------------------------------------------------- H3 --------
PE_RE = re.compile(r"^DIAG_PE (\d+) (.*)$")


def pe_rows(path):
    rows = []
    with open(path) as f:
        for line in f:
            m = PE_RE.match(line.strip())
            if not m:
                continue
            d = {"pe": int(m.group(1))}
            for field in m.group(2).split():
                k, _, v = field.partition("=")
                d[k] = int(v)
            rows.append(d)
    return rows


def imbalance(values):
    if not values:
        return None
    mean = sum(values) / float(len(values))
    return (max(values) / mean) if mean else 1.0


def h3(outdir):
    print("H3  partition balance")
    print()
    rows = []
    for path in sorted(glob.glob(os.path.join(outdir, "h3", "*_pe*.log"))):
        base = os.path.basename(path)[: -len(".log")]
        name, _, pes = base.rpartition("_pe")
        pes = int(pes)
        pr = pe_rows(path)
        if not pr:
            continue
        # A PE can hold its fair share of the graph and still sit out most
        # of the run, because the frontier moves. idle_rounds is the rounds in
        # which a PE processed nothing at all.
        idle = [r.get("idle_rounds", 0) for r in pr]
        total_rounds = max([r.get("rounds", 0) for r in pr] + [0])
        rows.append([name, pes, len(pr),
                     "%.2f" % imbalance([r["edges"] for r in pr]),
                     "%.2f" % imbalance([r["updates_created"] for r in pr]),
                     "%.2f" % imbalance([r["updates_processed"] for r in pr]),
                     "%.0f%%" % (100.0 * sum(idle) / (len(pr) * total_rounds))
                     if total_rounds else "-",
                     "%.0f%%" % (100.0 * max(idle) / total_rounds)
                     if total_rounds else "-",
                     min(r["edges"] for r in pr), max(r["edges"] for r in pr)])
    rows.sort(key=lambda r: (r[0], r[1]))
    table(["graph", "PEs", "rows", "edges max/mean", "created max/mean",
           "processed max/mean", "mean idle rounds", "worst PE idle",
           "min edges", "max edges"], rows)

    jitter = read_csv(os.path.join(outdir, "h3", "jitter.tsv"))
    if jitter:
        print("    calibration: known skew injected into the uniform graph's")
        print("    partition, so the row above can be read as a measurement")
        print("    rather than as an instrument stuck at 1.0.")
        rows = []
        for r in jitter:
            log = os.path.join(outdir, "h3",
                               "jitter%s_diag.log" % r["jitter_percent"])
            pr = pe_rows(log) if os.path.exists(log) else []
            base = float(jitter[0]["compute_s"]) if jitter[0]["compute_s"] else None
            t = float(r["compute_s"]) if r["compute_s"] else None
            rows.append([r["jitter_percent"],
                         "%.2f" % imbalance([x["edges"] for x in pr])
                         if pr else "-",
                         "%.2f" % imbalance([x["updates_processed"] for x in pr])
                         if pr else "-",
                         r["compute_s"] or "FAILED", r["rejected_per_edge"],
                         "%.2fx" % (t / base) if t and base else "-"])
        table(["jitter %", "edges max/mean", "processed max/mean", "compute_s",
               "rej/|E|", "vs even"], rows)


# ----------------------------------------------------------------- H4 --------
def h4(outdir):
    print("H4  tail behaviour")
    print()
    rows = []
    for name in graphs_in(outdir, "h4"):
        rounds = read_csv(os.path.join(outdir, "h4", name + ".rounds.csv"))
        if not rounds or len(rounds) < 2:
            continue
        done = [int(r["done_vertices"]) for r in rounds]
        t = [float(r["t"]) for r in rounds]
        final = done[-1]
        if final == 0:
            # A build without VCOUNT cannot see settled vertices; fall back to
            # the update count, which moves for the same reason but counts
            # improvements rather than final answers.
            done = [int(r["updates_noted"]) for r in rounds]
            final = done[-1]
        # The tail starts at the first round holding 99% of the final progress.
        start = next((i for i, d in enumerate(done) if d >= 0.99 * final),
                     len(rounds) - 1)
        total_t = t[-1] or 1e-12
        tail_t = total_t - t[start]
        periods = [t[i + 1] - t[i] for i in range(len(t) - 1)]
        tail_periods = periods[start:] or [0.0]
        rows.append([
            name, len(rounds), "%.4g" % total_t,
            start, "%.4g" % t[start],
            len(rounds) - start, "%.4g" % tail_t,
            "%.1f%%" % (100.0 * tail_t / total_t),
            "%.3g" % (sum(tail_periods) / len(tail_periods) * 1e3),
            "%.3g" % (sum(periods) / len(periods) * 1e3),
        ])
    table(["graph", "rounds", "total_s", "tail starts at round", "at t",
           "tail rounds", "tail_s", "tail share", "tail ms/round",
           "mean ms/round"], rows)

    flush = read_csv(os.path.join(outdir, "h4", "flush.tsv"))
    if flush:
        print("    aggregation-flush rate. Flushing every round is the most a")
        print("    non-adaptive policy can do; if that does not shorten the")
        print("    run, the tail is not waiting on buffered updates.")
        rows = []
        base = {r["graph"]: float(r["compute_s"]) for r in flush
                if r["flush_interval"] == "5" and r["compute_s"]}
        for r in flush:
            t = float(r["compute_s"]) if r["compute_s"] else None
            b = base.get(r["graph"])
            rows.append([r["graph"], r["flush_interval"],
                         r["compute_s"] or "FAILED", r["reductions"],
                         r["rejected_per_edge"], r["tram_messages"],
                         "%.2fx" % (t / b) if t and b else "-"])
        table(["graph", "flush every", "compute_s", "rounds", "rej/|E|",
               "tram msgs", "vs the default 5"], rows)

    delay = read_csv(os.path.join(outdir, "h4", "delay.tsv"))
    if delay:
        print("    round-delay sweep. If the tail is cadence-bound, compute")
        print("    time rises by about (tail rounds x added delay).")
        rows = []
        base = {}
        for r in delay:
            if r["round_delay_ms"] == "0" and r["compute_s"]:
                base[r["graph"]] = float(r["compute_s"])
        for r in delay:
            t = float(r["compute_s"]) if r["compute_s"] else None
            b = base.get(r["graph"])
            rows.append([r["graph"], r["round_delay_ms"],
                         r["compute_s"] or "FAILED", r["reductions"],
                         r["rejected_per_edge"],
                         "%.2fx" % (t / b) if t and b else "-"])
        table(["graph", "delay ms", "compute_s", "rounds", "rej/|E|",
               "vs delay 0"], rows)


# ----------------------------------------------------------------- A/B --------
# Metrics read from each run log: (column, regex). Medians are taken per
# (graph, variant) over the repetitions that completed.
AB_METRICS = [
    ("compute_s", r"^Compute time: ([0-9.eE+-]+)"),
    ("rounds", r"^Number of reductions: ([0-9]+)"),
    ("rej/|E|", r"^Rejected updates normalized to \|E\|: ([0-9.eE+-]+)"),
    ("tram_msgs", r"^TRAM messages: ([0-9]+)"),
    ("bytes_sent", r"^TRAM messages: [0-9]+, bytes sent: ([0-9]+)"),
    ("stale_flushes", r"^TRAM stale-destination flushes: ([0-9]+)"),
]


def median(xs):
    xs = sorted(xs)
    n = len(xs)
    if n == 0:
        return None
    return xs[n // 2] if n % 2 else (xs[n // 2 - 1] + xs[n // 2]) / 2


def log_metrics(path):
    found = {}
    try:
        text = open(path).read()
    except OSError:
        return found
    for col, pattern in AB_METRICS:
        m = re.search(pattern, text, re.M)
        if m:
            found[col] = float(m.group(1))
    return found


def fmt(x):
    if x is None:
        return "-"
    if x == int(x) and abs(x) >= 10:
        return "%d" % x
    return "%.4g" % x


def ab(outdir):
    runs = read_csv(os.path.join(outdir, "ab", "runs.tsv"))
    if not runs:
        return
    order = []
    for r in runs:
        if r["variant"] not in order:
            order.append(r["variant"])
    baseline = order[0]
    samples = {}
    failed = {}
    for r in runs:
        key = (r["graph"], r["variant"])
        if r["status"] != "ok":
            failed[key] = failed.get(key, 0) + 1
            continue
        m = log_metrics(os.path.join(outdir, r["log"]))
        for col, v in m.items():
            samples.setdefault(key, {}).setdefault(col, []).append(v)
    print("A/B  baseline = %s; medians over repetitions, spread is min..max "
          "compute_s" % baseline)
    print()
    for graph in sorted({r["graph"] for r in runs}):
        base = median(samples.get((graph, baseline), {}).get("compute_s", []))
        rows = []
        for variant in order:
            s = samples.get((graph, variant), {})
            t = s.get("compute_s", [])
            med = median(t)
            row = [variant, fmt(med),
                   "%s..%s" % (fmt(min(t)), fmt(max(t))) if t else "-",
                   "%.2fx" % (med / base) if med and base else "-",
                   str(failed.get((graph, variant), 0))]
            for col, _ in AB_METRICS[1:]:
                row.append(fmt(median(s.get(col, []))))
            rows.append(row)
        print("  " + graph)
        table(["variant", "compute_s", "spread", "vs base", "failed"] +
              [c for c, _ in AB_METRICS[1:]], rows)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    outdir = sys.argv[1]
    which = sys.argv[2] if len(sys.argv) > 2 else "all"
    for name, fn in (("h1", h1), ("h2", h2), ("h3", h3), ("h4", h4), ("ab", ab)):
        if which in (name, "all") and os.path.isdir(os.path.join(outdir, name)):
            fn(outdir)
    return 0


if __name__ == "__main__":
    sys.exit(main())
