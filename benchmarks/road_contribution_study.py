#!/usr/bin/env python3
"""Run and summarize the bounded contribution-placement screen on Delta."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import statistics
import subprocess
import sys

from work_cost_report import production_attempts


def geometric_mean(values):
    return math.exp(statistics.fmean(math.log(value) for value in values))


def median(rows, key):
    return statistics.median(row[key] for row in rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("campaign", type=Path)
    parser.add_argument("--protocol", type=Path,
                        default=Path(__file__).with_name("delta-road-contribution-protocol.json"))
    args = parser.parse_args()
    root = args.campaign.resolve()
    app = Path(__file__).resolve().parents[1]
    protocol = json.loads(args.protocol.read_text())
    candidate = protocol.get("candidate_label", "post_work")
    thresholds = protocol.get("thresholds", {})
    job = os.environ["SLURM_JOB_ID"]
    protocol_dir = root / "protocol"
    protocol_dir.mkdir(parents=True, exist_ok=True)
    frozen_protocol = protocol_dir / f"road-contribution-{job}.json"
    frozen_protocol.write_text(json.dumps(protocol, indent=2) + "\n")

    variants = []
    for variant in protocol["variants"]:
        variants.append({**variant, "flags": protocol["common_flags"]})
    variant_path = protocol_dir / f"road-contribution-{job}.variants.json"
    variant_path.write_text(json.dumps(variants, indent=2) + "\n")

    for graph in protocol["graphs"]:
        command = [sys.executable, str(app / "benchmarks" / "onenode_ab.py"),
                   str(root), graph["name"], str(variant_path),
                   "--workers", "112", "--rpn", "16",
                   "--source-role", graph["role"],
                   "--sources", str(graph["sources"]),
                   "--reps", str(graph["reps"])]
        subprocess.run(command, check=True)

    summaries = {}
    total_solves = 0
    placement_lines = {
        "immediate": "Contribution placement: immediate",
        "control": "Contribution placement: immediate",
        "post_work": "Contribution placement: after one bounded local work turn (experimental build)",
        "fused_post_work": "Contribution placement: after one bounded local work turn (fused experimental build)",
    }
    labels = [variant["label"] for variant in protocol["variants"]]
    expected_placement = {label: placement_lines[label] for label in labels}
    for graph in protocol["graphs"]:
        name = graph["name"]
        directory = root / "logs" / f"AB-{name}-1n-{job}"
        manifest = json.loads((directory / "manifest.json").read_text())
        if (manifest["workers"], manifest["rpn"], manifest["sources"], manifest["reps"]) != (
                112, 16, graph["sources"], graph["reps"]):
            raise ValueError(f"{name}: wrong layout or sample counts")
        manifest_variants = {variant["label"]: variant for variant in manifest["variants"]}
        if set(manifest_variants) != set(expected_placement):
            raise ValueError(f"{name}: wrong variants")
        if manifest_variants["immediate"]["sha256"] != manifest_variants["control"]["sha256"]:
            raise ValueError("duplicate controls do not use the same binary")
        if manifest_variants["immediate"]["sha256"] == manifest_variants[candidate]["sha256"]:
            raise ValueError("candidate and immediate binaries are identical")

        meta = (root / "graphs" / f"{name}.meta").read_text()
        vertices = int(re.search(r"vertices=(\d+)", meta)[1])
        rows = [json.loads(line) for line in (directory / "runs.jsonl").read_text().splitlines()]
        expected = len(expected_placement) * graph["sources"] * (graph["reps"] + 1)
        if len(rows) != expected or not all(row["valid"] for row in rows):
            raise ValueError(f"{name}: missing or invalid solve")
        total_solves += len(rows)
        for row in rows:
            log = directory / f"{row['variant']}-s{row['source_index']}-r{row['rep']}.out"
            text = log.read_text()
            required = [expected_placement[row["variant"]], "Using the original scheduler (+old-scheduler)",
                        "Process sharing: on", "Process queue: nearest",
                        "Process queue batch: 8", "Heap slice: 8"]
            if any(item not in text for item in required):
                raise ValueError(f"{log}: configuration mismatch")
            row["rounds"] = text.count("Updates: created:")
            row["edge_attempts"] = production_attempts(text, vertices)

        sources = []
        for source_index in range(graph["sources"]):
            cells = {}
            for label in expected_placement:
                timed = [row for row in rows if row["variant"] == label
                         and row["source_index"] == source_index and row["rep"] >= 0]
                if len(timed) != graph["reps"]:
                    raise ValueError(f"{name}/{label}/{source_index}: wrong repetitions")
                cells[label] = {
                    "seconds": median(timed, "seconds"),
                    "rounds": median(timed, "rounds"),
                    "edge_attempts": median(timed, "edge_attempts"),
                    "samples": [row["seconds"] for row in timed],
                }
            reference = {key: math.sqrt(cells["immediate"][key] * cells["control"][key])
                         for key in ("seconds", "rounds", "edge_attempts")}
            sources.append({
                "source_index": source_index,
                "source": next(row["source"] for row in rows if row["source_index"] == source_index),
                "variants": cells,
                "duplicate_reference": reference,
                "candidate_speedup": reference["seconds"] / cells[candidate]["seconds"],
                "candidate_round_ratio": cells[candidate]["rounds"] / reference["rounds"],
                "candidate_work_ratio": cells[candidate]["edge_attempts"] / reference["edge_attempts"],
            })
        noise = math.exp(max(abs(math.log(source["variants"]["immediate"]["seconds"] /
                                          source["variants"]["control"]["seconds"]))
                             for source in sources))
        summaries[name] = {
            "directory": str(directory),
            "binary_sha256": {label: value["sha256"] for label, value in manifest_variants.items()},
            "sources": sources,
            "geomean_speedup": geometric_mean(source["candidate_speedup"] for source in sources),
            "geomean_round_ratio": geometric_mean(source["candidate_round_ratio"] for source in sources),
            "geomean_work_ratio": geometric_mean(source["candidate_work_ratio"] for source in sources),
            "duplicate_timing_floor": noise,
        }

    road = summaries["road-usa-z"]
    mesh = summaries["mesh24-z"]
    road_faster_than_both = sum(
        source["variants"][candidate]["seconds"] < min(
            source["variants"]["immediate"]["seconds"],
            source["variants"]["control"]["seconds"])
        for source in road["sources"])
    mesh_worst_slowdown = max(1.0 / source["candidate_speedup"] for source in mesh["sources"])
    road_speedup_gate = thresholds.get("road_geomean_speedup", 1.10)
    road_source_gate = thresholds.get("road_faster_than_both_sources", 3)
    road_work_gate = thresholds.get("road_geomean_work_ratio", 1.10)
    mesh_floor = thresholds.get("mesh_slowdown_floor", 1.05)
    gates = {
        "road_geomean_speedup": road["geomean_speedup"] >= road_speedup_gate,
        "road_faster_than_both_sources": road_faster_than_both >= road_source_gate,
        "road_work_growth": road["geomean_work_ratio"] <= road_work_gate,
        "mesh_within_regression_limit": mesh_worst_slowdown <= max(mesh_floor, mesh["duplicate_timing_floor"]),
    }
    result = {
        "status": "SCREEN_PASS_CONFIRM" if all(gates.values()) else "REJECT",
        "job": job,
        "host": os.environ.get("SLURM_JOB_NODELIST"),
        "candidate": candidate,
        "protocol_sha256": hashlib.sha256(frozen_protocol.read_bytes()).hexdigest(),
        "validated_solves": total_solves,
        "gates": gates,
        "road_faster_than_both_sources": road_faster_than_both,
        "mesh_worst_slowdown": mesh_worst_slowdown,
        "graphs": summaries,
    }
    output = root / "logs" / f"contribution-{job}"
    output.mkdir()
    (output / "summary.json").write_text(json.dumps(result, indent=2) + "\n")
    lines = [
        f"# Contribution-placement screen (Delta job {job})",
        "",
        f"Decision: **{result['status']}**; {total_solves} digest-checked solves.",
        "",
        "| Graph | Time speedup | Round ratio | Edge-work ratio | Duplicate timing floor |",
        "|---|---:|---:|---:|---:|",
    ]
    for name in ("road-usa-z", "mesh24-z"):
        data = summaries[name]
        lines.append(f"| `{name}` | {data['geomean_speedup']:.3f}x | "
                     f"{data['geomean_round_ratio']:.3f} | {data['geomean_work_ratio']:.3f} | "
                     f"{data['duplicate_timing_floor']:.3f} |")
    lines += ["", "Gates:"] + [f"- {'PASS' if passed else 'FAIL'}: `{name}`"
                                           for name, passed in gates.items()]
    (output / "report.md").write_text("\n".join(lines) + "\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
