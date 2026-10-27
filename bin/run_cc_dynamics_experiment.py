#!/usr/bin/env python3
"""Run one single-thread algorithm and aggregate one timestamped CSV."""

import argparse
import csv
import re
import signal
import subprocess
import tempfile
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime
from pathlib import Path

import run_cc_experiment as common


STAT_RE = re.compile(r"^CC Stats ([^:]+):\s*([\d.]+)$", re.MULTILINE)

DXD_STATS = {
    "DXD Vertex Sum": "DXD_V",
    "DXD Edge Sum": "DXD_E",
    "DXD BFS CPU": "DXD_BFS_time_s",
}
DYN_STATS = {
    "Dyn Dec Calls": "Dyn_dec_calls",
    "Dyn Deleted Tree Edges": "Dyn_deleted_tree_edges",
    "Dyn Deleted Vertices": "Dyn_deleted_vertices",
    "Dyn Avg Vertices": "Dyn_avg_vertices",
    "Dyn Avg Replacement Checks": "Dyn_avg_replacement_checks",
    "Dyn Dec CPU": "Dyn_dec_time_s",
    "Dyn Inc CPU": "Dyn_inc_time_s",
}
METRIC_FIELDS = (
    "DXD_V", "DXD_E", "DXD_BFS_time_s",
    "Dyn_dec_calls", "Dyn_deleted_tree_edges", "Dyn_deleted_vertices",
    "Dyn_avg_vertices", "Dyn_avg_replacement_checks",
    "Dyn_dec_time_s", "Dyn_inc_time_s",
)
INTERNAL_FIELDS = (
    "record_type", "algorithm", "dataset", "instance", "input", "status",
    *METRIC_FIELDS,
    "success_count", "failure_count", "success_rate", "error",
)
FIELD_HEADERS_ZH = {
    "record_type": "记录类型（instance/dataset_summary/overall_summary）",
    "algorithm": "算法（dxd/ddxd）",
    "dataset": "测试集子目录",
    "instance": "测例名称",
    "input": "测例路径",
    "status": "运行状态",
    "DXD_V": "DXD累计遍历图顶点数",
    "DXD_E": "DXD累计遍历图边数",
    "DXD_BFS_time_s": "DXD BFS累计CPU时间(s)",
    "Dyn_dec_calls": "DynDXD减量更新次数",
    "Dyn_deleted_tree_edges": "DynDXD累计删除树边数",
    "Dyn_deleted_vertices": "DynDXD累计删除顶点数",
    "Dyn_avg_vertices": "DynDXD平均图顶点数",
    "Dyn_avg_replacement_checks": "DynDXD平均实际查找替代边数",
    "Dyn_dec_time_s": "DynDXD减量更新累计CPU时间(s)",
    "Dyn_inc_time_s": "DynDXD增量更新累计CPU时间(s)",
    "success_count": "成功测例数",
    "failure_count": "失败测例数",
    "success_rate": "运行成功率（成功数/总数）",
    "error": "错误信息",
}


def parse_stats(output, algorithm):
    """Parse the last complete CC Stats block for one single-thread run."""
    starts = [
        match.start()
        for match in re.finditer(r"^CC Stats Complete:", output, re.MULTILINE)
    ]
    expected = DXD_STATS if algorithm == "dxd" else DYN_STATS
    for start in reversed(starts):
        values = {
            name: value
            for name, value in STAT_RE.findall(output[start:])
        }
        if not all(name in values for name in expected):
            continue

        result = {}
        for log_name, field in expected.items():
            value = float(values[log_name])
            if field.endswith("_time_s") or field in {
                "Dyn_avg_vertices", "Dyn_avg_replacement_checks",
            }:
                result[field] = value
            else:
                result[field] = int(value)
        result["status"] = (
            "success" if int(float(values["Complete"])) else "time_out")
        return result
    return None


