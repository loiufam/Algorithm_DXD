#!/usr/bin/env python3
"""Run detailed single-thread DXD, Hybrid, or DynDXD CC metrics."""

from __future__ import annotations

import argparse
import csv
import json
import os
import re
import signal
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

import run_cc_experiment as common


STAT_RE = re.compile(r"^CC Stats ([^:]+):\s*([\d.eE+-]+)\s*$", re.MULTILINE)

FIELDS = (
    "run_id",
    "dataset",
    "category",
    "instance",
    "input",
    "algorithm",
    "threads",
    "status",
    "time_s",
    "cc_time_s",
    "bfs_time_s",
    "ett_build_time_s",
    "ett_decrement_time_s",
    "ett_extraction_time_s",
    "ett_release_time_s",
    "DXD_V",
    "DXD_E",
    "Dyn_dec_calls",
    "Dyn_deleted_tree_edges",
    "Dyn_deleted_vertices",
    "Dyn_avg_vertices",
    "Dyn_avg_replacement_checks",
    "Dyn_dec_time_s",
    "Dyn_inc_time_s",
    "hybrid_bfs_queries",
    "hybrid_ett_decrement_updates",
    "hybrid_ett_builds",
    "hybrid_ett_component_checks",
    "hybrid_ett_releases",
    "solutions",
    "max_blocks",
    "exit_code",
    "error",
)

STAT_FIELD_MAP = {
    "Complete": "complete",
    "DXD Vertex Sum": "DXD_V",
    "DXD Edge Sum": "DXD_E",
    "DXD BFS CPU": "bfs_time_s",
    "Dyn Dec Calls": "Dyn_dec_calls",
    "Dyn Deleted Tree Edges": "Dyn_deleted_tree_edges",
    "Dyn Deleted Vertices": "Dyn_deleted_vertices",
    "Dyn Avg Vertices": "Dyn_avg_vertices",
    "Dyn Avg Replacement Checks": "Dyn_avg_replacement_checks",
    "Dyn Dec CPU": "Dyn_dec_time_s",
    "Dyn Inc CPU": "Dyn_inc_time_s",
}


