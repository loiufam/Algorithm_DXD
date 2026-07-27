#!/usr/bin/env python3
"""Collect only replacement-link and tree-edge-cut counters for DynDXD."""

import argparse
import csv
from pathlib import Path

import run_cc_experiment as common
import run_cc_dynamics_experiment as dynamics


FIELDS = ("dataset", "instance", "merges", "tree_edge_cuts")


def write_rows(path, rows):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=FIELDS)
        writer.writeheader()
        writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=Path, default=common.DEFAULT_REPORT)
    parser.add_argument("--executable", type=Path, default=common.ROOT / "bin/main-2")
    parser.add_argument(
        "--output",
        type=Path,
        default=common.ROOT / "results/cc_merges_tree_edge_cuts.csv",
    )
    parser.add_argument("--timeout", type=int, default=600)
    parser.add_argument("--limit", type=int)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--resume", action="store_true")
    args = parser.parse_args()

    inputs = common.input_index(common.DEFAULT_INPUT_DIRS)
    names = dynamics.report_instances(args.report)
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

    rows = []
    if args.resume and args.output.is_file():
        with args.output.open(encoding="utf-8", newline="") as stream:
            rows = list(csv.DictReader(stream))
    completed = {row["instance"] for row in rows if row.get("merges") != ""}

    for number, name in enumerate(names, 1):
        if name in completed:
            print(f"[{number}/{len(names)}] {name} (already complete)", flush=True)
            continue
        path = inputs[name]
        print(f"[{number}/{len(names)}] {name}", flush=True)
        measured = dynamics.run_case(args.executable, path, args.timeout)

        has_counters = measured.get("status") in {"success", "sampled"}
        row = {
            "dataset": path.parent.name,
            "instance": name,
            "merges": measured.get("merges", "") if has_counters else "",
            "tree_edge_cuts": (
                measured.get("tree_edge_cuts", "") if has_counters else ""
            ),
        }
        rows = [existing for existing in rows if existing["instance"] != name]
        rows.append(row)
        write_rows(args.output, rows)

    print(f"wrote {len(rows)} rows to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
