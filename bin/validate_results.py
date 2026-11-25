#!/usr/bin/env python3
"""Validate completeness and solution consistency of experiment CSV files."""

from __future__ import annotations

import argparse
import csv
import sys
from collections import Counter, defaultdict
from pathlib import Path

import benchmark_catalog


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(encoding="utf-8", newline="") as stream:
        return list(csv.DictReader(stream))


def report_errors(errors: list[str]) -> int:
    if errors:
        for error in errors[:100]:
            print(f"[error] {error}", file=sys.stderr)
        if len(errors) > 100:
            print(f"[error] ... {len(errors) - 100} more", file=sys.stderr)
        return 1
    print("[ok] result validation passed")
    return 0


def validate_exact(batch_path: Path, d3x_path: Path) -> int:
    rows = [row for row in read_csv(batch_path) if row["solver"] in ("DLX", "DXZ")]
    rows.extend(read_csv(d3x_path))
    expected = {
        row.instance
        for row in benchmark_catalog.build_catalog()[0]
        if row.dataset == "new"
    }
    grouped: dict[str, dict[str, list[dict[str, str]]]] = defaultdict(
        lambda: defaultdict(list)
    )
    for row in rows:
        grouped[row["instance"]][row["solver"]].append(row)
    errors = []
    for instance in sorted(expected):
        by_solver = grouped.get(instance, {})
        for solver in ("DLX", "DXZ", "D3X"):
            if not by_solver.get(solver):
                errors.append(f"{instance}: missing {solver} record")
        successful = {
            solver: {
                row["solutions"]
                for row in by_solver.get(solver, [])
                if row["status"] == "success" and row["solutions"]
            }
            for solver in ("DLX", "DXZ", "D3X")
        }
        all_counts = set().union(*successful.values())
        if len(all_counts) > 1:
            errors.append(
                f"{instance}: solution mismatch {dict(successful)}"
            )
    print(f"exact-cover instances checked: {len(expected)}")
    return report_errors(errors)


def validate_dxd(path: Path) -> int:
    rows = read_csv(path)
    grouped: dict[str, list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        grouped[row["instance"]].append(row)
    errors = []
    for instance, group in grouped.items():
        counts = {
            row["solutions"]
            for row in group
            if row["status"] == "success" and row["solutions"]
        }
        if len(counts) > 1:
            errors.append(f"{instance}: successful solution sets differ: {counts}")
        errors.extend(
            f"{instance}: {row['variant']} T{row['threads']} status={row['status']}"
            for row in group
            if row["status"] == "solution_mismatch"
        )
    print(f"DXD instances checked: {len(grouped)}")
    return report_errors(errors)


def validate_thread(path: Path, rounds: int) -> int:
    rows = read_csv(path)
    grouped = Counter(
        (row["instance"], row["variant"], row["threads"]) for row in rows
    )
    errors = [
        f"{key}: expected {rounds} rows, found {count}"
        for key, count in grouped.items()
        if count != rounds
    ]
    errors.extend(
        f"{row['instance']} {row['variant']} T{row['threads']} "
        f"repeat={row['repeat']} status={row['status']}"
        for row in rows
        if row["status"] == "solution_mismatch"
    )
    print(f"thread rows checked: {len(rows)} across {len(grouped)} configs")
    return report_errors(errors)


def validate_mc(path: Path) -> int:
    rows = read_csv(path)
    grouped: dict[str, list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        grouped[row["instance"]].append(row)
    errors = []
    for instance, group in grouped.items():
        counts = {
            row["solutions"]
            for row in group
            if row["status"] == "success" and row["solutions"]
        }
        if len(counts) > 1:
            errors.append(f"{instance}: counter counts differ: {counts}")
        errors.extend(
            f"{instance}: {row['solver']} status={row['status']}"
            for row in group
            if row["status"] == "solution_mismatch"
        )
    print(f"model-counting instances checked: {len(grouped)}")
    return report_errors(errors)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="kind", required=True)

    exact = subparsers.add_parser("exact")
    exact.add_argument("--batch-runs", type=Path, required=True)
    exact.add_argument("--d3x-runs", type=Path, required=True)

    dxd = subparsers.add_parser("dxd")
    dxd.add_argument("--runs", type=Path, required=True)

    thread = subparsers.add_parser("thread")
    thread.add_argument("--runs", type=Path, required=True)
    thread.add_argument("--rounds", type=int, default=10)

    mc = subparsers.add_parser("mc")
    mc.add_argument("--best", type=Path, required=True)

    args = parser.parse_args()
    if args.kind == "exact":
        return validate_exact(args.batch_runs, args.d3x_runs)
    if args.kind == "dxd":
        return validate_dxd(args.runs)
    if args.kind == "thread":
        return validate_thread(args.runs, args.rounds)
    return validate_mc(args.best)


if __name__ == "__main__":
    raise SystemExit(main())
