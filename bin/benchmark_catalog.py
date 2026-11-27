#!/usr/bin/env python3
"""Build and validate the benchmark manifests used by all experiments."""

from __future__ import annotations

import argparse
import csv
import json
import re
import sys
import zipfile
from collections import Counter
from dataclasses import asdict, dataclass
from pathlib import Path
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[1]
BENCHMARK_ROOT = ROOT / "data" / "benchmarks"
REPORT_PATH = BENCHMARK_ROOT / "Final_Experiment_Report.xlsx"
MANIFEST_ROOT = BENCHMARK_ROOT / "manifests"
NEW_ROME_ROOT = BENCHMARK_ROOT / "rome3_350"

NS = {"x": "http://schemas.openxmlformats.org/spreadsheetml/2006/main"}
EXPECTED_COUNTS = {
    "old_dominoes": 10,
    "old_exact_cover": 217,
    "old_set_partition": 52,
    "old_topology": 87,
    "old": 366,
    "new_rome3": 350,
    "all": 716,
}


@dataclass(frozen=True)
class BenchmarkEntry:
    dataset: str
    category: str
    instance: str
    path: str
    source: str


def column_index(cell_ref: str) -> int:
    letters = re.match(r"[A-Z]+", cell_ref)
    if letters is None:
        raise ValueError(f"invalid XLSX cell reference: {cell_ref}")
    result = 0
    for char in letters.group(0):
        result = result * 26 + ord(char) - ord("A") + 1
    return result - 1


def read_report_instances(path: Path) -> list[str]:
    """Read instance names from the first worksheet without extra dependencies."""
    with zipfile.ZipFile(path) as archive:
        shared_strings: list[str] = []
        if "xl/sharedStrings.xml" in archive.namelist():
            root = ET.fromstring(archive.read("xl/sharedStrings.xml"))
            for item in root.findall("x:si", NS):
                shared_strings.append(
                    "".join(node.text or "" for node in item.iterfind(".//x:t", NS))
                )

        sheet = ET.fromstring(archive.read("xl/worksheets/sheet1.xml"))
        rows: list[list[str]] = []
        for row in sheet.findall(".//x:sheetData/x:row", NS):
            values: dict[int, str] = {}
            for cell in row.findall("x:c", NS):
                value = cell.find("x:v", NS)
                text = "" if value is None else value.text or ""
                if cell.get("t") == "s" and text:
                    text = shared_strings[int(text)]
                values[column_index(cell.get("r", ""))] = text
            width = max(values, default=-1) + 1
            rows.append([values.get(index, "") for index in range(width)])

    if not rows or not rows[0] or rows[0][0] != "Instance":
        raise ValueError(f"{path}: first worksheet must start with an Instance column")
    return [row[0].strip() for row in rows[1:] if row and row[0].strip()]


def files_by_stem(folder: Path, pattern: str = "*") -> dict[str, Path]:
    result: dict[str, Path] = {}
    for path in sorted(folder.glob(pattern)):
        if not path.is_file():
            continue
        stem = path.stem
        if stem in result:
            raise ValueError(f"duplicate instance stem in {folder}: {stem}")
        result[stem] = path
    return result


def relative_path(path: Path) -> str:
    return str(path.resolve().relative_to(ROOT.resolve()))


def make_entry(dataset: str, category: str, path: Path, source: str) -> BenchmarkEntry:
    return BenchmarkEntry(
        dataset=dataset,
        category=category,
        instance=path.stem,
        path=relative_path(path),
        source=source,
    )


