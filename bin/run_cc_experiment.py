#!/usr/bin/env python3
"""Run single-thread DynDXD/DXD CC timing experiments selected from the report."""

import argparse
import csv
import re
import subprocess
import sys
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_REPORT = ROOT / "data/batch_2/Final_Experiment_Report.xlsx"
DEFAULT_OUTPUT = ROOT / "results/cc_cpu_experiment.csv"
DEFAULT_INPUT_DIRS = (
    ROOT / "data/batch_2/dominoes_set",
    ROOT / "data/batch_2/exact_cover_benchmarks",
    ROOT / "data/batch_2/set_partition_benchmarks",
    ROOT / "data/batch_2/graphs_set",
)
NS = {"x": "http://schemas.openxmlformats.org/spreadsheetml/2006/main"}

TIME_RE = re.compile(r"^Time:\s*([\d.]+)\s*s", re.MULTILINE)
CC_RE = re.compile(r"^(?:Dyn|DXD) CC CPU:\s*([\d.]+)\s*s", re.MULTILINE)
SOL_RE = re.compile(r"^Solutions:\s*(\S+)", re.MULTILINE)
BLOCK_RE = re.compile(r"^Max Blocks:\s*(\d+)", re.MULTILINE)


def column_index(cell_ref):
    result = 0
    for char in re.match(r"[A-Z]+", cell_ref).group(0):
        result = result * 26 + ord(char) - ord("A") + 1
    return result - 1


def read_xlsx_rows(path):
    """Read the first XLSX worksheet using only the Python standard library."""
    with zipfile.ZipFile(path) as archive:
        strings = []
        if "xl/sharedStrings.xml" in archive.namelist():
            root = ET.fromstring(archive.read("xl/sharedStrings.xml"))
            for item in root.findall("x:si", NS):
                strings.append("".join(node.text or "" for node in item.iterfind(".//x:t", NS)))

        sheet = ET.fromstring(archive.read("xl/worksheets/sheet1.xml"))
        for row in sheet.findall(".//x:sheetData/x:row", NS):
            values = {}
            for cell in row.findall("x:c", NS):
                value = cell.find("x:v", NS)
                text = "" if value is None else value.text or ""
                if cell.get("t") == "s" and text:
                    text = strings[int(text)]
                values[column_index(cell.get("r"))] = text
            width = max(values, default=-1) + 1
            yield [values.get(index, "") for index in range(width)]


def eligible_instances(report):
    rows = iter(read_xlsx_rows(report))
    header = next(rows)
    required = ("Instance", "#cols", "#rows", "DXD-T1", "DynDXD-T1")
    missing = [name for name in required if name not in header]
    if missing:
        raise ValueError(f"report is missing columns: {', '.join(missing)}")
    columns = {name: header.index(name) for name in required}

    selected = []
    for row in rows:
        row += [""] * (len(header) - len(row))
        dxd = row[columns["DXD-T1"]].strip()
        dyn = row[columns["DynDXD-T1"]].strip()
        try:
            float(dxd)
            float(dyn)
        except ValueError:
            continue
        else:
            selected.append({
                "instance": row[columns["Instance"]].strip(),
                "cols": row[columns["#cols"]].strip(),
                "rows": row[columns["#rows"]].strip(),
                "report_dxd_time_s": dxd,
                "report_dyndxd_time_s": dyn,
            })
    return selected


def input_index(input_dirs):
    index = {}
    for folder in input_dirs:
        # Keep this consistent with batch_runner.get_input_files(): benchmark
        # inputs include both graph/set-partition .txt files and exact-cover
        # .ec files.
        for path in sorted(candidate for candidate in folder.rglob("*") if candidate.is_file()):
            previous = index.setdefault(path.stem, path)
            if previous != path:
                raise ValueError(
                    f"duplicate instance name {path.stem!r}: {previous} and {path}"
                )
    return index


