#!/usr/bin/env python3
"""Run the single-stage HybridDXD runtime-policy experiment.

The experiment compares Hybrid, DXD, and optionally DXZ on the same cases.
Hybrid uses runtime V/E/ET/EN cost decisions; there is no stage-1 probability
calibration and no stop-ratio sweep.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import os
import re
import signal
import statistics
import subprocess
import time
from datetime import datetime
from pathlib import Path

import run_cc_experiment as common


TIME_RE = common.TIME_RE
SOL_RE = common.SOL_RE
BLOCKS_RE = common.BLOCKS_RE
METRIC_RE = re.compile(
    r"^(?:Hybrid|DXD) ([^:]+):\s*([\d.eE+-]+)(?:\s*s)?\s*$",
    re.MULTILINE,
)
DXD_CPU_RE = common.DXD_CPU_RE
DXD_BFS_TIME_RE = re.compile(
    r"^(?:CC Stats )?DXD BFS Time:\s*([\d.eE+-]+)\s*s?",
    re.MULTILINE,
)
STAT_RE = re.compile(
    r"^CC Stats ([^:]+):\s*([\d.eE+-]+)\s*$", re.MULTILINE
)
DNNF_RE = re.compile(r"^DNNF Size:\s*(\d+)", re.MULTILINE)
ZDD_RE = re.compile(r"^ZDD Size:\s*(\d+)", re.MULTILINE)

SUPPORTED_INPUT_SUFFIXES = {".txt", ".ec"}
ALGORITHM_LABELS = {
    "hybrid": "Hybrid",
    "dxd": "DXD-no-threshold",
    "dxd-threshold": "DXD-threshold",
    "dxz": "DXZ",
}

FIELDS = (
    "run_id",
    "dataset",
    "category",
    "instance",
    "input",
    "algorithm",
    "threads",
    "threshold_ratio",
    "status",
    "time_s",
    "wall_time_s",
    "solutions",
    "max_blocks",
    "model_size",
    "runtime_policy",
    "component_query_calls",
    "component_query_time_s",
    "initial_bfs_components",
    "initial_bfs_multi_splits",
    "initial_dynamic_rows",
    "initial_dynamic_cols",
    "initial_dynamic_entries",
    "initial_dynamic_tree_edges",
    "initial_dynamic_non_tree_edges",
    "cost_decisions",
    "cost_dyn_selections",
    "cost_bfs_selections",
    "cost_threshold_stops",
    "cost_v_after_sum",
    "cost_e_after_sum",
    "cost_vd_sum",
    "cost_ed_sum",
    "cost_etd_sum",
    "cost_end_sum",
    "cost_dyn_sum",
    "cost_bfs_sum",
    "dyn_query_calls",
    "dyn_decrement_calls",
    "dyn_increment_calls",
    "bfs_calls",
    "bfs_time_s",
    "dyn_time_s",
    "v_before_sum",
    "e_before_sum",
    "et_before_sum",
    "en_before_sum",
    "vd_sum",
    "ed_sum",
    "etd_sum",
    "end_sum",
    "er_sum",
    "tree_edge_cuts",
    "tree_edge_links",
    "dxd_multi_splits",
    "dxd_threshold_stops",
    "exit_code",
    "error",
)


def first(pattern: re.Pattern[str], text: str) -> str:
    match = pattern.search(text)
    return match.group(1) if match else ""


def select_cases_from_directory(directory: Path) -> list[dict[str, str]]:
    directory = directory.resolve()
    if not directory.is_dir():
        raise ValueError(f"input directory does not exist: {directory}")

    manifest_by_path = {
        (common.ROOT / row["path"]).resolve(): row
        for row in common.read_manifest()
    }
    files = sorted(
        path.resolve()
        for path in directory.rglob("*")
        if path.is_file()
        and path.suffix.lower() in SUPPORTED_INPUT_SUFFIXES
        and not any(part.startswith(".") for part in path.parts)
    )
    if not files:
        suffixes = ", ".join(sorted(SUPPORTED_INPUT_SUFFIXES))
        raise ValueError(
            f"no benchmark files ({suffixes}) found under {directory}"
        )

    cases: list[dict[str, str]] = []
    for path in files:
        manifest_row = manifest_by_path.get(path)
        if manifest_row is not None:
            cases.append(dict(manifest_row))
            continue

        try:
            stored_path = str(path.relative_to(common.ROOT))
        except ValueError:
            stored_path = str(path)
        cases.append(
            {
                "dataset": directory.name,
                "category": path.parent.name or directory.name,
                "instance": path.stem,
                "path": stored_path,
            }
        )

    duplicate_names = sorted(
        name
        for name in {case["instance"] for case in cases}
        if sum(case["instance"] == name for case in cases) > 1
    )
    if duplicate_names:
        raise ValueError(
            "input directory contains duplicate instance stems: "
            + ", ".join(duplicate_names[:20])
        )
    return cases


def parse_output(output: str) -> dict[str, str]:
    stats = dict(STAT_RE.findall(output))
    metrics = {
        key.strip().lower().replace(" ", "_"): value
        for key, value in METRIC_RE.findall(output)
    }
    return {
        "time_s": first(TIME_RE, output),
        "solutions": first(SOL_RE, output),
        "max_blocks": first(BLOCKS_RE, output),
        "model_size": first(DNNF_RE, output) or first(ZDD_RE, output),
        "runtime_policy": metrics.get("runtime_policy", ""),
        "component_query_calls": metrics.get(
            "component_query_calls",
            metrics.get("bfs_calls", ""),
        ),
        "component_query_time_s": metrics.get(
            "component_query_time",
            stats.get(
                "DXD BFS Time",
                first(DXD_BFS_TIME_RE, output)
                or first(DXD_CPU_RE, output),
            ),
        ),
        "initial_bfs_components": metrics.get(
            "initial_bfs_components", ""
        ),
        "initial_bfs_multi_splits": metrics.get(
            "initial_bfs_multi_splits", ""
        ),
        "initial_dynamic_rows": metrics.get(
            "initial_dynamic_rows", ""
        ),
        "initial_dynamic_cols": metrics.get(
            "initial_dynamic_cols", ""
        ),
        "initial_dynamic_entries": metrics.get(
            "initial_dynamic_entries", ""
        ),
        "initial_dynamic_tree_edges": metrics.get(
            "initial_dynamic_tree_edges", ""
        ),
        "initial_dynamic_non_tree_edges": metrics.get(
            "initial_dynamic_non_tree_edges", ""
        ),
        "cost_decisions": metrics.get("cost_decisions", ""),
        "cost_dyn_selections": metrics.get(
            "cost_dyn_selections", ""
        ),
        "cost_bfs_selections": metrics.get(
            "cost_bfs_selections", ""
        ),
        "cost_threshold_stops": metrics.get(
            "cost_threshold_stops", ""
        ),
        "cost_v_after_sum": metrics.get("cost_v_after_sum", ""),
        "cost_e_after_sum": metrics.get("cost_e_after_sum", ""),
        "cost_vd_sum": metrics.get("cost_vd_sum", ""),
        "cost_ed_sum": metrics.get("cost_ed_sum", ""),
        "cost_etd_sum": metrics.get("cost_etd_sum", ""),
        "cost_end_sum": metrics.get("cost_end_sum", ""),
        "cost_dyn_sum": metrics.get("cost_dyn_sum", ""),
        "cost_bfs_sum": metrics.get("cost_bfs_sum", ""),
        "dyn_query_calls": metrics.get(
            "dyn_query_calls",
            metrics.get("ett_calls", ""),
        ),
        "dyn_decrement_calls": metrics.get(
            "dyn_decrement_calls",
            stats.get("Dyn Dec Calls", ""),
        ),
        "dyn_increment_calls": metrics.get(
            "dyn_increment_calls", ""
        ),
        "bfs_calls": metrics.get("bfs_calls", ""),
        "bfs_time_s": metrics.get(
            "bfs_time",
            stats.get(
                "DXD BFS Time",
                first(DXD_BFS_TIME_RE, output)
                or first(DXD_CPU_RE, output),
            ),
        ),
        "dyn_time_s": metrics.get(
            "dyn_time",
            "",
        ),
        "v_before_sum": metrics.get(
            "v_before_sum",
            stats.get("Dyn Active Vertex Sum", ""),
        ),
        "e_before_sum": metrics.get(
            "e_before_sum",
            stats.get("Dyn Active Edge Sum", ""),
        ),
        "et_before_sum": metrics.get(
            "et_before_sum",
            stats.get("Dyn Active Tree Edge Sum", ""),
        ),
        "en_before_sum": metrics.get(
            "en_before_sum",
            stats.get("Dyn Active Non-Tree Edge Sum", ""),
        ),
        "vd_sum": metrics.get(
            "vd_sum",
            stats.get("Dyn Vd Sum", ""),
        ),
        "ed_sum": metrics.get(
            "ed_sum",
            stats.get("Dyn Ed Sum", ""),
        ),
        "etd_sum": metrics.get(
            "etd_sum",
            stats.get("Dyn ETd Sum", ""),
        ),
        "end_sum": metrics.get(
            "end_sum",
            stats.get("Dyn ENd Sum", ""),
        ),
        "er_sum": metrics.get(
            "er_sum",
            stats.get("Dyn Er Sum", ""),
        ),
        "tree_edge_cuts": metrics.get(
            "tree_edge_cuts",
            stats.get("Dyn Tree Edge Cuts", ""),
        ),
        "tree_edge_links": metrics.get(
            "tree_edge_links",
            stats.get("Dyn Tree Edge Links", ""),
        ),
        "dxd_multi_splits": metrics.get("multi_splits", ""),
        "dxd_threshold_stops": metrics.get("threshold_stops", ""),
    }


def run_process(
    command: list[str], timeout: int
) -> tuple[str, int | None, float]:
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
        return output, process.returncode, time.perf_counter() - started
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        output, _ = process.communicate()
        output += (
            f"\n[experiment] process timeout after {timeout + 30}s\n"
        )
        return output, None, time.perf_counter() - started


def run_case(
    case: dict[str, str],
    algorithm: str,
    threads: int,
    timeout: int,
    threshold_ratio: float,
    decision_log_dir: Path | None,
) -> tuple[dict[str, str], str]:
    base_algorithm = "dxd" if algorithm.startswith("dxd") else algorithm
    command = [
        str(common.EXECUTABLE),
        "-a",
        base_algorithm,
        "-i",
        str(common.ROOT / case["path"]),
        "-t",
        str(threads),
        "--time-limit",
        str(timeout),
        "--skip-dnnf-size",
    ]
    if algorithm == "hybrid":
        command.extend(
            [
                "--hybrid-runtime-policy",
                "--threshold-ratio",
                f"{threshold_ratio:g}",
                "--enable-cc-stats",
            ]
        )
        if decision_log_dir is not None:
            command.extend(
                [
                    "--hybrid-decision-log",
                    str(
                        decision_log_dir
                        / (
                            f"{case['instance']}_hybrid_"
                            f"r{threshold_ratio:g}_t{threads}.log"
                        )
                    ),
                ]
            )
    elif base_algorithm == "dxd":
        command.append("--enable-cc-stats")
        if algorithm == "dxd-threshold":
            command.extend(
                ["--dxd-threshold", "--threshold-ratio",
                 f"{threshold_ratio:g}"]
            )

    output, exit_code, wall_time = run_process(command, timeout)
    parsed = parse_output(output)
    if exit_code is None or "搜索超时" in output or "Time bound" in output:
        status = "timeout"
    elif exit_code != 0:
        status = "error"
    elif parsed["time_s"] and parsed["solutions"]:
        status = (
            "success"
            if algorithm != "hybrid"
            or parsed["runtime_policy"] == "1"
            else "incomplete"
        )
    else:
        status = "incomplete"

    row = {
        "dataset": case["dataset"],
        "category": case["category"],
        "instance": case["instance"],
        "input": case["path"],
        "algorithm": algorithm,
        "threads": str(threads),
        "threshold_ratio": f"{threshold_ratio:g}",
        "status": status,
        "wall_time_s": f"{wall_time:.6f}",
        "exit_code": "" if exit_code is None else str(exit_code),
        "error": (
            ""
            if status == "success"
            else " | ".join(output.strip().splitlines()[-3:])[:1000]
        ),
        **parsed,
    }
    return row, output


def write_csv(path: Path, rows: list[dict[str, str]]) -> None:
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(
            stream, fieldnames=FIELDS, extrasaction="ignore"
        )
        writer.writeheader()
        writer.writerows(rows)


def ensure_solution_consistency(rows: list[dict[str, str]]) -> None:
    by_case: dict[str, list[dict[str, str]]] = {}
    for row in rows:
        by_case.setdefault(row["instance"], []).append(row)
    for case_rows in by_case.values():
        successful = [row for row in case_rows if row["solutions"]]
        if len(successful) < 2:
            continue
        expected = successful[0]["solutions"]
        if any(row["solutions"] != expected for row in successful):
            for row in successful:
                row["status"] = "solution_mismatch"


def comparison_summary(rows: list[dict[str, str]]) -> dict[str, object]:
    by_variant: dict[str, object] = {}
    variant_keys = sorted({
        (row["algorithm"], row["threshold_ratio"])
        for row in rows
    })
    for algorithm, ratio in variant_keys:
        selected = [
            row for row in rows
            if row["algorithm"] == algorithm
            and row["threshold_ratio"] == ratio
            and row["status"] == "success"
        ]
        times = [float(row["time_s"]) for row in selected]
        by_variant[f"{algorithm}_r{ratio}"] = {
            "successful_cases": len(times),
            "mean_time_s": statistics.mean(times) if times else None,
            "median_time_s": statistics.median(times) if times else None,
            "component_query_calls": sum(
                int(float(row["component_query_calls"]))
                for row in selected
                if row["component_query_calls"]
            ),
            "dyn_query_calls": sum(
                int(float(row["dyn_query_calls"]))
                for row in selected
                if row["dyn_query_calls"]
            ),
            "dyn_decrement_calls": sum(
                int(float(row["dyn_decrement_calls"]))
                for row in selected
                if row["dyn_decrement_calls"]
            ),
            "bfs_calls": sum(
                int(float(row["bfs_calls"]))
                for row in selected
                if row["bfs_calls"]
            ),
        }

    matched: dict[str, object] = {}
    hybrid_rows = [
        row
        for row in rows
        if row["algorithm"] == "hybrid" and row["status"] == "success"
    ]
    for baseline in ("dxd", "dxd-threshold", "dxz"):
        speedups: list[float] = []
        for row in hybrid_rows:
            baseline_row = next(
                (
                    candidate
                    for candidate in rows
                    if candidate["algorithm"] == baseline
                    and candidate["instance"] == row["instance"]
                    and candidate["threshold_ratio"] == row["threshold_ratio"]
                    and candidate["status"] == "success"
                ),
                None,
            )
            if baseline_row is None:
                continue
            hybrid_time = float(row["time_s"])
            baseline_time = float(baseline_row["time_s"])
            if hybrid_time > 0:
                speedups.append(baseline_time / hybrid_time)
        matched[f"hybrid_vs_{baseline}"] = {
            "matched_cases": len(speedups),
            "median_speedup": (
                statistics.median(speedups) if speedups else None
            ),
            "geometric_mean_speedup": (
                math.exp(
                    statistics.mean(math.log(value) for value in speedups)
                )
                if speedups
                else None
            ),
        }
    return {
        "variants": by_variant,
        "matched_speedups": matched,
    }


def parse_case_count(value: str) -> int | None:
    """Parse --count: 'all' (or 0) keeps every selected case."""
    normalized = value.strip().lower()
    if normalized in {"all", "--all", "0"}:
        return None
    try:
        count = int(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError(
            "--count must be 'all' or a positive integer"
        ) from error
    if count < 1:
        raise argparse.ArgumentTypeError(
            "--count must be 'all' or a positive integer"
        )
    return count


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--benchmark-set", choices=("old", "new", "all"), default="all"
    )
    case_group = parser.add_mutually_exclusive_group()
    case_group.add_argument("--case-file", type=Path)
    case_group.add_argument("--input-dir", type=Path)
    parser.add_argument("--timeout", type=int, default=1500)
    parser.add_argument(
        "--count",
        "--limit",
        dest="case_count",
        nargs="?",
        const=None,
        default=None,
        type=parse_case_count,
        metavar="{all|N}",
        help="number of selected cases; default: all (--limit is an alias)",
    )
    parser.add_argument("--threads", type=int, default=8)
    parser.add_argument(
        "--ratios",
        nargs="+",
        type=float,
        default=[0.8, 0.75, 0.5],
        help="shared Hybrid/DXD-threshold ratios",
    )
    parser.add_argument(
        "--algorithms",
        nargs="+",
        choices=tuple(ALGORITHM_LABELS),
        default=["hybrid", "dxd-threshold", "dxd"],
    )
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument(
        "--decision-log-dir",
        type=Path,
        help="optional per-case Hybrid cost-decision CSV output",
    )
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--resume", action="store_true")
    args = parser.parse_args()

    if not common.EXECUTABLE.is_file():
        parser.error(f"solver executable not found: {common.EXECUTABLE}")
    if args.timeout < 1:
        parser.error("--timeout must be positive")
    if args.threads < 1 or args.threads > 8:
        parser.error("--threads must be in [1,8]")
    if any(ratio < 0.0 or ratio > 1.0 for ratio in args.ratios):
        parser.error("--ratios values must be in [0,1]")

    try:
        cases = (
            select_cases_from_directory(args.input_dir)
            if args.input_dir is not None
            else common.select_cases(
                args.benchmark_set,
                args.case_file,
                args.case_count or 0,
            )
        )
    except (OSError, ValueError) as error:
        parser.error(str(error))
    if args.case_count is not None:
        cases = cases[: args.case_count]

    algorithms = list(dict.fromkeys(args.algorithms))
    variants = [
        (ratio, algorithm)
        for ratio in args.ratios
        for algorithm in algorithms
    ]
    print(
        f"cases={len(cases)} "
        f"ratios={','.join(str(ratio) for ratio in args.ratios)} "
        f"methods={','.join(ALGORITHM_LABELS[a] for a in algorithms)} "
        f"threads={args.threads} count="
        f"{args.case_count if args.case_count is not None else 'all'} "
        f"tasks_per_case={len(variants)}"
    )
    if args.dry_run:
        for case in cases:
            print(
                f"{case['dataset']}/{case['category']}/"
                f"{case['instance']} <- {case['path']}"
            )
        return 0

    output_dir = (
        args.output_dir.resolve() if args.output_dir else common.RESULTS
    )
    output_dir.mkdir(parents=True, exist_ok=True)
    decision_log_dir = (
        args.decision_log_dir.resolve()
        if args.decision_log_dir is not None
        else None
    )
    if decision_log_dir is not None:
        decision_log_dir.mkdir(parents=True, exist_ok=True)
    rows: list[dict[str, str]] = []
    run_id = datetime.now().strftime("%Y%m%d_%H%M%S")
    result_path = output_dir / f"hybrid_runtime_{run_id}.csv"
    if args.resume:
        candidates = sorted(output_dir.glob("hybrid_runtime_*.csv"))
        if not candidates:
            parser.error(f"no hybrid_runtime_*.csv found in {output_dir}")
        result_path = candidates[-1]
        with result_path.open(encoding="utf-8", newline="") as stream:
            rows = list(csv.DictReader(stream))
        if rows:
            run_id = rows[0]["run_id"]

    completed = {
        (row["instance"], row["algorithm"], row["threshold_ratio"])
        for row in rows
        if row["status"] == "success"
    }
    total = len(cases) * len(variants)
    index = 0
    for case in cases:
        for threshold_ratio, algorithm in variants:
            index += 1
            key = (case["instance"], algorithm, f"{threshold_ratio:g}")
            if key in completed:
                continue
            print(
                f"[{index}/{total}] {case['instance']} "
                f"{ALGORITHM_LABELS[algorithm]} r={threshold_ratio:g}",
                flush=True,
            )
            row, _ = run_case(
                case=case,
                algorithm=algorithm,
                threads=args.threads,
                timeout=args.timeout,
                threshold_ratio=threshold_ratio,
                decision_log_dir=decision_log_dir,
            )
            row["run_id"] = run_id
            rows.append(row)
            ensure_solution_consistency(rows)
            write_csv(result_path, rows)

    ensure_solution_consistency(rows)
    write_csv(result_path, rows)
    summary = {
        "run_id": run_id,
        "threads": args.threads,
        "cases": len(cases),
        "count": (
            args.case_count if args.case_count is not None else "all"
        ),
        "algorithms": algorithms,
        "timeout_s": args.timeout,
        "ratios": args.ratios,
        "decision_log_dir": (
            str(decision_log_dir) if decision_log_dir is not None else None
        ),
        "status_counts": {
            status: sum(row["status"] == status for row in rows)
            for status in sorted({row["status"] for row in rows})
        },
        "comparison": comparison_summary(rows),
    }
    summary_path = output_dir / f"hybrid_runtime_{run_id}_summary.json"
    summary_path.write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(f"results: {result_path}")
    print(f"summary: {summary_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
