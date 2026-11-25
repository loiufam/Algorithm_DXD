#!/usr/bin/env python3
"""Run single-thread DXD and Hybrid connectivity timing experiments."""

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


ROOT = Path(__file__).resolve().parents[1]
EXECUTABLE = ROOT / "bin" / "main"
MANIFEST = ROOT / "data" / "benchmarks" / "benchmark_manifest.csv"
RESULTS = ROOT / "results"

TIME_RE = re.compile(r"^Time:\s*([\d.eE+-]+)\s*s", re.MULTILINE)
SOL_RE = re.compile(r"^Solutions(?: \(scientific\))?:\s*(\S+)", re.MULTILINE)
BLOCKS_RE = re.compile(r"^Max Blocks:\s*(\d+)", re.MULTILINE)
DXD_CPU_RE = re.compile(r"^DXD CC BFS CPU:\s*([\d.eE+-]+)", re.MULTILINE)
DXD_BFS_TIME_RE = re.compile(
    r"^(?:CC Stats )?DXD BFS Time:\s*([\d.eE+-]+)", re.MULTILINE
)
HYBRID_METRIC_RE = re.compile(
    r"^Hybrid ([^:]+):\s*([\d.eE+-]+)(?:\s*s)?\s*$", re.MULTILINE
)

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
    "bfs_queries",
    "ett_decrement_updates",
    "solutions",
    "max_blocks",
    "solutions_match",
    "walk_time_s",
    "exit_code",
    "error",
)


def read_manifest() -> list[dict[str, str]]:
    with MANIFEST.open(encoding="utf-8", newline="") as stream:
        return list(csv.DictReader(stream))


def read_case_names(path: Path) -> set[str]:
    return {
        token
        for line in path.read_text(encoding="utf-8").splitlines()
        for token in re.split(r"[,\s]+", line)
        if token
    }


def select_cases(
    benchmark_set: str,
    case_file: Path | None,
    limit: int,
) -> list[dict[str, str]]:
    rows = read_manifest()
    if benchmark_set != "all":
        rows = [row for row in rows if row["dataset"] == benchmark_set]
    if case_file is not None:
        names = read_case_names(case_file)
        rows = [row for row in rows if row["instance"] in names]
        missing = sorted(names - {row["instance"] for row in rows})
        if missing:
            raise ValueError(
                "case file includes instances outside the selected set: "
                + ", ".join(missing[:20])
            )
    rows.sort(key=lambda row: (row["category"], row["instance"]))
    if len({row["instance"] for row in rows}) != len(rows):
        raise ValueError("duplicate instance names in benchmark manifest")
    return rows[:limit] if limit > 0 else rows


def first(pattern: re.Pattern[str], text: str) -> str:
    match = pattern.search(text)
    return match.group(1) if match else ""


def parse_output(algorithm: str, output: str) -> dict[str, str]:
    metrics = {
        name.strip().lower().replace(" ", "_"): value
        for name, value in HYBRID_METRIC_RE.findall(output)
    }
    bfs = (
        first(DXD_BFS_TIME_RE, output)
        or first(DXD_CPU_RE, output)
        if algorithm == "dxd"
        else metrics.get("bfs_time", "")
    )
    build = metrics.get("ett_build_time", "")
    decrement = metrics.get("ett_decrement_time", "")
    extraction = metrics.get("ett_extraction_time", "")
    release = metrics.get("ett_release_time", "")
    if algorithm == "dxd":
        cc_time = bfs
    else:
        values = [build, decrement, extraction, release]
        cc_time = f"{sum(float(value) for value in values):.9f}" if all(values) else ""
    return {
        "time_s": first(TIME_RE, output),
        "cc_time_s": cc_time,
        "bfs_time_s": bfs,
        "ett_build_time_s": build,
        "ett_decrement_time_s": decrement,
        "ett_extraction_time_s": extraction,
        "ett_release_time_s": release,
        "bfs_queries": metrics.get("bfs_queries", ""),
        "ett_decrement_updates": metrics.get("ett_decrement_updates", ""),
        "solutions": first(SOL_RE, output),
        "max_blocks": first(BLOCKS_RE, output),
    }


