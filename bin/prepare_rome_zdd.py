#!/usr/bin/env python3
"""Compile the new ROME matrices to ZDDs one instance at a time."""

from __future__ import annotations

import argparse
import csv
import os
import re
import signal
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

from benchmark_catalog import ROOT, MANIFEST_ROOT


COMPILER = ROOT / "data" / "ToZdd" / "zdd_compiler"
DEFAULT_OUTPUT = ROOT / "data" / "ToZdd" / "rome3_output"
MANIFEST = ROOT / "data" / "benchmarks" / "benchmark_manifest.csv"


def read_cases(case_file: Path | None, limit: int) -> list[dict[str, str]]:
    with MANIFEST.open(encoding="utf-8", newline="") as stream:
        rows = [row for row in csv.DictReader(stream) if row["dataset"] == "new"]
    if case_file is not None:
        names = {
            token
            for line in case_file.read_text(encoding="utf-8").splitlines()
            for token in re.split(r"[,\s]+", line)
            if token
        }
        rows = [row for row in rows if row["instance"] in names]
        missing = sorted(names - {row["instance"] for row in rows})
        if missing:
            raise ValueError(f"unknown new ROME cases: {', '.join(missing)}")
    rows.sort(key=lambda row: row["instance"])
    return rows[:limit] if limit > 0 else rows


def compile_one(input_path: Path, output_path: Path, timeout: int) -> dict[str, str]:
    started = time.perf_counter()
    process = subprocess.Popen(
        [str(COMPILER), str(input_path), str(output_path)],
        cwd=str(COMPILER.parent),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
        start_new_session=True,
    )
    try:
        output, _ = process.communicate(timeout=timeout)
        exit_code: int | None = process.returncode
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        output, _ = process.communicate()
        exit_code = None
    if exit_code is None:
        status = "timeout"
    elif exit_code != 0:
        status = "error"
    elif output_path.is_file() and output_path.stat().st_size:
        status = "success"
    else:
        status = "missing_output"
    return {
        "status": status,
        "time_s": f"{time.perf_counter() - started:.6f}",
        "output_bytes": str(output_path.stat().st_size) if output_path.exists() else "",
        "exit_code": "" if exit_code is None else str(exit_code),
        "error": "" if status == "success" else " | ".join(
            output.strip().splitlines()[-3:]
        )[:1200],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case-file", type=Path)
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--timeout", type=int, default=1500)
    parser.add_argument("--limit", type=int, default=0)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--allow-fail", action="store_true")
    args = parser.parse_args()

    if not COMPILER.is_file():
        parser.error(f"ZDD compiler not found: {COMPILER}")
    try:
        cases = read_cases(args.case_file, args.limit)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    output_dir = args.output_dir.resolve()
    print(f"cases={len(cases)} output={output_dir}")
    if args.dry_run:
        for case in cases:
            print(f"{case['instance']} <- {ROOT / case['path']}")
        return 0
    output_dir.mkdir(parents=True, exist_ok=True)
    result_path = output_dir / "compile_results.csv"
    fields = (
        "run_id",
        "instance",
        "input",
        "output",
        "status",
        "time_s",
        "output_bytes",
        "exit_code",
        "error",
    )
    run_id = datetime.now().strftime("%Y%m%d_%H%M%S")
    failures = 0
    with result_path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        for index, case in enumerate(cases, 1):
            input_path = ROOT / case["path"]
            output_path = output_dir / f"{case['instance']}.zdd"
            if output_path.is_file() and output_path.stat().st_size:
                result = {
                    "status": "success",
                    "time_s": "0",
                    "output_bytes": str(output_path.stat().st_size),
                    "exit_code": "",
                    "error": "",
                }
                print(f"[{index}/{len(cases)}] {case['instance']} exists")
            else:
                print(f"[{index}/{len(cases)}] {case['instance']}", flush=True)
                result = compile_one(input_path, output_path, args.timeout)
            if result["status"] != "success":
                failures += 1
            writer.writerow(
                {
                    "run_id": run_id,
                    "instance": case["instance"],
                    "input": case["path"],
                    "output": str(output_path.relative_to(ROOT)),
                    **result,
                }
            )
            stream.flush()
    print(f"results: {result_path}")
    print(f"failures: {failures}")
    if failures and not args.allow_fail:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
