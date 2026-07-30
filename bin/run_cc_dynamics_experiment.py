#!/usr/bin/env python3
"""Run CC-stat experiments, one sequential runner per benchmark dataset."""

import argparse
import csv
import re
import subprocess
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

import run_cc_experiment as common


STAT_RE = re.compile(r"^CC Stats ([^:]+):\s*([\d.]+)$", re.MULTILINE)
TIME_RE = re.compile(r"^Time:\s*([\d.]+)\s*s", re.MULTILINE)

# These names deliberately mirror logCCExperimentStats().  Keeping this map in
# log order makes a raw CSV row directly comparable with the solver output.
STAT_FIELDS = {
    "Complete": "stats_complete",
    "Init Graph Edges": "init_graph_edges",
    "Query": "query",
    "Splits": "splits",
    "ETT CC Times": "ett_cc_times",
    "ETT Dec Calls": "ett_dec_calls",
    "ETT DXD Vertex Sum": "ett_dxd_vertex_sum",
    "ETT DXD Edge Sum": "ett_dxd_edge_sum",
    "Dyn ETT Updated Vertex Sum": "dyn_ett_updated_vertex_sum",
    "Dyn ETT Updated Edge Sum": "dyn_ett_updated_edge_sum",
    "Dyn ETT Deleted Tree Edge Sum": "dyn_ett_deleted_tree_edge_sum",
    "Dyn ETT Replacement Scan Steps": "dyn_ett_replacement_scan_steps",
}
IDENTITY_FIELDS = (
    "dataset", "instance", "input", "ett_max_calls", "status", "time_s", "error",
)
CSV_FIELDS = IDENTITY_FIELDS + tuple(STAT_FIELDS.values())
SUCCESS_STATUSES = {"success"}


def report_instances(report):
    """Use exactly the report selection shared with run_cc_experiment.py."""
    return common.eligible_instances(report)


def parse_measurement(output):
    """Parse the last complete CC Stats block emitted by the solver."""
    starts = [m.start() for m in re.finditer(r"^CC Stats Complete:", output, re.MULTILINE)]
    snapshots = []
    for index, start in enumerate(starts):
        end = starts[index + 1] if index + 1 < len(starts) else len(output)
        candidate = {name: value for name, value in STAT_RE.findall(output[start:end])}
        if all(name in candidate for name in STAT_FIELDS):
            snapshots.append(candidate)
    if not snapshots:
        return None

    stats = snapshots[-1]
    result = {
        csv_name: int(float(stats[log_name]))
        for log_name, csv_name in STAT_FIELDS.items()
    }
    complete = bool(result["stats_complete"])
    time_match = TIME_RE.search(output)
    result.update({
        "status": "success" if complete else "time_out",
        "time_s": time_match.group(1) if time_match else "",
        "error": "" if complete else "solver time limit reached",
    })
    return result


def run_case(executable, input_path, timeout, ett_row_threshold=0, ett_max_calls=0):
    """Run one case and convert all failures into a row so the batch continues."""
    command = [
        str(executable), "-a", "ddxd", "-i", str(input_path), "-t", "1",
        "--enable-cc-stats", "--cc-ett-threshold", str(ett_row_threshold),
        "--cc-ett-max-calls", str(ett_max_calls),
        "--time-limit", str(timeout),
    ]
    try:
        # The solver normally stops itself at --time-limit and emits a partial
        # statistics block.  The grace period only protects the experiment
        # runner if the solver stops responding altogether.
        process = subprocess.run(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=timeout + 30,
        )
    except subprocess.TimeoutExpired:
        return {"status": "time_out", "time_s": "", "error": "process timeout"}
    except OSError as error:
        return {"status": "error", "time_s": "", "error": str(error)}

    measured = parse_measurement(process.stdout)
    if measured is not None:
        if process.returncode != 0 and measured["status"] == "success":
            measured.update(status="error", error=f"solver exited with {process.returncode}")
        return measured
    if process.returncode != 0:
        return {
            "status": "error", "time_s": "",
            "error": f"solver exited with {process.returncode}; no CC Stats block",
        }
    return {"status": "missing_stats", "time_s": "", "error": "no CC Stats block"}


def write_csv(path, rows):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=CSV_FIELDS)
        writer.writeheader()
        writer.writerows(rows)


def read_csv(path):
    if not path.is_file():
        return []
    with path.open(encoding="utf-8", newline="") as stream:
        return list(csv.DictReader(stream))


def dataset_output(output_dir, dataset):
    return output_dir / f"cc_dynamics_{dataset}.csv"