def run_solver(executable, algorithm, input_path, timeout):
    command = [str(executable), "-a", algorithm, "-i", str(input_path), "-t", "1"]
    try:
        process = subprocess.run(command, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return {"status": "timeout", "time_s": "", "cc_cpu_s": "",
                "solutions": "", "max_blocks": ""}

    output = process.stdout + process.stderr
    if process.returncode != 0 or "超时" in output:
        status = "timeout" if "超时" in output else f"error({process.returncode})"
    else:
        status = "success"

    def match(pattern):
        found = pattern.search(output)
        return found.group(1) if found else ""

    return {
        "status": status,
        "time_s": match(TIME_RE),
        "cc_cpu_s": match(CC_RE),
        "solutions": match(SOL_RE),
        "max_blocks": match(BLOCK_RE),
    }


def estimate_dxd_cc_cost(dyn, dxd):
    """Project DXD's BFS CC share onto the DynDXD wall-clock time."""
    try:
        dxd_time = float(dxd["time_s"])
        dxd_cc_time = float(dxd["cc_cpu_s"])
        dyndxd_time = float(dyn["time_s"])
    except (KeyError, TypeError, ValueError):
        return "", ""
    if dxd_time <= 0.0:
        return "", ""

    dxd_cc_ratio = dxd_cc_time / dxd_time
    return dxd_cc_ratio, dyndxd_time * dxd_cc_ratio


def write_results(path, rows):
    path.parent.mkdir(parents=True, exist_ok=True)
    fields = (
        "instance", "input", "rows", "cols",
        "report_dyndxd_time_s", "report_dxd_time_s",
        "dyndxd_status", "dyndxd_time_s", "dyndxd_cc_cpu_s",
        "dxd_status", "dxd_time_s", "dxd_cc_cpu_s", "dxd_cc_cpu_ratio",
        "dyndxd_cc_cpu_from_dxd_ratio_s",
        "solutions", "max_blocks",
    )
    with path.open("w", encoding="utf-8", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser(
        description="Run single-thread DynDXD and DXD on cases that did not time out in the final report."
    )
    parser.add_argument("--report", type=Path, default=DEFAULT_REPORT)
    parser.add_argument("--executable", type=Path, default=ROOT / "bin/main")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--input-dir", action="append", type=Path, dest="input_dirs")
    parser.add_argument("--timeout", type=int, default=1500)
    parser.add_argument("--limit", type=int, help="run only the first N selected cases")
    parser.add_argument("--dry-run", action="store_true", help="only list selected inputs")
    args = parser.parse_args()

    selected = eligible_instances(args.report)
    if args.limit is not None:
        selected = selected[:args.limit]
    inputs = input_index(args.input_dirs or DEFAULT_INPUT_DIRS)

    missing = [item["instance"] for item in selected if item["instance"] not in inputs]
    if missing:
        print(f"warning: {len(missing)} selected instances have no input file", file=sys.stderr)

    runnable = [item for item in selected if item["instance"] in inputs]
    print(f"report selected {len(selected)} cases; {len(runnable)} input files found")
    if args.dry_run:
        for item in runnable:
            print(inputs[item["instance"]])
        return 0
    if not args.executable.is_file():
        parser.error(f"executable not found: {args.executable}; build the project first")

    results = []
    for number, item in enumerate(runnable, 1):
        name = item["instance"]
        path = inputs[name]
        print(f"[{number}/{len(runnable)}] {name}", flush=True)
        dyn = run_solver(args.executable, "ddxd", path, args.timeout)
        dxd = run_solver(args.executable, "dxd", path, args.timeout)
        dxd_cc_ratio, projected_cc_time = estimate_dxd_cc_cost(dyn, dxd)
        result = {
            **item,
            "input": str(path.relative_to(ROOT)),
            "dyndxd_status": dyn["status"],
            "dyndxd_time_s": dyn["time_s"],
            "dyndxd_cc_cpu_s": dyn["cc_cpu_s"],
            "dxd_status": dxd["status"],
            "dxd_time_s": dxd["time_s"],
            "dxd_cc_cpu_s": dxd["cc_cpu_s"],
            "dxd_cc_cpu_ratio": dxd_cc_ratio,
            "dyndxd_cc_cpu_from_dxd_ratio_s": projected_cc_time,
            "solutions": dyn["solutions"] or dxd["solutions"],
            "max_blocks": dyn["max_blocks"],
        }
        if dyn["solutions"] and dxd["solutions"] and dyn["solutions"] != dxd["solutions"]:
            result["dyndxd_status"] = result["dxd_status"] = "solution_mismatch"
        results.append(result)
        write_results(args.output, results)

    print(f"wrote {len(results)} rows to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
