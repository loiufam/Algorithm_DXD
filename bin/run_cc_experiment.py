#!/usr/bin/env python3
"""Run single-thread DynDXD/DXD CC timing experiments selected from the report."""

import argparse
import csv
import re
import subprocess
import sys
import xml.etree.ElementTree as ET
import zipfile
from concurrent.futures import ThreadPoolExecutor, as_completed
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
DYN_BFS_RE = re.compile(r"^Dyn CC BFS CPU:\s*([\d.]+)\s*s", re.MULTILINE)
DYN_DEC_RE = re.compile(r"^Dyn CC Decrement CPU:\s*([\d.]+)\s*s", re.MULTILINE)
DXD_BFS_RE = re.compile(r"^DXD CC BFS CPU:\s*([\d.]+)\s*s", re.MULTILINE)
DXD_COVER_RE = re.compile(r"^DXD CC Cover CPU:\s*([\d.]+)\s*s", re.MULTILINE)
DXD_UNCOVER_RE = re.compile(r"^DXD CC Uncover CPU:\s*([\d.]+)\s*s", re.MULTILINE)
SOL_RE = re.compile(r"^Solutions:\s*(\S+)", re.MULTILINE)


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

def read_case_names(path):
    """Read instance names separated by newlines or commas, preserving order."""
    names = []
    seen = set()
    for name in re.split(r"[,\r\n]+", path.read_text(encoding="utf-8")):
        name = name.strip()
        if name and name not in seen:
            names.append(name)
            seen.add(name)
    if not names:
        raise ValueError(f"case file is empty: {path}")
    return names


def instances_from_case_file(path, report):
    """Build rows for explicitly requested cases, adding report data when present."""
    report_rows = iter(read_xlsx_rows(report))
    header = next(report_rows)
    required = ("Instance", "#cols", "#rows", "DXD-T1", "DynDXD-T1")
    missing = [name for name in required if name not in header]
    if missing:
        raise ValueError(f"report is missing columns: {', '.join(missing)}")
    columns = {name: header.index(name) for name in required}
    metadata = {}
    for row in report_rows:
        row += [""] * (len(header) - len(row))
        name = row[columns["Instance"]].strip()
        metadata[name] = {
            "instance": name,
            "cols": row[columns["#cols"]].strip(),
            "rows": row[columns["#rows"]].strip(),
            "report_dxd_time_s": row[columns["DXD-T1"]].strip(),
            "report_dyndxd_time_s": row[columns["DynDXD-T1"]].strip(),
        }

    blank = {"cols": "", "rows": "", "report_dxd_time_s": "",
             "report_dyndxd_time_s": ""}
    return [{"instance": name, **blank, **metadata.get(name, {})}
            for name in read_case_names(path)]


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