def run_case(
    case: dict[str, str],
    algorithm: str,
    timeout: int,
) -> tuple[dict[str, str], str]:
    command = [
        str(common.EXECUTABLE),
        "-a",
        algorithm,
        "-i",
        str(common.ROOT / case["path"]),
        "-t",
        "1",
        "--time-limit",
        str(timeout),
        "--skip-dnnf-size",
    ]
    if algorithm in ("dxd", "ddxd"):
        command.append("--enable-cc-stats")
    if algorithm == "hybrid":
        command.append("--debug")

    started = time.perf_counter()
    process = subprocess.Popen(
        command,
        cwd=str(common.ROOT),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
        start_new_session=True,
    )
    try:
        output, _ = process.communicate(timeout=timeout + 30)
        exit_code: int | None = process.returncode
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        output, _ = process.communicate()
        exit_code = None
        output += f"\n[experiment] process timeout after {timeout + 30}s\n"

    basic = common.parse_output(algorithm, output)
    stats = dict(STAT_RE.findall(output))
    parsed = {
        field: stats.get(source, "")
        for source, field in STAT_FIELD_MAP.items()
    }
    hybrid_metrics = {
        name.strip().lower().replace(" ", "_"): value
        for name, value in common.HYBRID_METRIC_RE.findall(output)
    }
    if algorithm == "hybrid":
        parsed.update(
            {
                "hybrid_bfs_queries": hybrid_metrics.get("bfs_queries", ""),
                "hybrid_ett_decrement_updates": hybrid_metrics.get(
                    "ett_decrement_updates", ""
                ),
                "hybrid_ett_builds": hybrid_metrics.get("ett_builds", ""),
                "hybrid_ett_component_checks": hybrid_metrics.get(
                    "ett_component_checks", ""
                ),
                "hybrid_ett_releases": hybrid_metrics.get("ett_releases", ""),
            }
        )
    if exit_code is None or "搜索超时" in output or "Time bound" in output:
        status = "timeout"
    elif exit_code != 0:
        status = "error"
    elif basic["time_s"] and basic["solutions"]:
        status = "success"
    else:
        status = "incomplete"
    row = {
        "dataset": case["dataset"],
        "category": case["category"],
        "instance": case["instance"],
        "input": case["path"],
        "algorithm": algorithm,
        "threads": "1",
        "status": status,
        "exit_code": "" if exit_code is None else str(exit_code),
        "error": (
            ""
            if status == "success"
            else " | ".join(output.strip().splitlines()[-3:])[:1000]
        ),
        **basic,
        **parsed,
    }
    return row, output


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--algorithm",
        required=True,
        choices=("dxd", "hybrid", "ddxd"),
    )
    parser.add_argument(
        "--benchmark-set", choices=("old", "new", "all"), default="all"
    )
    parser.add_argument("--case-file", type=Path)
    parser.add_argument("--timeout", type=int, default=1500)
    parser.add_argument("--limit", type=int, default=0)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--resume", action="store_true")
    args = parser.parse_args()
    if not common.EXECUTABLE.is_file():
        parser.error(f"solver executable not found: {common.EXECUTABLE}")
    try:
        cases = common.select_cases(
            args.benchmark_set, args.case_file, args.limit
        )
    except (OSError, ValueError) as error:
        parser.error(str(error))
    print(f"cases={len(cases)} algorithm={args.algorithm} threads=1")
    if args.dry_run:
        for case in cases:
            print(
                f"{case['dataset']}/{case['category']}/"
                f"{case['instance']} <- {case['path']}"
            )
        return 0

    output_dir = args.output_dir.resolve() if args.output_dir else common.RESULTS
    output_dir.mkdir(parents=True, exist_ok=True)
    existing_rows: list[dict[str, str]] = []
    if args.resume:
        candidates = sorted(
            output_dir.glob(f"cc_metrics_{args.algorithm}_*.csv")
        )
        if not candidates:
            parser.error(
                f"no cc_metrics_{args.algorithm} CSV found in {output_dir}"
            )
        result_path = candidates[-1]
        with result_path.open(encoding="utf-8", newline="") as stream:
            existing_rows = list(csv.DictReader(stream))
        run_id = (
            existing_rows[0]["run_id"]
            if existing_rows
            else datetime.now().strftime("%Y%m%d_%H%M%S")
        )
    else:
        run_id = datetime.now().strftime("%Y%m%d_%H%M%S")
        result_path = output_dir / (
            f"cc_metrics_{args.algorithm}_{run_id}.csv"
        )
    all_rows = list(existing_rows)
    completed = {
        row["instance"]
        for row in existing_rows
        if row["status"] == "success"
    }
    status_counts: dict[str, int] = {}
    for index, case in enumerate(cases, 1):
        print(
            f"[{index}/{len(cases)}] {case['instance']} "
            f"({args.algorithm})",
            flush=True,
        )
        if case["instance"] not in completed:
            row, _ = run_case(case, args.algorithm, args.timeout)
            row["run_id"] = run_id
            all_rows.append(row)
        with result_path.open("w", encoding="utf-8", newline="") as stream:
            writer = csv.DictWriter(
                stream, fieldnames=FIELDS, extrasaction="ignore"
            )
            writer.writeheader()
            writer.writerows(all_rows)
    for row in all_rows:
        status_counts[row["status"]] = status_counts.get(row["status"], 0) + 1
    summary_path = output_dir / (
        f"cc_metrics_{args.algorithm}_{run_id}_summary.json"
    )
    summary_path.write_text(
        json.dumps(
            {
                "run_id": run_id,
                "algorithm": args.algorithm,
                "threads": 1,
                "cases": len(cases),
                "status_counts": status_counts,
                "timeout_s": args.timeout,
            },
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )
    print(f"results: {result_path}")
    print(f"summary: {summary_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
