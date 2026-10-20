#!/usr/bin/env python3
"""Run the final single-thread DXD and DynDXD CC metrics experiment."""

import argparse
import csv
import re
import signal
import subprocess
from concurrent.futures import ThreadPoolExecutor, as_completed
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
RESULT_FIELDS = (
    "dataset", "instance", "input", "DXD_status", "Dyn_status",
    "DXD_V", "DXD_E", "DXD_BFS_time_s",
    "Dyn_dec_calls", "Dyn_deleted_tree_edges", "Dyn_deleted_vertices",
    "Dyn_avg_vertices", "Dyn_avg_replacement_checks",
    "Dyn_dec_time_s", "Dyn_inc_time_s", "error",
)
METRIC_DEFINITIONS = (
    ("DXD_V", "单线程 DXD 全过程所有 BFS 连通分量查询累计遍历的图顶点数", "个"),
    ("DXD_E", "单线程 DXD 全过程所有 BFS 连通分量查询累计遍历的图边数", "条"),
    ("DXD_BFS_time_s", "单线程 DXD 全过程 BFS 连通分量查询累计 CPU 时间", "s"),
    ("Dyn_dec_calls", "单线程 DynDXD 的 DecUpdateCC 调用次数", "次"),
    ("Dyn_deleted_tree_edges", "减量更新期间累计删除的 ETT 树边数", "条"),
    ("Dyn_deleted_vertices", "减量更新期间累计删除的活跃图顶点数", "个"),
    ("Dyn_avg_vertices", "每次减量更新前活跃图顶点数的平均值", "个/次"),
    ("Dyn_avg_replacement_checks", "每次减量更新实际扫描的候选替代边数平均值", "条/次"),
    ("Dyn_dec_time_s", "单线程 DynDXD 全过程 DecUpdateCC 累计 CPU 时间", "s"),
    ("Dyn_inc_time_s", "单线程 DynDXD 全过程 IncUpdateCC 累计 CPU 时间", "s"),
)


def parse_stats(output, algorithm):
    """Parse the last complete CC Stats block for one algorithm."""
    starts = [
        match.start()
        for match in re.finditer(r"^CC Stats Complete:", output, re.MULTILINE)
    ]
    expected = DXD_STATS if algorithm == "dxd" else DYN_STATS
    for start in reversed(starts):
        block = output[start:]
        values = {
            name: value for name, value in STAT_RE.findall(block)
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
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=timeout + 30,
        )
    except subprocess.TimeoutExpired:
        return {"status": "time_out", "error": "process timeout"}
    except OSError as error:
        return {"status": "error", "error": str(error)}

    measured = parse_stats(process.stdout, algorithm)
    if measured is not None:
        if process.returncode != 0 and measured["status"] == "success":
            measured.update(
                status="error",
                error=f"solver exited with {process.returncode}",
            )
        return measured
    if process.returncode < 0:
        signal_number = -process.returncode
        try:
            signal_name = signal.Signals(signal_number).name
        except ValueError:
            signal_name = f"signal {signal_number}"
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


def write_csv(path, rows, fields=RESULT_FIELDS):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def write_metric_dictionary(output_dir):
    path = output_dir / "cc_metrics_zh.csv"
    write_csv(
        path,
        (
            {"field": field, "meaning_zh": meaning, "unit": unit}
            for field, meaning, unit in METRIC_DEFINITIONS
        ),
        ("field", "meaning_zh", "unit"),
    )
    return path


def read_csv(path):
    if not path.is_file():
        return []
    with path.open(encoding="utf-8", newline="") as stream:
        return list(csv.DictReader(stream))


def dataset_output(output_dir, dataset):
    return output_dir / f"cc_metrics_{dataset}.csv"


def run_dataset(dataset, entries, args):
    output = dataset_output(args.output_dir, dataset)
    rows = read_csv(output) if args.resume else []
    completed = {
        row["instance"] for row in rows
        if row.get("DXD_status") == row.get("Dyn_status") == "success"
    }

    for number, (_, path) in enumerate(entries, 1):
        name = path.stem
        if name in completed:
            print(
                f"[{dataset} {number}/{len(entries)}] {name} "
                "(already recorded)",
                flush=True,
            )
            continue

        print(f"[{dataset} {number}/{len(entries)}] {name}", flush=True)
        dxd = run_case(
            args.executable, path, "dxd", args.timeout,
            args.cc_ett_max_calls,
        )
        dyn = run_case(
            args.executable, path, "ddxd", args.timeout,
            args.cc_ett_max_calls,
        )
        row = {
            "dataset": dataset,
            "instance": name,
            "input": str(path.relative_to(common.ROOT)),
            "DXD_status": dxd["status"],
            "Dyn_status": dyn["status"],
            **{field: dxd.get(field, "") for field in DXD_STATS.values()},
            **{field: dyn.get(field, "") for field in DYN_STATS.values()},
            "error": "; ".join(
                filter(None, (dxd.get("error"), dyn.get("error")))),
        }
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
        "--output-dir", type=Path,
        default=common.ROOT / "results/cc_metrics",
    )
    parser.add_argument(
        "--merged-output", type=Path,
        default=common.ROOT / "results/cc_metrics_all.csv",
    )
    parser.add_argument("--timeout", type=int, default=1200)
    parser.add_argument("--cc-ett-max-calls", type=int, default=3)
    parser.add_argument(
        "--dataset", action="append", dest="datasets",
        help="run only this input-directory dataset; may be repeated",
    )
    parser.add_argument(
        "--case-file", type=Path,
        help="run only names in this file (one per line or comma separated)",
    )
    parser.add_argument("--workers", type=int, default=4)
    parser.add_argument("--limit", type=int, help="maximum cases per dataset")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--resume", action="store_true")
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
            selected = common.instances_from_case_file(
                args.case_file, args.report)
        except (OSError, ValueError) as error:
            parser.error(str(error))
    else:
        selected = common.eligible_instances(args.report)

    grouped = {folder.name: [] for folder in common.DEFAULT_INPUT_DIRS}
    for item in selected:
        path = inputs.get(item["instance"])
        if path is not None:
            grouped[path.parent.name].append((item, path))

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
    print(f"selected {total} cases across {len(grouped)} datasets")
    if args.dry_run:
        for dataset, entries in grouped.items():
            print(f"[{dataset}] {len(entries)} cases")
            for _, path in entries:
                print(path)
        return 0
    if not args.executable.is_file():
        parser.error(f"executable not found: {args.executable}")
    if not grouped:
        write_csv(args.merged_output, [])
        print(f"wrote 0 rows to {args.merged_output}")
        return 0

    dictionary_path = write_metric_dictionary(args.output_dir)
    print(f"metric definitions written to {dictionary_path}", flush=True)
    with ThreadPoolExecutor(max_workers=min(args.workers, len(grouped))) as executor:
        futures = {
            executor.submit(run_dataset, dataset, entries, args): dataset
            for dataset, entries in grouped.items()
        }
        for future in as_completed(futures):
            dataset = futures[future]
            try:
                future.result()
            except Exception as error:
                print(f"[{dataset}] runner error: {error}", flush=True)

    all_rows = []
    for dataset in grouped:
        all_rows.extend(read_csv(dataset_output(args.output_dir, dataset)))
    all_rows.sort(key=lambda row: (row["dataset"], row["instance"]))
    write_csv(args.merged_output, all_rows)
    print(f"merged {len(all_rows)} rows into {args.merged_output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
