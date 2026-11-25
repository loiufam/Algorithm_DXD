#!/usr/bin/env python3
"""Fast preflight checks for manifests, CNF encodings, and solver entry points."""

from __future__ import annotations

import argparse
import importlib.util
import os
import re
import subprocess
import sys
import threading
from pathlib import Path

import benchmark_catalog


ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = ROOT.parent
MC_ROOT = WORKSPACE / "MC_BINS"
D3X_ROOT = WORKSPACE / "D3X"
MATRIX_MODULE = ROOT / "data" / "ToCnf" / "matrix_to_cnf.py"


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot import {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def check_files() -> None:
    required = (
        ROOT / "bin" / "main",
        ROOT / "data" / "ToZdd" / "zdd_compiler",
        D3X_ROOT / "bin" / "d3x",
        MC_ROOT / "d4",
        MC_ROOT / "sharpSAT",
        MC_ROOT / "exactMC",
        MC_ROOT / "ganak",
        MC_ROOT / "DeMoniaC",
        MC_ROOT / "countAntom",
        MC_ROOT / "discount" / "build" / "discount",
        MC_ROOT / "flow_cutter_pace17",
    )
    missing = [str(path) for path in required if not path.is_file()]
    if missing:
        raise FileNotFoundError("missing executables: " + ", ".join(missing))


def brute_force_covers(sets: list[set[int]], points: int) -> int:
    count = 0
    for mask in range(1 << len(sets)):
        covered = set()
        valid = True
        for index, row in enumerate(sets):
            if not (mask & (1 << index)):
                continue
            if covered & row:
                valid = False
                break
            covered |= row
        if valid and covered == set(range(1, points + 1)):
            count += 1
    return count


def check_encodings(memory_gb: int) -> None:
    matrix = load_module("matrix_to_cnf", MATRIX_MODULE)
    sets = [{1, 2}, {1, 2}, {3}]
    points = 3
    expected = brute_force_covers(sets, points)
    counters = load_module("mc_run_counters", MC_ROOT / "run_counters.py")
    semaphore = threading.Semaphore(1)
    for encoding in ("pairwise", "ladder", "bitwise", "matrix"):
        fd, total_vars, clauses = matrix.encode_exact_cover_to_memfd(
            sets, points, encoding
        )
        try:
            result = counters.call_solver(
                "d4",
                f"/proc/{os.getpid()}/fd/{fd}",
                timeout=20,
                memory_gb=memory_gb,
                solver_jobs=semaphore,
            )
        finally:
            os.close(fd)
        if result["status"] != "success" or int(result["solutions"]) != expected:
            raise AssertionError(
                f"{encoding}: expected {expected}, got {result}"
            )
        print(
            f"[ok] {encoding}: vars={total_vars} clauses={clauses} "
            f"solutions={expected}"
        )


def check_d3x() -> None:
    fixture = ROOT / "data" / "ToZdd" / "output" / "bell-03.zdd"
    result = subprocess.run(
        [str(D3X_ROOT / "bin" / "d3x"), "-z", str(fixture)],
        cwd=str(D3X_ROOT),
        capture_output=True,
        text=True,
        timeout=30,
    )
    output = result.stdout + result.stderr
    match = re.search(r"Solutions:\s*(\d+)", output)
    if result.returncode != 0 or not match or match.group(1) != "5":
        raise AssertionError(f"D3X fixture failed: rc={result.returncode}, {output}")
    print("[ok] D3X fixture: solutions=5")


def check_all_counters() -> None:
    counters = load_module("mc_run_counters", MC_ROOT / "run_counters.py")
    fd = os.memfd_create("setup.cnf")
    try:
        os.write(fd, b"p cnf 2 2\n1 0\n-1 2 0\n")
        os.lseek(fd, 0, os.SEEK_SET)
        semaphore = threading.Semaphore(1)
        for solver in counters.SOLVERS:
            result = counters.call_solver(
                solver,
                f"/proc/{os.getpid()}/fd/{fd}",
                timeout=30,
                memory_gb=8,
                solver_jobs=semaphore,
            )
            if result["status"] != "success" or int(result["solutions"]) != 1:
                raise AssertionError(f"{solver}: {result}")
            print(f"[ok] counter {solver}: models=1")
    finally:
        os.close(fd)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--all-counters",
        action="store_true",
        help="also run all seven counters on a tiny CNF",
    )
    parser.add_argument("--memory-gb", type=int, default=8)
    args = parser.parse_args()
    try:
        benchmark_catalog.build_catalog()
        print("[ok] benchmark catalog counts and paths")
        check_files()
        check_encodings(args.memory_gb)
        check_d3x()
        if args.all_counters:
            check_all_counters()
    except Exception as error:
        print(f"[error] {error}", file=sys.stderr)
        return 1
    print("preflight checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