def run_solver(executable, algorithm, input_path, timeout, cc_ett_max_calls=0):
    command = [
        str(executable), "-a", algorithm, "-i", str(input_path), "-t", "1",
        "--enable-cc-time",
        "--cc-ett-max-calls", str(cc_ett_max_calls),
    ]
    try:
        process = subprocess.run(command, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return {"status": "timeout", "time_s": "", "cc_cpu_s": "",
                "bfs_cpu_s": "", "update_cpu_s": "", "cover_cpu_s": "",
                "uncover_cpu_s": "",
                "solutions": ""}

    output = process.stdout + process.stderr
    if process.returncode != 0 or "超时" in output:
        status = "timeout" if "超时" in output else f"error({process.returncode})"
    else:
        status = "success"

    def match(pattern):
        found = pattern.search(output)
        return found.group(1) if found else ""

    bfs = match(DYN_BFS_RE if algorithm == "ddxd" else DXD_BFS_RE)
    update = match(DYN_DEC_RE)
    cover = match(DXD_COVER_RE)
    uncover = match(DXD_UNCOVER_RE)
    parts = (bfs, update) if algorithm == "ddxd" else (bfs, cover, uncover)
    cc_cpu = (f"{sum(map(float, parts)):.6f}" if all(parts) else match(CC_RE))

    return {
        "status": status,
        "time_s": match(TIME_RE),
        "cc_cpu_s": cc_cpu,
        "bfs_cpu_s": bfs,
        "update_cpu_s": update,
        "cover_cpu_s": cover,
        "uncover_cpu_s": uncover,
        # Retained in memory only to validate that both algorithms agree.
        "solutions": match(SOL_RE),
    }


def write_results(path, rows):
    path.parent.mkdir(parents=True, exist_ok=True)
    fields = (
        "instance",
        "Dyn_time", "Dyn_cc_cpu_time", "DXD_time", "DXD_cc_cpu_time",
    )
    with path.open("w", encoding="utf-8", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def dataset_output(output_dir, dataset):
    return output_dir / f"cc_cpu_{dataset}.csv"


def run_dataset(dataset, entries, args):
    """Run one dataset sequentially; the four datasets run concurrently."""
    rows = []
    output = dataset_output(args.output_dir, dataset)
    write_results(output, rows)
    for number, (item, path) in enumerate(entries, 1):
        name = item["instance"]
        print(f"[{dataset} {number}/{len(entries)}] {name}", flush=True)
        dyn = run_solver(
            args.executable, "ddxd", path, args.timeout, args.cc_ett_max_calls
        )
        dxd = run_solver(args.executable, "dxd", path, args.timeout)
        row = {
            "instance": name,
            "Dyn_time": dyn["time_s"],
            # Solver aggregate is exactly BFS + decremental update in Dyn mode.
            "Dyn_cc_cpu_time": dyn["cc_cpu_s"],
            "DXD_time": dxd["time_s"],
            # Solver aggregate is exactly BFS + cover + uncover in DXD mode.
            "DXD_cc_cpu_time": dxd["cc_cpu_s"],
        }
        if dyn["solutions"] and dxd["solutions"] and dyn["solutions"] != dxd["solutions"]:
            print(f"warning: solution mismatch for {name}", file=sys.stderr, flush=True)
        rows.append(row)
        write_results(output, rows)
    print(f"[{dataset}] wrote {len(rows)} rows to {output}", flush=True)
    return rows


def main():
    parser = argparse.ArgumentParser(
        description="Run single-thread DynDXD and DXD on cases that did not time out in the final report."
    )
    parser.add_argument("--report", type=Path, default=DEFAULT_REPORT)
    parser.add_argument("--executable", type=Path, default=ROOT / "bin/main")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument(
        "--output-dir", type=Path, default=ROOT / "results/cc_cpu_datasets",
        help="directory for per-dataset checkpoint CSV files",
    )
    parser.add_argument(
        "--case-file", type=Path,
        help="run only names in this UTF-8 text file (one per line; commas also accepted)",
    )
    parser.add_argument("--input-dir", action="append", type=Path, dest="input_dirs")
    parser.add_argument("--timeout", type=int, default=600)
    parser.add_argument("--workers", type=int, default=4,
                        help="number of datasets to run concurrently (default: 4)")
    parser.add_argument("--dataset", action="append", dest="datasets",
                        help="run only this dataset directory; may be repeated")
    parser.add_argument("--cc-ett-max-calls", type=int, default=0,
                        help="stop Dyn CC work after N ETT queries (0: normal unlimited policy)")
    parser.add_argument("--limit", type=int, help="run only the first N selected cases")
    parser.add_argument("--dry-run", action="store_true", help="only list selected inputs")
    args = parser.parse_args()
    if args.workers < 1:
        parser.error("--workers must be at least 1")
    if args.cc_ett_max_calls < 0:
        parser.error("--cc-ett-max-calls must be non-negative")

    if args.case_file:
        try:
            selected = instances_from_case_file(args.case_file, args.report)
        except (OSError, ValueError, zipfile.BadZipFile) as error:
            parser.error(str(error))
        output = args.output or (
            ROOT / "results" / f"cc_cpu_experiment_{args.case_file.stem}.csv"
        )
    else:
        selected = eligible_instances(args.report)
        output = args.output or DEFAULT_OUTPUT
    if args.limit is not None:
        selected = selected[:args.limit]
    inputs = input_index(args.input_dirs or DEFAULT_INPUT_DIRS)

    missing = [item["instance"] for item in selected if item["instance"] not in inputs]
    if missing:
        print(f"warning: {len(missing)} selected instances have no input file", file=sys.stderr)

    runnable = [item for item in selected if item["instance"] in inputs]
    grouped = {folder.name: [] for folder in (args.input_dirs or DEFAULT_INPUT_DIRS)}
    for item in runnable:
        path = inputs[item["instance"]]
        grouped.setdefault(path.parent.name, []).append((item, path))
    requested = set(args.datasets or grouped)
    unknown = requested - set(grouped)
    if unknown:
        parser.error("unknown --dataset value(s): " + ", ".join(sorted(unknown)))
    grouped = {name: entries for name, entries in grouped.items()
               if name in requested and entries}
    source = "case file" if args.case_file else "report"
    print(f"{source} selected {len(selected)} cases; {len(runnable)} input files found")
    if args.dry_run:
        for dataset, entries in grouped.items():
            print(f"[{dataset}] {len(entries)} cases")
            for _, path in entries:
                print(path)
        return 0
    if not args.executable.is_file():
        parser.error(f"executable not found: {args.executable}; build the project first")
    if not grouped:
        write_results(output, [])
        print(f"wrote 0 rows to {output}")
        return 0

    results = []
    with ThreadPoolExecutor(max_workers=min(args.workers, len(grouped))) as executor:
        futures = {
            executor.submit(run_dataset, dataset, entries, args): dataset
            for dataset, entries in grouped.items()
        }
        for future in as_completed(futures):
            results.extend(future.result())

    results.sort(key=lambda row: row["instance"])
    write_results(output, results)
    print(f"wrote {len(results)} rows to {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