def run_case(executable, input_path, algorithm, timeout, ett_max_calls):
    command = [
        str(executable), "-a", algorithm, "-i", str(input_path), "-t", "1",
        "--enable-cc-stats",
        "--cc-ett-max-calls", str(ett_max_calls),
        "--time-limit", str(timeout),
    ]
    try:
        process = subprocess.run(
            command,
            capture_output=True,
            text=True,
            timeout=timeout + 30,
        )
    except subprocess.TimeoutExpired:
        return {"status": "time_out", "error": "process timeout"}
    except OSError as error:
        return {"status": "error", "error": str(error)}

    measured = parse_stats(process.stdout + process.stderr, algorithm)
    if measured is not None:
        if process.returncode != 0 and measured["status"] == "success":
            measured.update(
                status="error",
                error=f"solver exited with {process.returncode}",
            )
        return measured
    if process.returncode < 0:
        try:
            signal_name = signal.Signals(-process.returncode).name
        except ValueError:
            signal_name = f"signal {-process.returncode}"
        return {
            "status": "interrupted",
            "error": f"solver interrupted by {signal_name} before CC Stats",
        }
    if process.returncode != 0:
        return {
            "status": "error",
            "error": f"solver exited with {process.returncode}; no CC Stats",
        }
    return {"status": "missing_stats", "error": "no CC Stats block"}


