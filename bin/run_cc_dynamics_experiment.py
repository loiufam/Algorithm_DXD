#!/usr/bin/env python3
"""Collect the CC counters printed by single-thread DynDXD sampling runs."""

import argparse
import csv
import re
import subprocess
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

import run_cc_experiment as common


STAT_RE = re.compile(r"^CC Stats ([^:]+):\s*([\d.]+)$", re.MULTILINE)
TIME_RE = re.compile(r"^Time:\s*([\d.]+)\s*s", re.MULTILINE)

# Keep one CSV column for every CC Stats line emitted by the solver.  Apart
# from identifying/status columns, no derived rates or legacy counters are
# stored, so the CSV can be checked directly against solver output.
STAT_FIELDS = {
    "Complete": "stats_complete",
    "Decompose": "decompose",
    "CC Computations": "cc_computations",
    "ETT CC Times": "ett_cc_times",
    "BFS CC Times": "bfs_cc_times",
    "CC Graph Vertex Sum": "cc_graph_vertex_sum",
    "CC Graph Edge Sum": "cc_graph_edge_sum",
    "DXD BFS Vertex Scan Sum": "dxd_bfs_vertex_scan_sum",
    "DXD BFS Edge Scan Sum": "dxd_bfs_edge_scan_sum",
    "Dyn ETT Updated Vertex Sum": "dyn_ett_updated_vertex_sum",
    "Dyn ETT Updated Edge Sum": "dyn_ett_updated_edge_sum",
    "Dyn ETT Replacement Scan Steps": "dyn_ett_replacement_scan_steps",
    "Dyn BFS Vertex Scan Sum": "dyn_bfs_vertex_scan_sum",
    "Dyn BFS Edge Scan Sum": "dyn_bfs_edge_scan_sum",
}
IDENTITY_FIELDS = ("dataset", "instance", "input", "status", "validation_errors", "time_s")
RAW_FIELDS = IDENTITY_FIELDS + tuple(STAT_FIELDS.values())
SUMMARY_FIELDS = ("dataset", "valid_runs") + tuple(
    f"avg_{field}" for field in STAT_FIELDS.values() if field != "stats_complete"
)


def report_instances(report):
    """Use exactly the report selection shared with run_cc_experiment.py."""
    return common.eligible_instances(report)


def validate_stats(stats):
    """Return violated invariants for a fully rolled-back statistics sample."""
    errors = []
    check = lambda condition, message: errors.append(message) if not condition else None
    check(stats["CC Computations"] == stats["ETT CC Times"] + stats["BFS CC Times"],
          "CC Computations != ETT CC Times + BFS CC Times")
    check(stats["CC Graph Vertex Sum"] == stats["DXD BFS Vertex Scan Sum"],
          "CC graph vertex sum != all-BFS DXD vertex scans")
    check(stats["Dyn BFS Vertex Scan Sum"] <= stats["DXD BFS Vertex Scan Sum"],
          "Dyn BFS vertex scans exceed the all-BFS DXD baseline")
    check(stats["Dyn BFS Edge Scan Sum"] <= stats["DXD BFS Edge Scan Sum"],
          "Dyn BFS edge scans exceed the all-BFS DXD baseline")
    check(stats["Decompose"] <= stats["CC Computations"],
          "successful decompositions exceed CC computations")
    return errors


def parse_measurement(output, forced_partial=False):
    """Parse and validate the newest complete set of emitted CC Stats fields."""
    starts = [m.start() for m in re.finditer(r"^CC Stats Complete:", output, re.MULTILINE)]
    snapshots = []
    for index, start in enumerate(starts):
        end = starts[index + 1] if index + 1 < len(starts) else len(output)
        candidate = {name: float(value) for name, value in STAT_RE.findall(output[start:end])}
        if all(name in candidate for name in STAT_FIELDS):
            snapshots.append(candidate)
    if not snapshots:
        return None

    stats = snapshots[-1]
    errors = validate_stats(stats)
    solved = bool(stats["Complete"]) and not forced_partial
    result = {
        output_name: int(stats[log_name])
        for log_name, output_name in STAT_FIELDS.items()
    }
    time_match = TIME_RE.search(output)
    result.update({
        "status": "invalid" if errors else ("success" if solved else "sampled"),
        "validation_errors": "; ".join(errors),
        "time_s": time_match.group(1) if time_match else "",
    })
    return result


def run_case(executable, input_path, ett_row_threshold=0):
    """Run one case to completion; partial/timeout samples are not meaningful."""
    command = [str(executable), "-a", "ddxd", "-i", str(input_path), "-t", "1",
               "--enable-cc-stats", "--cc-ett-threshold", str(ett_row_threshold)]
    process = subprocess.run(
        command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
    )
    if process.returncode != 0:
        return {"status": f"error({process.returncode})", "validation_errors": "solver process failed"}
    return parse_measurement(process.stdout) or {"status": "missing_stats", "validation_errors": "no complete snapshot"}


