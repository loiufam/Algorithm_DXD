#!/usr/bin/env python3
"""Collect the CC counters printed by single-thread DynDXD sampling runs."""

import argparse
import csv
import re
import subprocess
from collections import defaultdict
from pathlib import Path

import run_cc_experiment as common


STAT_RE = re.compile(r"^CC Stats ([^:]+):\s*([\d.]+)$", re.MULTILINE)
TIME_RE = re.compile(r"^Time:\s*([\d.]+)\s*s", re.MULTILINE)
START_MARKER = "开始多线程DXD搜索..."

# Keep one CSV column for every CC Stats line emitted by the solver.  Apart
# from identifying/status columns, no derived rates or legacy counters are
# stored, so the CSV can be checked directly against solver output.
STAT_FIELDS = {
    "Complete": "stats_complete",
    "Calls": "cc_calls",
    "Dec Calls": "dec_cc_calls",
    "Inc Calls": "inc_cc_calls",
    "Merges": "merges",
    "Tree Edge Cuts": "tree_edge_cuts",
    "Splits": "splits",
    "Decompose": "decompose",
    "Dec Vertex Sum": "dec_vertex_sum",
    "Dec Edge Sum": "dec_edge_sum",
    "DecUpdate Vertex Sum": "dec_update_vertex_sum",
    "DecUpdate Edge Sum": "dec_update_edge_sum",
    "Inc Vertex Sum": "inc_vertex_sum",
    "Inc Edge Sum": "inc_edge_sum",
    "IncUpdate Vertex Sum": "inc_update_vertex_sum",
    "IncUpdate Edge Sum": "inc_update_edge_sum",
    "En Sum": "en_sum",
    "Replacement Searches": "replacement_searches",
    "Replacement Scan Steps": "replacement_scan_steps",
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
    check(stats["Calls"] == stats["Dec Calls"] + stats["Inc Calls"],
          "Calls != Dec Calls + Inc Calls")
    check(stats["Dec Calls"] == stats["Inc Calls"],
          "Dec Calls != Inc Calls (search did not fully roll back)")
    check(stats["Tree Edge Cuts"] == stats["Replacement Searches"],
          "Tree Edge Cuts != Replacement Searches")
    check(stats["Merges"] + stats["Splits"] == stats["Tree Edge Cuts"],
          "Merges + Splits != Tree Edge Cuts")
    check(stats["Replacement Scan Steps"] <= stats["En Sum"],
          "Replacement Scan Steps > En Sum")
    check(stats["DecUpdate Vertex Sum"] <= stats["Dec Vertex Sum"],
          "DecUpdate Vertex Sum > Dec Vertex Sum")
    check(stats["DecUpdate Edge Sum"] <= stats["Dec Edge Sum"],
          "DecUpdate Edge Sum > Dec Edge Sum")
    check(stats["IncUpdate Vertex Sum"] <= stats["Inc Vertex Sum"],
          "IncUpdate Vertex Sum > Inc Vertex Sum")
    check(stats["IncUpdate Edge Sum"] <= stats["Inc Edge Sum"],
          "IncUpdate Edge Sum > Inc Edge Sum")
    check(stats["DecUpdate Vertex Sum"] == stats["IncUpdate Vertex Sum"],
          "decremental/incremental update vertex sums differ")
    check(stats["DecUpdate Edge Sum"] == stats["IncUpdate Edge Sum"],
          "decremental/incremental update edge sums differ")
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


def run_case(executable, input_path, search_seconds, process_timeout=None):
    """Wait through initialization, then apply the subprocess safety timeout."""
    if process_timeout is None:
        # Backward-compatible entry point for the focused merge/cut runner.
        process_timeout = search_seconds + 30
    command = [str(executable), "-a", "ddxd", "-i", str(input_path), "-t", "1",
               "--full-cc-stats", "--time-limit", str(search_seconds)]
    process = subprocess.Popen(
        command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, bufsize=1,
    )
    prefix = []
    assert process.stdout is not None
    # Initialization is deliberately not timed.  The safety timeout starts
    # only after the solver confirms that the algorithm itself has begun.
    for line in process.stdout:
        prefix.append(line)
        if START_MARKER in line:
            break
    else:
        process.wait()
        return {"status": f"error({process.returncode})", "validation_errors": "algorithm did not start"}

    try:
        suffix, _ = process.communicate(timeout=process_timeout)
    except subprocess.TimeoutExpired:
        process.kill()
        suffix, _ = process.communicate()
        measured = parse_measurement("".join(prefix) + suffix, forced_partial=True)
        return measured or {"status": "timeout", "validation_errors": "process safety timeout"}

    output = "".join(prefix) + suffix
    if process.returncode != 0:
        return {"status": f"error({process.returncode})", "validation_errors": "solver process failed"}
    return parse_measurement(output) or {"status": "missing_stats", "validation_errors": "no complete snapshot"}


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
    parser.add_argument("--search-seconds", type=int, default=30,
                        help="gracefully stop search and roll back after this many seconds")
    parser.add_argument("--timeout", type=int, default=60,
                        help="safety timeout after the algorithm starts")
    parser.add_argument("--limit", type=int)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--resume", action="store_true")
    args = parser.parse_args()
    if args.search_seconds < 1 or args.timeout <= args.search_seconds:
        parser.error("require 1 <= --search-seconds < --timeout")

    inputs = common.input_index(common.DEFAULT_INPUT_DIRS)
    selected = report_instances(args.report)
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
    completed = {row["instance"] for row in raw if row["status"] in {"success", "invalid"}}
    for number, item in enumerate(runnable, 1):
        name = item["instance"]
        if name in completed:
            continue
        path = inputs[name]
        print(f"[{number}/{len(runnable)}] {name}", flush=True)
        measured = run_case(args.executable, path, args.search_seconds, args.timeout)
        row = {field: "" for field in RAW_FIELDS}
        row.update(measured)
        row.update({"dataset": path.parent.name, "instance": name,
                    "input": str(path.relative_to(common.ROOT))})
        raw = [old for old in raw if old["instance"] != name]
        raw.append(row)
        write_csv(args.raw_output, RAW_FIELDS, raw)
        write_csv(args.summary_output, SUMMARY_FIELDS, summaries(raw))

    print(f"wrote {len(raw)} instance rows to {args.raw_output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