def write_csv(path, rows):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(
            stream, fieldnames=INTERNAL_FIELDS, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def read_csv(path):
    with path.open(encoding="utf-8", newline="") as stream:
        return list(csv.DictReader(stream))


def run_dataset(dataset, entries, args, temp_dir):
    """Run one dataset in one worker and write only a temporary CSV."""
    temp_path = temp_dir / f"cc_metrics_{args.algorithm}_{dataset}.csv"
    rows = []
    for number, (_, path) in enumerate(entries, 1):
        name = path.stem
        print(
            f"[{dataset} {number}/{len(entries)}] {name} ({args.algorithm})",
            flush=True,
        )
        measured = run_case(
            args.executable, path, args.algorithm,
            args.timeout, args.cc_ett_max_calls,
        )
        row = {
            "record_type": "instance",
            "algorithm": args.algorithm,
            "dataset": dataset,
            "instance": name,
            "input": str(path.relative_to(common.ROOT)),
            "status": measured["status"],
            "error": measured.get("error", ""),
            "success_count": "",
            "failure_count": "",
            "success_rate": "",
        }
        for field in METRIC_FIELDS:
            row[field] = measured.get(field, "")
        rows.append(row)
    write_csv(temp_path, rows)
    print(f"[{dataset}] temporary CSV: {temp_path}", flush=True)
    return temp_path


def summarize_group(algorithm, dataset, rows):
    success = sum(row["status"] == "success" for row in rows)
    failure = len(rows) - success
    return {
        "record_type": "dataset_summary",
        "algorithm": algorithm,
        "dataset": dataset,
        "instance": "",
        "input": "",
        "status": "summary",
        **{field: "" for field in METRIC_FIELDS},
        "success_count": success,
        "failure_count": failure,
        "success_rate": success / len(rows) if rows else 0.0,
        "error": "",
    }


def output_path(output_dir, algorithm):
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    path = output_dir / f"cc_metrics_{algorithm}_{timestamp}.csv"
    suffix = 1
    while path.exists():
        path = output_dir / (
            f"cc_metrics_{algorithm}_{timestamp}_{suffix}.csv")
        suffix += 1
    return path


def write_final_csv(path, rows):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(
            stream,
            fieldnames=[FIELD_HEADERS_ZH[field] for field in INTERNAL_FIELDS],
            extrasaction="ignore",
        )
        writer.writeheader()
        for row in rows:
            writer.writerow({
                FIELD_HEADERS_ZH[field]: row.get(field, "")
                for field in INTERNAL_FIELDS
            })


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, default=common.ROOT / "bin/main")
    parser.add_argument(
        "--algorithm", choices=("dxd", "ddxd"), required=True,
        help="algorithm to run; each solver process is forced to one thread",
    )
    parser.add_argument(
        "--output-dir", type=Path,
        default=common.ROOT / "results",
        help="directory for the single timestamped result CSV",
    )
    parser.add_argument("--timeout", type=int, default=1200)
    parser.add_argument("--cc-ett-max-calls", type=int, default=3)
    parser.add_argument(
        "--dataset", action="append", dest="datasets",
        help="run only this dataset directory; may be repeated",
    )
    parser.add_argument(
        "--case-file", type=Path,
        help="run only names in this file (one per line or comma separated)",
    )
    parser.add_argument(
        "--workers", type=int, default=4,
        help="number of dataset workers running in parallel (default: 4)",
    )
    parser.add_argument("--limit", type=int, help="maximum cases per dataset")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    if args.workers < 1:
        parser.error("--workers must be at least 1")
    if args.timeout < 1:
        parser.error("--timeout must be at least 1")
    if args.cc_ett_max_calls < 0:
        parser.error("--cc-ett-max-calls must be non-negative")

    inputs = common.input_index(common.DEFAULT_INPUT_DIRS)
    if args.case_file:
        try:
            names = common.read_case_names(args.case_file)
        except (OSError, ValueError) as error:
            parser.error(str(error))
    else:
        names = sorted(inputs)

    missing = [name for name in names if name not in inputs]
    if missing:
        print(
            f"warning: {len(missing)} selected instances have no input file",
            flush=True,
        )
    grouped = {folder.name: [] for folder in common.DEFAULT_INPUT_DIRS}
    for name in names:
        path = inputs.get(name)
        if path is not None:
            grouped[path.parent.name].append((name, path))

    requested = set(args.datasets or grouped)
    unknown = requested - set(grouped)
    if unknown:
        parser.error("unknown --dataset value(s): " + ", ".join(sorted(unknown)))
    grouped = {
        name: entries for name, entries in grouped.items()
        if name in requested and entries
    }
    if args.limit is not None:
        grouped = {
            name: entries[:args.limit] for name, entries in grouped.items()
        }

    total = sum(len(entries) for entries in grouped.values())
    print(
        f"selected {total} cases across {len(grouped)} datasets; "
        f"algorithm={args.algorithm}; solvers=1 thread; workers={args.workers}"
    )
    if args.dry_run:
        for dataset, entries in grouped.items():
            print(f"[{dataset}] {len(entries)} cases")
            for _, path in entries:
                print(path)
        return 0
    if not args.executable.is_file():
        parser.error(f"executable not found: {args.executable}")

    result_path = output_path(args.output_dir, args.algorithm)
    if not grouped:
        write_final_csv(result_path, [{
            "record_type": "overall_summary",
            "algorithm": args.algorithm,
            "dataset": "ALL",
            "instance": "",
            "input": "",
            "status": "summary",
            **{field: "" for field in METRIC_FIELDS},
            "success_count": 0,
            "failure_count": 0,
            "success_rate": 0.0,
            "error": "",
        }])
        print(f"wrote 0 cases to {result_path}")
        return 0

    final_rows = []
    temp_files = []
    worker_errors = {}
    with tempfile.TemporaryDirectory(prefix="cc_metrics_") as temp_name:
        temp_dir = Path(temp_name)
        with ThreadPoolExecutor(max_workers=min(args.workers, len(grouped))) as executor:
            futures = {
                executor.submit(
                    run_dataset, dataset, entries, args, temp_dir
                ): dataset
                for dataset, entries in grouped.items()
            }
            for future in as_completed(futures):
                dataset = futures[future]
                try:
                    temp_files.append((dataset, future.result()))
                except Exception as error:
                    worker_errors[dataset] = str(error)
                    print(f"[{dataset}] worker error: {error}", flush=True)

        all_instance_rows = []
        summaries = []
        for dataset, temp_path in sorted(temp_files):
            rows = read_csv(temp_path)
            all_instance_rows.extend(rows)
            summaries.append(summarize_group(args.algorithm, dataset, rows))
        for dataset, error in sorted(worker_errors.items()):
            summaries.append({
                "record_type": "dataset_summary",
                "algorithm": args.algorithm,
                "dataset": dataset,
                "instance": "",
                "input": "",
                "status": "error",
                **{field: "" for field in METRIC_FIELDS},
                "success_count": 0,
                "failure_count": len(grouped[dataset]),
                "success_rate": 0.0,
                "error": error,
            })

        all_instance_rows.sort(
            key=lambda row: (row["dataset"], row["instance"]))
        final_rows = all_instance_rows + summaries
        success = sum(int(row["success_count"]) for row in summaries)
        count = sum(
            int(row["success_count"]) + int(row["failure_count"])
            for row in summaries
        )
        final_rows.append({
            "record_type": "overall_summary",
            "algorithm": args.algorithm,
            "dataset": "ALL",
            "instance": "",
            "input": "",
            "status": "summary",
            **{field: "" for field in METRIC_FIELDS},
            "success_count": success,
            "failure_count": count - success,
            "success_rate": success / count if count else 0.0,
            "error": "",
        })

    write_final_csv(result_path, final_rows)
    print(f"wrote {len(final_rows)} rows to {result_path}")
    print(f"temporary CSVs removed: {len(temp_files)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