def write_csv(path, fields, rows):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def summaries(raw_rows):
    grouped = defaultdict(list)
    for row in raw_rows:
        if row["status"] in {"success", "sampled"}:
            grouped[row["dataset"]].append(row)
        else:
            grouped[row["dataset"]]
    result = []
    numeric = [field for field in STAT_FIELDS.values() if field != "stats_complete"]
    for dataset, rows in sorted(grouped.items()):
        summary = {"dataset": dataset, "valid_runs": len(rows)}
        if rows:
            for field in numeric:
                summary[f"avg_{field}"] = sum(float(row[field]) for row in rows) / len(rows)
        result.append(summary)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=Path, default=common.DEFAULT_REPORT)
    parser.add_argument("--executable", type=Path, default=common.ROOT / "bin/main")
    parser.add_argument("--raw-output", type=Path, default=common.ROOT / "results/cc_dynamics_instances.csv")
    parser.add_argument("--summary-output", type=Path, default=common.ROOT / "results/cc_dynamics_summary.csv")
    parser.add_argument(
        "--cc-ett-threshold", type=int, default=0,
        help=("active-row boundary for ETT statistics (default: 0/auto: "
              ">2000=>200, >1000=>100, >100=>50, otherwise 30)"),
    )
    parser.add_argument(
        "--dataset", action="append", dest="datasets",
        help="run only this input-directory dataset; may be repeated",
    )
    parser.add_argument(
        "--workers", type=int, default=4,
        help="number of cases to run concurrently (default: 4)",
    )
    parser.add_argument("--limit", type=int)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--resume", action="store_true")
    args = parser.parse_args()
    if args.workers < 1:
        parser.error("--workers must be at least 1")
    if args.cc_ett_threshold < 0:
        parser.error("--cc-ett-threshold must be non-negative (0 means auto)")

    inputs = common.input_index(common.DEFAULT_INPUT_DIRS)
    selected = report_instances(args.report)
    known_datasets = {path.parent.name for path in inputs.values()}
    requested_datasets = set(args.datasets or ())
    unknown_datasets = requested_datasets - known_datasets
    if unknown_datasets:
        parser.error(
            "unknown --dataset value(s): " + ", ".join(sorted(unknown_datasets)) +
            "; choose from: " + ", ".join(sorted(known_datasets))
        )
    if requested_datasets:
        selected = [
            item for item in selected
            if item["instance"] in inputs and
            inputs[item["instance"]].parent.name in requested_datasets
        ]
    if args.limit is not None:
        selected = selected[:args.limit]
    runnable = [item for item in selected if item["instance"] in inputs]
    print(f"report selected {len(selected)} cases; {len(runnable)} input files found")
    if args.dry_run:
        for item in runnable:
            print(inputs[item["instance"]])
        return 0
    if not args.executable.is_file():
        parser.error(f"executable not found: {args.executable}")

    raw = []
    if args.resume and args.raw_output.is_file():
        with args.raw_output.open(encoding="utf-8", newline="") as stream:
            raw = list(csv.DictReader(stream))
    completed = {
        (row["dataset"], row["instance"])
        for row in raw if row["status"] in {"success", "sampled"}
    }
    pending = []
    for number, item in enumerate(runnable, 1):
        path = inputs[item["instance"]]
        key = (path.parent.name, item["instance"])
        if key not in completed:
            pending.append((number, item, path))

    def execute(entry):
        number, item, path = entry
        print(f"[{number}/{len(runnable)}] [{path.parent.name}] {item['instance']}", flush=True)
        return entry, run_case(args.executable, path, args.cc_ett_threshold)

    with ThreadPoolExecutor(max_workers=args.workers) as executor:
        futures = [executor.submit(execute, entry) for entry in pending]
        for future in as_completed(futures):
            (_, item, path), measured = future.result()
            name = item["instance"]
            dataset = path.parent.name
            row = {field: "" for field in RAW_FIELDS}
            row.update(measured)
            row.update({"dataset": dataset, "instance": name,
                        "input": str(path.relative_to(common.ROOT))})
            raw = [
                old for old in raw
                if (old["dataset"], old["instance"]) != (dataset, name)
            ]
            raw.append(row)
            raw.sort(key=lambda value: (value["dataset"], value["instance"]))
            write_csv(args.raw_output, RAW_FIELDS, raw)
            write_csv(args.summary_output, SUMMARY_FIELDS, summaries(raw))

    print(f"wrote {len(raw)} instance rows to {args.raw_output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
