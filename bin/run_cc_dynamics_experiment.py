#!/usr/bin/env python3
"""Measure full DynDXD dynamic-connectivity behavior without adaptive shutdown."""

import argparse
import csv
import re
import subprocess
from collections import defaultdict
from pathlib import Path

import run_cc_experiment as common


STAT_RE = re.compile(r"^CC Stats ([^:]+):\s*([\d.]+)$", re.MULTILINE)
TIME_RE = re.compile(r"^Time:\s*([\d.]+)\s*s", re.MULTILINE)
CC_TIME_RE = re.compile(r"^Dyn CC CPU:\s*([\d.]+)\s*s", re.MULTILINE)
CC_RATIO_RE = re.compile(r"^Dyn CC CPU Ratio:\s*([\d.]+)", re.MULTILINE)
SOLUTION_RE = re.compile(r"^Solutions:\s*(\S+)", re.MULTILINE)

RAW_FIELDS = (
    "dataset", "instance", "input", "status", "stats_complete", "time_s", "cc_cpu_s", "cc_cpu_ratio", "solutions",
    "cc_calls", "dec_cc_calls", "inc_cc_calls", "merges", "tree_edge_cuts", "splits",
    "merge_per_cc", "split_per_cc", "avg_v", "avg_e", "avg_vd", "avg_ed",
    "avg_en_per_update", "avg_en_per_ed", "replacement_search_calls",
    "replacement_scan_steps", "avg_scan_per_search", "early_break_rate", "full_scan_rate",
)
SUMMARY_FIELDS = (
    "dataset", "solved", "avg_cc_calls", "avg_merges", "avg_splits",
    "merge_per_cc", "split_per_cc", "avg_v", "avg_e", "avg_vd", "avg_ed",
    "avg_en", "avg_en_per_ed", "avg_scan", "early_break_rate", "full_scan_rate",
)


def report_instances(report):
    rows = iter(common.read_xlsx_rows(report))
    header = next(rows)
    name_col = header.index("Instance")
    dyn_col = header.index("DynDXD-T1")
    selected = []
    for row in rows:
        row += [""] * (len(header) - len(row))
        try:
            float(row[dyn_col])
        except ValueError:
            continue
        selected.append(row[name_col].strip())
    return selected


def ratio(numerator, denominator):
    return numerator / denominator if denominator else 0.0


def parse_measurement(output, forced_partial=False):
    """Parse the last complete counter snapshot emitted by the solver."""
    required = ("Complete", "Calls", "Dec Calls", "Inc Calls", "Merges", "Tree Edge Cuts", "Splits",
                "Vertex Sum", "Edge Sum", "Update Vertex Sum", "Update Edge Sum",
                "En Samples", "En Sum", "En Positive Updates", "En Update Average Sum",
                "Replacement Searches", "Replacement Scan Steps", "Early Breaks", "Full Scans")
    # A hard kill may interrupt the newest multi-line snapshot.  Split at each
    # Complete marker and use only the newest snapshot containing every field,
    # rather than combining a partial new snapshot with stale older values.
    starts = [match.start() for match in re.finditer(r"^CC Stats Complete:", output, re.MULTILINE)]
    snapshots = []
    for index, start in enumerate(starts):
        end = starts[index + 1] if index + 1 < len(starts) else len(output)
        candidate = {name: float(value) for name, value in STAT_RE.findall(output[start:end])}
        if all(name in candidate for name in required):
            snapshots.append(candidate)
    if not snapshots:
        return None
    stats = snapshots[-1]

    calls = stats["Calls"]
    searches = stats["Replacement Searches"]
    en_samples = stats["En Samples"]
    en_updates = stats["En Positive Updates"]
    time_match = TIME_RE.search(output)
    cc_time_match = CC_TIME_RE.search(output)
    cc_ratio_match = CC_RATIO_RE.search(output)
    solution_match = SOLUTION_RE.search(output)
    complete = bool(stats["Complete"]) and not forced_partial
    return {
        "status": "success" if complete else "timeout_partial",
        "stats_complete": int(complete),
        "time_s": time_match.group(1) if time_match else "",
        "cc_cpu_s": cc_time_match.group(1) if cc_time_match else "",
        "cc_cpu_ratio": cc_ratio_match.group(1) if cc_ratio_match else "",
        "solutions": solution_match.group(1) if solution_match else "",
        "cc_calls": int(calls),
        "dec_cc_calls": int(stats["Dec Calls"]),
        "inc_cc_calls": int(stats["Inc Calls"]),
        "merges": int(stats["Merges"]),
        "tree_edge_cuts": int(stats["Tree Edge Cuts"]),
        "splits": int(stats["Splits"]),
        "merge_per_cc": ratio(stats["Merges"], calls),
        "split_per_cc": ratio(stats["Splits"], calls),
        "avg_v": ratio(stats["Vertex Sum"], calls),
        "avg_e": ratio(stats["Edge Sum"], calls),
        "avg_vd": ratio(stats["Update Vertex Sum"], calls),
        "avg_ed": ratio(stats["Update Edge Sum"], calls),
        "avg_en_per_update": ratio(stats["En Update Average Sum"], en_updates),
        "avg_en_per_ed": ratio(stats["En Sum"], en_samples),
        "replacement_search_calls": int(searches),
        "replacement_scan_steps": int(stats["Replacement Scan Steps"]),
        "avg_scan_per_search": ratio(stats["Replacement Scan Steps"], searches),
        "early_break_rate": ratio(stats["Early Breaks"], searches),
        "full_scan_rate": ratio(stats["Full Scans"], searches),
    }