def run_case(
    case: dict[str, str],
    algorithm: str,
    timeout: int,
) -> tuple[dict[str, str], str]:
    command = [
        str(EXECUTABLE),
        "-a",
        algorithm,
        "-i",
        str(ROOT / case["path"]),
        "-t",
        "1",
        "--time-limit",
        str(timeout),
        "--skip-dnnf-size",
    ]
    if algorithm == "dxd":
        command.append("--enable-cc-time")
    else:
        command.append("--debug")
    started = time.perf_counter()
    process = subprocess.Popen(
        command,
        cwd=str(ROOT),
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
    wall_time = time.perf_counter() - started
    parsed = parse_output(algorithm, output)
    if exit_code is None or "搜索超时" in output or "Time bound" in output:
        status = "timeout"
    elif exit_code != 0:
        status = "error"
    elif parsed["time_s"] and parsed["solutions"]:
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
        "solutions_match": "",
        "walk_time_s": f"{wall_time:.6f}",
        "exit_code": "" if exit_code is None else str(exit_code),
        "error": (
            ""
            if status == "success"
            else " | ".join(output.strip().splitlines()[-3:])[:1000]
        ),
        **parsed,
    }
    return row, output


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--benchmark-set", choices=("old", "new", "all"), default="all"
    )
    parser.add_argument("--case-file", type=Path)
    parser.add_argument("--timeout", type=int, default=1500)
    parser.add_argument("--limit", type=int, default=0)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument(
        "--resume",
        action="store_true",
        help="resume the latest cc_cpu_experiment CSV in --output-dir",
    )
    args = parser.parse_args()
    if not EXECUTABLE.is_file():
        parser.error(f"solver executable not found: {EXECUTABLE}")
    try:
        cases = select_cases(args.benchmark_set, args.case_file, args.limit)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    print(f"cases={len(cases)} algorithms=DXD,Hybrid threads=1")
    if args.dry_run:
        for case in cases:
            print(
                f"{case['dataset']}/{case['category']}/"
                f"{case['instance']} <- {case['path']}"
            )
        return 0

    output_dir = args.output_dir.resolve() if args.output_dir else RESULTS
    output_dir.mkdir(parents=True, exist_ok=True)
    existing_rows: list[dict[str, str]] = []
    if args.resume:
        candidates = sorted(output_dir.glob("cc_cpu_experiment_*.csv"))
        if not candidates:
            parser.error(f"no cc_cpu_experiment CSV found in {output_dir}")
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
        result_path = output_dir / f"cc_cpu_experiment_{run_id}.csv"

    all_rows = list(existing_rows)
    completed = {
        (row["instance"], row["algorithm"])
        for row in existing_rows
        if row["status"] == "success"
    }
    for index, case in enumerate(cases, 1):
        print(f"[{index}/{len(cases)}] {case['instance']}", flush=True)
        for algorithm in ("dxd", "hybrid"):
            if (case["instance"], algorithm) in completed:
                continue
            row, _ = run_case(case, algorithm, args.timeout)
            row["run_id"] = run_id
            all_rows.append(row)
        case_rows = [
            row for row in all_rows if row["instance"] == case["instance"]
        ]
        successful = [row for row in case_rows if row["solutions"]]
        if len(successful) == 2:
            matches = successful[0]["solutions"] == successful[1]["solutions"]
            for row in case_rows:
                row["solutions_match"] = (
                    str(matches) if row["solutions"] else ""
                )
                if row["solutions"] and not matches:
                    row["status"] = "solution_mismatch"
        with result_path.open("w", encoding="utf-8", newline="") as stream:
            writer = csv.DictWriter(
                stream, fieldnames=FIELDS, extrasaction="ignore"
            )
            writer.writeheader()
            writer.writerows(all_rows)
    (output_dir / f"cc_cpu_experiment_{run_id}.json").write_text(
        json.dumps(
            {
                "run_id": run_id,
                "cases": len(cases),
                "algorithms": ["dxd", "hybrid"],
                "threads": 1,
                "timeout_s": args.timeout,
            },
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )
    print(f"results: {result_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