def run_dataset(dataset, entries, args):
    """Run every case in one dataset sequentially and checkpoint each row."""
    output = dataset_output(args.output_dir, dataset)
    rows = read_csv(output) if args.resume else []
    completed_statuses = SUCCESS_STATUSES | ({"time_out"} if args.cc_ett_max_calls == 0 else set())
    completed = {
        row["instance"] for row in rows
        if row.get("status") in completed_statuses
    }

    for number, (item, path) in enumerate(entries, 1):
        name = item["instance"]
        if name in completed:
            print(f"[{dataset} {number}/{len(entries)}] {name} (already recorded)", flush=True)
            continue
        print(f"[{dataset} {number}/{len(entries)}] {name}", flush=True)
        try:
            measured = run_case(
                args.executable, path, args.timeout,
                args.cc_ett_threshold, args.cc_ett_max_calls,
            )
        except Exception as error:
            # A malformed output or an isolated runner failure must not prevent
            # later instances in this dataset from being attempted.
            measured = {"status": "error", "time_s": "", "error": str(error)}
        row = {field: "" for field in CSV_FIELDS}
        row.update(measured)
        row.update({
            "dataset": dataset,
            "instance": name,
            "input": str(path.relative_to(common.ROOT)),
            "ett_max_calls": args.cc_ett_max_calls,
        })
        rows = [old for old in rows if old["instance"] != name]
        rows.append(row)
        rows.sort(key=lambda value: value["instance"])
        write_csv(output, rows)

    print(f"[{dataset}] wrote {len(rows)} rows to {output}", flush=True)
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=Path, default=common.DEFAULT_REPORT)
    parser.add_argument("--executable", type=Path, default=common.ROOT / "bin/main")
    parser.add_argument(
        "--output-dir", type=Path, default=common.ROOT / "results",
        help="directory for the four per-dataset CSV files",
    )
    parser.add_argument(
        "--merged-output", type=Path,
        default=common.ROOT / "results/cc_dynamics_all.csv",
        help="final CSV containing all per-dataset rows",
    )
    parser.add_argument("--timeout", type=int, default=600, help="seconds per case")
    parser.add_argument("--cc-ett-threshold", type=int, default=0)
    parser.add_argument(
        "--cc-ett-max-calls", type=int, default=0,
        help="global ETT-query budget; 0 preserves the unlimited default policy",
    )
    parser.add_argument(
        "--dataset", action="append", dest="datasets",
        help="run only this input-directory dataset; may be repeated",
    )
    parser.add_argument(
        "--workers", type=int, default=4,
        help="number of datasets to process concurrently (default: 4)",
    )
    parser.add_argument("--limit", type=int, help="maximum cases per dataset")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--resume", action="store_true")
    args = parser.parse_args()
    if args.workers < 1:
        parser.error("--workers must be at least 1")
    if args.timeout < 1:
        parser.error("--timeout must be at least 1")
    if args.cc_ett_threshold < 0:
        parser.error("--cc-ett-threshold must be non-negative (0 means auto)")
    if args.cc_ett_max_calls < 0:
        parser.error("--cc-ett-max-calls must be non-negative (0 means unlimited)")

    inputs = common.input_index(common.DEFAULT_INPUT_DIRS)
    selected = report_instances(args.report)
    grouped = {folder.name: [] for folder in common.DEFAULT_INPUT_DIRS}
    for item in selected:
        path = inputs.get(item["instance"])
        if path is not None:
            grouped[path.parent.name].append((item, path))

    requested = set(args.datasets or grouped)
    unknown = requested - set(grouped)
    if unknown:
        parser.error("unknown --dataset value(s): " + ", ".join(sorted(unknown)))
    grouped = {name: entries for name, entries in grouped.items() if name in requested}
    if args.limit is not None:
        grouped = {name: entries[:args.limit] for name, entries in grouped.items()}

    total = sum(len(entries) for entries in grouped.values())
    print(f"selected {total} cases across {len(grouped)} datasets")
    if args.dry_run:
        for dataset, entries in grouped.items():
            print(f"[{dataset}] {len(entries)} cases -> {dataset_output(args.output_dir, dataset)}")
            for _, path in entries:
                print(path)
        return 0
    if not args.executable.is_file():
        parser.error(f"executable not found: {args.executable}; build the project first")

    # Parallelism is across datasets only.  Each dataset retains a single,
    # ordered runner that keeps going after timeout/error rows.
    with ThreadPoolExecutor(max_workers=min(args.workers, len(grouped))) as executor:
        futures = {
            executor.submit(run_dataset, dataset, entries, args): dataset
            for dataset, entries in grouped.items()
        }
        for future in as_completed(futures):
            dataset = futures[future]
            try:
                future.result()
            except Exception as error:  # keep the other dataset runners alive
                print(f"[{dataset}] runner error: {error}", flush=True)

    # Read checkpoints as the source of truth, including datasets that may
    # have encountered an unexpected runner-level exception.
    all_rows = []
    for dataset in grouped:
        all_rows.extend(read_csv(dataset_output(args.output_dir, dataset)))
    all_rows.sort(key=lambda row: (row["dataset"], row["instance"]))
    write_csv(args.merged_output, all_rows)
    print(f"merged {len(all_rows)} rows into {args.merged_output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