def run_case(executable, input_path, timeout):
    command = [str(executable), "-a", "ddxd", "-i", str(input_path),
               "-t", "1", "--full-cc-stats", "--time-limit", str(timeout)]
    try:
        # Let the solver hit its own bound and flush partial counters cleanly.
        process = subprocess.run(command, capture_output=True, text=True, timeout=timeout + 30)
    except subprocess.TimeoutExpired as error:
        # TimeoutExpired retains output captured before subprocess.run killed the
        # child.  The solver emits periodic counter snapshots, so even a long,
        # non-interruptible CC update leaves a recent valid censored row.
        stdout = error.stdout or ""
        stderr = error.stderr or ""
        if isinstance(stdout, bytes):
            stdout = stdout.decode(errors="replace")
        if isinstance(stderr, bytes):
            stderr = stderr.decode(errors="replace")
        return parse_measurement(stdout + stderr, forced_partial=True) or {"status": "timeout"}

    output = process.stdout + process.stderr
    if process.returncode != 0:
        return {"status": f"error({process.returncode})"}
    return parse_measurement(output) or {"status": "missing_stats"}


def write_csv(path, fields, rows):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def summaries(raw_rows):
    grouped = defaultdict(list)
    for row in raw_rows:
        grouped[row["dataset"]]
        if row["status"] == "success":
            grouped[row["dataset"]].append(row)
    mapping = {
        "avg_cc_calls": "cc_calls", "avg_merges": "merges", "avg_splits": "splits",
        "merge_per_cc": "merge_per_cc", "split_per_cc": "split_per_cc",
        "avg_v": "avg_v", "avg_e": "avg_e", "avg_vd": "avg_vd", "avg_ed": "avg_ed",
        "avg_en": "avg_en_per_update", "avg_en_per_ed": "avg_en_per_ed",
        "avg_scan": "avg_scan_per_search", "early_break_rate": "early_break_rate",
        "full_scan_rate": "full_scan_rate",
    }
    result = []
    for dataset, rows in sorted(grouped.items()):
        summary = {"dataset": dataset, "solved": len(rows)}
        if rows:
            for output_name, raw_name in mapping.items():
                summary[output_name] = sum(float(row[raw_name]) for row in rows) / len(rows)
        result.append(summary)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=Path, default=common.DEFAULT_REPORT)
    parser.add_argument("--executable", type=Path, default=common.ROOT / "bin/main-1")
    parser.add_argument("--raw-output", type=Path,
                        default=common.ROOT / "results/cc_dynamics_instances.csv")
    parser.add_argument("--summary-output", type=Path,
                        default=common.ROOT / "results/cc_dynamics_summary.csv")
    parser.add_argument("--timeout", type=int, default=600)
    parser.add_argument("--limit", type=int)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--resume", action="store_true",
                        help="keep successful rows and rerun timeout/error rows")
    args = parser.parse_args()

    inputs = common.input_index(common.DEFAULT_INPUT_DIRS)
    names = report_instances(args.report)
    if args.limit is not None:
        names = names[:args.limit]
    missing = [name for name in names if name not in inputs]
    if missing:
        parser.error(f"{len(missing)} report instances have no input file")
    if args.dry_run:
        for name in names:
            print(inputs[name])
        return 0
    if not args.executable.is_file():
        parser.error(f"executable not found: {args.executable}")

    raw = []
    if args.resume and args.raw_output.is_file():
        with args.raw_output.open(encoding="utf-8", newline="") as stream:
            raw = list(csv.DictReader(stream))
    completed = {
            row["instance"] for row in raw 
            if row["status"] in {"success", "timeout_partial"}
        }
    for number, name in enumerate(names, 1):
        if name in completed:
            print(f"[{number}/{len(names)}] {name} (already recorded)", flush=True)
            continue
        path = inputs[name]
        print(f"[{number}/{len(names)}] {name}", flush=True)
        measured = run_case(args.executable, path, args.timeout)
        row = {field: "" for field in RAW_FIELDS}
        row.update(measured)
        row.update({"dataset": path.parent.name, "instance": name,
                    "input": str(path.relative_to(common.ROOT))})
        raw = [existing for existing in raw if existing["instance"] != name]
        raw.append(row)
        write_csv(args.raw_output, RAW_FIELDS, raw)
        write_csv(args.summary_output, SUMMARY_FIELDS, summaries(raw))

    print(f"wrote {len(raw)} instance rows to {args.raw_output}")
    print(f"wrote dataset averages to {args.summary_output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