def build_catalog() -> tuple[list[BenchmarkEntry], set[str], set[str]]:
    report_names = read_report_instances(REPORT_PATH)
    report_set = set(report_names)
    if len(report_set) != len(report_names):
        duplicates = sorted(
            name for name, count in Counter(report_names).items() if count > 1
        )
        raise ValueError(f"duplicate instances in report: {', '.join(duplicates)}")

    folder_specs = {
        "old_dominoes": ("old", "dominoes", BENCHMARK_ROOT / "dominoes_set"),
        "old_exact_cover": (
            "old",
            "exact_cover",
            BENCHMARK_ROOT / "exact_cover_set",
        ),
        "old_set_partition": (
            "old",
            "set_partition",
            BENCHMARK_ROOT / "set_partition_set",
        ),
        "old_topology": ("old", "topology", BENCHMARK_ROOT / "graphs_set"),
    }

    groups: dict[str, list[BenchmarkEntry]] = {}
    for group, (dataset, category, folder) in folder_specs.items():
        if not folder.is_dir():
            raise FileNotFoundError(f"benchmark directory not found: {folder}")
        index = files_by_stem(folder)
        if group == "old_dominoes":
            selected = sorted(index)
        elif group == "old_topology":
            selected = sorted(
                name
                for name in index
                if name in report_set and not name.startswith("grafo")
            )
        else:
            selected = sorted(name for name in index if name in report_set)
        groups[group] = [
            make_entry(dataset, category, index[name], relative_path(REPORT_PATH))
            for name in selected
        ]

    if not NEW_ROME_ROOT.is_dir():
        raise FileNotFoundError(f"new ROME matrix directory not found: {NEW_ROME_ROOT}")
    new_index = files_by_stem(NEW_ROME_ROOT, "grafo*.txt")
    groups["new_rome3"] = [
        make_entry(
            "new",
            "rome3",
            new_index[name],
            relative_path(NEW_ROME_ROOT),
        )
        for name in sorted(new_index)
    ]

    old_entries = sorted(
        [entry for group in folder_specs for entry in groups[group]],
        key=lambda entry: (entry.category, entry.instance),
    )
    new_entries = groups["new_rome3"]
    all_entries = sorted(
        old_entries + new_entries,
        key=lambda entry: (entry.dataset, entry.category, entry.instance),
    )

    for group, expected in EXPECTED_COUNTS.items():
        actual = {
            "old": len(old_entries),
            "new_rome3": len(new_entries),
            "all": len(all_entries),
            **{name: len(entries) for name, entries in groups.items()},
        }[group]
        if actual != expected:
            raise ValueError(f"{group}: expected {expected} entries, found {actual}")

    old_names = {entry.instance for entry in old_entries}
    new_names = {entry.instance for entry in new_entries}
    overlap = sorted(old_names & new_names)
    if overlap:
        raise ValueError(f"old/new benchmark overlap: {', '.join(overlap[:20])}")

    groups["old"] = old_entries
    groups["all"] = all_entries
    for group, entries in groups.items():
        counts = Counter(entry.instance for entry in entries)
        duplicate_names = sorted(name for name, count in counts.items() if count > 1)
        if duplicate_names:
            raise ValueError(
                f"{group}: duplicate instance names: {', '.join(duplicate_names[:20])}"
            )
        missing = [entry.path for entry in entries if not (ROOT / entry.path).is_file()]
        if missing:
            raise FileNotFoundError(f"{group}: missing input files: {missing[:5]}")

    return all_entries, old_names, new_names


def write_manifests(all_entries: list[BenchmarkEntry], groups: dict[str, list[BenchmarkEntry]]) -> None:
    MANIFEST_ROOT.mkdir(parents=True, exist_ok=True)
    for group, entries in groups.items():
        path = MANIFEST_ROOT / f"{group}.txt"
        path.write_text(
            "".join(f"{entry.instance}\n" for entry in entries),
            encoding="utf-8",
        )

    csv_path = BENCHMARK_ROOT / "benchmark_manifest.csv"
    with csv_path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(
            stream,
            fieldnames=("dataset", "category", "instance", "path", "source"),
        )
        writer.writeheader()
        for entry in all_entries:
            writer.writerow(asdict(entry))

    metadata = {
        "report": relative_path(REPORT_PATH),
        "new_rome_root": relative_path(NEW_ROME_ROOT),
        "counts": {
            group: len(entries) for group, entries in groups.items()
        },
        "rules": {
            "old_exact_cover": "exact_cover_set files listed in the report",
            "old_set_partition": "set_partition_set files listed in the report",
            "old_topology": "graphs_set files listed in the report, excluding grafo*",
            "new_rome3": "grafo*.txt files in rome3_350",
        },
    }
    (BENCHMARK_ROOT / "benchmark_manifest.json").write_text(
        json.dumps(metadata, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--write",
        action="store_true",
        help="write manifest text/CSV/JSON files after validation",
    )
    parser.add_argument(
        "--list",
        choices=("old", "new_rome3", "all"),
        help="print one validated group instead of a summary",
    )
    args = parser.parse_args()

    try:
        all_entries, old_names, new_names = build_catalog()
    except (OSError, ValueError, KeyError, zipfile.BadZipFile) as error:
        print(f"[error] {error}", file=sys.stderr)
        return 1

    categories = Counter((entry.dataset, entry.category) for entry in all_entries)
    print(f"old: {len(old_names)}")
    for (dataset, category), count in sorted(categories.items()):
        print(f"  {dataset}/{category}: {count}")
    print(f"new ROME: {len(new_names)}")
    print(f"all current: {len(all_entries)}")
    print("old/new overlap: 0")

    if args.write:
        groups: dict[str, list[BenchmarkEntry]] = {}
        for entry in all_entries:
            groups.setdefault(entry.dataset, []).append(entry)
        for category in sorted({entry.category for entry in all_entries}):
            if not category.startswith("rome3"):
                group = {
                    "dominoes": "old_dominoes",
                    "exact_cover": "old_exact_cover",
                    "set_partition": "old_set_partition",
                    "topology": "old_topology",
                }[category]
                groups[group] = [
                    entry for entry in groups["old"] if entry.category == category
                ]
        groups["old_all"] = groups["old"]
        del groups["old"]
        groups["new_rome3"] = groups["new"]
        del groups["new"]
        groups["all_current"] = all_entries
        write_manifests(all_entries, groups)
        print(f"manifests: {MANIFEST_ROOT}")

    if args.list:
        names = {
            "old": old_names,
            "new_rome3": new_names,
            "all": old_names | new_names,
        }[args.list]
        for name in sorted(names):
            print(name)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
