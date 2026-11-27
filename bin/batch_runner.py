#!/usr/bin/env python3
"""Run exact-cover, DXD-variant, and repeatable thread experiments."""

from __future__ import annotations

import argparse
import csv
import json
import os
import re
import shutil
import signal
import statistics
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MAIN_EXECUTABLE = ROOT / "bin" / "main"
MANIFEST_PATH = ROOT / "data" / "benchmarks" / "benchmark_manifest.csv"
DEFAULT_RESULTS_ROOT = ROOT / "results" / "runs"

TIME_RE = re.compile(r"^Time:\s*([\d.eE+-]+)\s*s\s*$", re.MULTILINE)
SOL_RE = re.compile(r"^Solutions:\s*(\S+)\s*$", re.MULTILINE)
SOL_SCI_RE = re.compile(r"^Solutions \(scientific\):\s*(\S+)\s*$", re.MULTILINE)
BLOCKS_RE = re.compile(r"^Max Blocks:\s*(\d+)\s*$", re.MULTILINE)
ROWS_RE = re.compile(r"^rows:\s*(\d+)\s*$", re.MULTILINE)
COLS_RE = re.compile(r"^cols:\s*(\d+)\s*$", re.MULTILINE)
DNNF_RE = re.compile(r"^DNNF Size:\s*(\d+)\s*$", re.MULTILINE)
DXZ_CACHE_READS_RE = re.compile(
    r"^dxz cache_reads\s*:\s*([\d.eE+-]+)\s*$", re.MULTILINE
)
DXZ_CACHE_HITS_RE = re.compile(
    r"^dxz cache_hits\s*:\s*([\d.eE+-]+)\s*$", re.MULTILINE
)
DXZ_CACHE_HIT_RATE_RE = re.compile(
    r"^dxz cache_hit_rate\s*:\s*([\d.eE+-]+)\s*$", re.MULTILINE
)
INTERNAL_TIMEOUT_MARKERS = ("搜索超时", "Time bound", "timeout")
MEMORY_MARKERS = ("bad_alloc", "Memory", "out of memory", "内存")

FIELDS = (
    "run_id",
    "suite",
    "dataset",
    "category",
    "instance",
    "input",
    "solver",
    "variant",
    "threads",
    "repeat",
    "status",
    "time_s",
    "wall_time_s",
    "solutions",
    "solutions_match",
    "max_blocks",
    "dnnf_size",
    "cache_reads",
    "cache_hits",
    "cache_hit_rate",
    "rows",
    "cols",
    "exit_code",
    "error",
    "started_at",
)


@dataclass(frozen=True)
class Benchmark:
    dataset: str
    category: str
    instance: str
    path: Path


@dataclass(frozen=True)
class Variant:
    solver: str
    variant: str
    algorithm: str
    threads: int | None
    extra_args: tuple[str, ...] = ()
    performance_mode: bool = False


def read_manifest() -> list[Benchmark]:
    if not MANIFEST_PATH.is_file():
        raise FileNotFoundError(
            f"missing {MANIFEST_PATH}; run bin/benchmark_catalog.py --write"
        )
    entries: list[Benchmark] = []
    with MANIFEST_PATH.open(encoding="utf-8", newline="") as stream:
        for row in csv.DictReader(stream):
            entries.append(
                Benchmark(
                    dataset=row["dataset"],
                    category=row["category"],
                    instance=row["instance"],
                    path=(ROOT / row["path"]).resolve(),
                )
            )
    return entries


def read_case_names(path: Path) -> set[str]:
    names = {
        name.strip()
        for line in path.read_text(encoding="utf-8").splitlines()
        for name in re.split(r"[,\s]+", line)
        if name.strip()
    }
    if not names:
        raise ValueError(f"case list is empty: {path}")
    return names


def select_benchmarks(
    entries: list[Benchmark],
    benchmark_set: str,
    case_list: Path | None,
) -> list[Benchmark]:
    selected = entries if benchmark_set == "all" else [
        entry for entry in entries if entry.dataset == benchmark_set
    ]
    if case_list is not None:
        names = read_case_names(case_list)
        selected = [entry for entry in selected if entry.instance in names]
        found = {entry.instance for entry in selected}
        missing = sorted(names - found)
        if missing:
            raise ValueError(
                "case list contains names outside the selected benchmark set: "
                + ", ".join(missing[:20])
            )
    if not selected:
        raise ValueError("no benchmarks selected")
    selected_names = [entry.instance for entry in selected]
    if len(selected_names) != len(set(selected_names)):
        raise ValueError("selected benchmarks contain duplicate instance names")
    return sorted(selected, key=lambda entry: (entry.category, entry.instance))


def variants_for(suite: str, dxd_group: str = "all") -> list[Variant]:
    if suite == "exact":
        return [
            Variant("DLX", "DLX", "dlx", None),
            Variant("DXZ", "DXZ", "dxz", None),
        ]
    if suite == "dxd-versions":
        thread_groups = {
            "single-thread": (1,),
            "multi-thread": (8,),
            "all": (1, 8),
        }
        variants = []
        for threads in thread_groups[dxd_group]:
            variants.extend(
                [
                    Variant("DXD", "DXD", "dxd", threads, performance_mode=True),
                    Variant(
                        "DynDXD",
                        "pureDynDXD",
                        "ddxd",
                        threads,
                        ("--pure-dyndxd", "--cc-ett-max-calls", "0",
                         "--cc-bfs-max-calls", "0"),
                        True,
                    ),
                    Variant(
                        "HybridDXD",
                        "HybridDXD",
                        "hybrid",
                        threads,
                        performance_mode=True,
                    ),
                ]
            )
        return variants
    if suite == "thread-compare":
        return [
            Variant(
                solver,
                solver,
                algorithm,
                threads,
                performance_mode=True,
            )
            for solver, algorithm in (
                ("DXD", "dxd"),
                ("HybridDXD", "hybrid"),
            )
            for threads in (1, 2, 4, 8)
        ]
    raise ValueError(f"unknown suite: {suite}")


def parse_output(output: str) -> dict[str, object]:
    result: dict[str, object] = {
        "time_s": "",
        "solutions": "",
        "max_blocks": "",
        "dnnf_size": "",
        "cache_reads": "",
        "cache_hits": "",
        "cache_hit_rate": "",
        "rows": "",
        "cols": "",
    }
    matches = TIME_RE.findall(output)
    if matches:
        result["time_s"] = matches[-1]
    matches = SOL_RE.findall(output)
    if matches:
        result["solutions"] = matches[-1]
    else:
        matches = SOL_SCI_RE.findall(output)
        if matches:
            result["solutions"] = matches[-1]
    for field, pattern in (
        ("max_blocks", BLOCKS_RE),
        ("dnnf_size", DNNF_RE),
        ("cache_reads", DXZ_CACHE_READS_RE),
        ("cache_hits", DXZ_CACHE_HITS_RE),
        ("cache_hit_rate", DXZ_CACHE_HIT_RATE_RE),
        ("rows", ROWS_RE),
        ("cols", COLS_RE),
    ):
        matches = pattern.findall(output)
        if matches:
            result[field] = matches[-1]
    return result


def run_variant(
    benchmark: Benchmark,
    variant: Variant,
    timeout: int,
    memory_gb: int,
) -> tuple[dict[str, object], str]:
    command = [
        str(MAIN_EXECUTABLE),
        "-a",
        variant.algorithm,
        "-i",
        str(benchmark.path),
    ]
    if variant.threads is not None:
        command.extend(["-t", str(variant.threads)])
    command.extend(["--time-limit", str(timeout)])
    if variant.performance_mode:
        command.append("--skip-dnnf-size")
    command.extend(variant.extra_args)
    if memory_gb > 0:
        prlimit = shutil.which("prlimit")
        if prlimit is None:
            raise RuntimeError("prlimit is required when --memory-gb is positive")
        command = [
            prlimit,
            f"--as={memory_gb * 1024 * 1024 * 1024}",
            "--",
            *command,
        ]

    started_wall = time.perf_counter()
    started_at = datetime.now().isoformat(timespec="seconds")
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

    wall_time = time.perf_counter() - started_wall
    parsed = parse_output(output)
    lower_output = output.lower()
    if exit_code is None or any(marker.lower() in lower_output for marker in INTERNAL_TIMEOUT_MARKERS):
        status = "timeout"
    elif any(marker.lower() in lower_output for marker in MEMORY_MARKERS):
        status = "memout"
    elif exit_code != 0:
        status = "error"
    elif parsed["time_s"] and parsed["solutions"]:
        status = "success"
    else:
        status = "incomplete"

    row: dict[str, object] = {
        "dataset": benchmark.dataset,
        "category": benchmark.category,
        "instance": benchmark.instance,
        "input": str(benchmark.path.relative_to(ROOT)),
        "solver": variant.solver,
        "variant": variant.variant,
        "threads": variant.threads if variant.threads is not None else "",
        "status": status,
        "wall_time_s": f"{wall_time:.6f}",
        "exit_code": "" if exit_code is None else exit_code,
        "stdout_tail": output[-4000:],
        "started_at": started_at,
        **parsed,
    }
    if status != "success" and not row.get("error"):
        row["error"] = (
            f"exit={exit_code}; "
            f"tail={' '.join(output.strip().splitlines()[-3:])[:800]}"
        )
    return row, output


def variant_result_path(output_dir: Path, variant: Variant) -> Path:
    return output_dir / f"runs_{variant.variant}.csv"


def run_variant_benchmarks(
    benchmarks: list[Benchmark],
    variant: Variant,
    output_dir: Path,
    run_id: str,
    suite: str,
    rounds: int,
    timeout: int,
    memory_gb: int,
    resume: bool,
    save_logs: bool,
    raw_path: Path,
    truncate_file: bool,
) -> Path:
    jobs_per_variant = len(benchmarks) * rounds
    if resume and raw_path.is_file():
        completed = read_existing_keys(raw_path)
        file_mode = "a"
    elif truncate_file or not raw_path.is_file():
        completed = set()
        file_mode = "w"
    else:
        completed = set()
        file_mode = "a"

    fieldnames = (*FIELDS, "stdout_tail")
    with raw_path.open(file_mode, encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(
            stream,
            fieldnames=fieldnames,
            extrasaction="ignore",
        )
        if file_mode == "w":
            writer.writeheader()
        stream.flush()

        job_index = 0
        for benchmark in benchmarks:
            for repeat in range(1, rounds + 1):
                job_index += 1
                key = (
                    benchmark.instance,
                    variant.variant,
                    str(variant.threads or ""),
                    str(repeat),
                )
                if key in completed:
                    continue
                print(
                    f"[{variant.variant} T{variant.threads or '-'} "
                    f"{job_index}/{jobs_per_variant}] {benchmark.instance} "
                    f"repeat={repeat}",
                    flush=True,
                )
                row, output = run_variant(
                    benchmark,
                    variant,
                    timeout,
                    memory_gb,
                )
                row.update(
                    {
                        "run_id": run_id,
                        "suite": suite,
                        "repeat": repeat,
                    }
                )
                if save_logs:
                    log_path = output_dir / "logs" / (
                        f"{benchmark.instance}.{variant.variant}."
                        f"t{variant.threads or '-'}.r{repeat}.log"
                    )
                    log_path.write_text(output, encoding="utf-8")
                writer.writerow(row)
                stream.flush()
    return raw_path


def finalize_result_files(raw_paths: list[Path], summary_path: Path) -> None:
    rows_by_path: dict[Path, list[dict[str, object]]] = {}
    all_rows: list[dict[str, object]] = []
    for raw_path in raw_paths:
        with raw_path.open(encoding="utf-8", newline="") as stream:
            rows = list(csv.DictReader(stream))
        rows_by_path[raw_path] = rows
        all_rows.extend(rows)

    apply_solution_checks(all_rows)
    fieldnames = (*FIELDS, "stdout_tail")
    for raw_path, rows in rows_by_path.items():
        with raw_path.open("w", encoding="utf-8", newline="") as stream:
            writer = csv.DictWriter(
                stream,
                fieldnames=fieldnames,
                extrasaction="ignore",
            )
            writer.writeheader()
            writer.writerows(rows)
    write_summary(raw_paths, summary_path)


def write_combined_results(raw_paths: list[Path], output_path: Path) -> None:
    fieldnames = (*FIELDS, "stdout_tail")
    with output_path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(
            stream,
            fieldnames=fieldnames,
            extrasaction="ignore",
        )
        writer.writeheader()
        for raw_path in raw_paths:
            with raw_path.open(encoding="utf-8", newline="") as raw_stream:
                writer.writerows(csv.DictReader(raw_stream))


def migrate_legacy_exact_results(
    output_dir: Path,
    variants: list[Variant],
) -> None:
    legacy_path = output_dir / "runs.csv"
    if not legacy_path.is_file():
        return

    fieldnames = (*FIELDS, "stdout_tail")
    with legacy_path.open(encoding="utf-8", newline="") as stream:
        legacy_rows = list(csv.DictReader(stream))
    for variant in variants:
        target_path = variant_result_path(output_dir, variant)
        existing_rows: list[dict[str, str]] = []
        if target_path.is_file():
            with target_path.open(encoding="utf-8", newline="") as stream:
                existing_rows = list(csv.DictReader(stream))
        existing_keys = {row_key(row) for row in existing_rows}
        matching_rows: list[dict[str, str]] = []
        for row in legacy_rows:
            if (
                row.get("solver") != variant.solver
                or row.get("variant") != variant.variant
            ):
                continue
            key = row_key(row)
            if key in existing_keys:
                continue
            existing_keys.add(key)
            matching_rows.append(row)
        if not matching_rows:
            continue
        with target_path.open("w", encoding="utf-8", newline="") as stream:
            writer = csv.DictWriter(
                stream,
                fieldnames=fieldnames,
                extrasaction="ignore",
            )
            writer.writeheader()
            writer.writerows([*existing_rows, *matching_rows])


def run_parallel_variants(
    benchmarks: list[Benchmark],
    variants: list[Variant],
    output_dir: Path,
    run_id: str,
    suite: str,
    rounds: int,
    timeout: int,
    memory_gb: int,
    resume: bool,
    save_logs: bool,
) -> list[Path]:
    raw_paths = [
        variant_result_path(output_dir, variant) for variant in variants
    ]
    with ThreadPoolExecutor(max_workers=len(variants)) as executor:
        futures = {
            executor.submit(
                run_variant_benchmarks,
                benchmarks,
                variant,
                output_dir,
                run_id,
                suite,
                rounds,
                timeout,
                memory_gb,
                resume,
                save_logs,
                variant_result_path(output_dir, variant),
                True,
            ): variant
            for variant in variants
        }
        failures: list[BaseException] = []
        for future in as_completed(futures):
            variant = futures[future]
            try:
                future.result()
            except BaseException as error:
                failures.append(error)
                print(
                    f"[error] {variant.variant} "
                    f"T{variant.threads or '-'} worker failed: {error}",
                    file=sys.stderr,
                    flush=True,
                )
    if failures:
        raise RuntimeError(
            f"{len(failures)} parallel worker(s) failed"
        ) from failures[0]
    return raw_paths


def run_exact(
    benchmarks: list[Benchmark],
    variants: list[Variant],
    output_dir: Path,
    run_id: str,
    args: argparse.Namespace,
) -> list[Path]:
    if args.resume:
        migrate_legacy_exact_results(output_dir, variants)
    return run_parallel_variants(
        benchmarks,
        variants,
        output_dir,
        run_id,
        args.suite,
        args.rounds,
        args.timeout,
        args.memory_gb,
        args.resume,
        args.save_logs,
    )


def run_dxd_versions(
    benchmarks: list[Benchmark],
    variants: list[Variant],
    output_dir: Path,
    run_id: str,
    args: argparse.Namespace,
) -> list[Path]:
    thread_1_variants = [variant for variant in variants if variant.threads == 1]
    thread_8_variants = [variant for variant in variants if variant.threads == 8]

    if thread_1_variants:
        run_parallel_variants(
            benchmarks,
            thread_1_variants,
            output_dir,
            run_id,
            args.suite,
            args.rounds,
            args.timeout,
            args.memory_gb,
            args.resume,
            args.save_logs,
        )

    truncate_thread_8_files = not thread_1_variants
    for variant in thread_8_variants:
        run_variant_benchmarks(
            benchmarks,
            variant,
            output_dir,
            run_id,
            args.suite,
            args.rounds,
            args.timeout,
            args.memory_gb,
            args.resume,
            args.save_logs,
            variant_result_path(output_dir, variant),
            truncate_thread_8_files,
        )

    return [variant_result_path(output_dir, variant) for variant in variants]


def row_key(row: dict[str, object]) -> tuple[str, str, str, str]:
    return (
        str(row["instance"]),
        str(row["variant"]),
        str(row["threads"]),
        str(row["repeat"]),
    )


def read_existing_keys(path: Path) -> set[tuple[str, str, str, str]]:
    if not path.is_file():
        return set()
    with path.open(encoding="utf-8", newline="") as stream:
        return {row_key(row) for row in csv.DictReader(stream)}


def apply_solution_checks(rows: list[dict[str, object]]) -> None:
    by_instance: dict[str, list[dict[str, object]]] = {}
    for row in rows:
        by_instance.setdefault(str(row["instance"]), []).append(row)
    for group in by_instance.values():
        successful = [
            row
            for row in group
            if row.get("status") == "success" and row.get("solutions")
        ]
        if not successful:
            for row in group:
                row["solutions_match"] = ""
            continue
        reference = next(
            (
                row
                for row in successful
                if row.get("variant") == "DXD" and str(row.get("threads")) == "1"
            ),
            successful[0],
        )
        reference_count = str(reference["solutions"])
        for row in group:
            if row.get("status") != "success" or not row.get("solutions"):
                row["solutions_match"] = ""
                continue
            matches = str(row["solutions"]) == reference_count
            row["solutions_match"] = str(matches)
            if not matches:
                row["status"] = "solution_mismatch"
                row["error"] = (
                    f"reference solutions={reference_count}; "
                    f"observed={row['solutions']}"
                )


def write_summary(raw_paths: list[Path], summary_path: Path) -> None:
    grouped: dict[tuple[str, str, str], list[dict[str, str]]] = {}
    for raw_path in raw_paths:
        with raw_path.open(encoding="utf-8", newline="") as stream:
            for row in csv.DictReader(stream):
                grouped.setdefault(
                    (row["solver"], row["variant"], row["threads"]), []
                ).append(row)

    with summary_path.open("w", encoding="utf-8", newline="") as stream:
        fields = (
            "solver",
            "variant",
            "threads",
            "runs",
            "success",
            "timeout",
            "errors",
            "min_time_s",
            "median_time_s",
            "max_time_s",
        )
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        for (solver, variant, threads), rows in sorted(grouped.items()):
            times = [
                float(row["time_s"])
                for row in rows
                if row["status"] == "success" and row["time_s"]
            ]
            writer.writerow(
                {
                    "solver": solver,
                    "variant": variant,
                    "threads": threads,
                    "runs": len(rows),
                    "success": sum(row["status"] == "success" for row in rows),
                    "timeout": sum(row["status"] == "timeout" for row in rows),
                    "errors": sum(
                        row["status"] not in ("success", "timeout") for row in rows
                    ),
                    "min_time_s": f"{min(times):.6f}" if times else "",
                    "median_time_s": (
                        f"{statistics.median(times):.6f}" if times else ""
                    ),
                    "max_time_s": f"{max(times):.6f}" if times else "",
                }
            )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--suite",
        required=True,
        choices=("exact", "dxd-versions", "thread-compare"),
    )
    parser.add_argument(
        "--benchmark-set",
        choices=("old", "new", "all"),
        default="all",
    )
    parser.add_argument(
        "--group",
        choices=("single-thread", "multi-thread", "all"),
        default="all",
        help=(
            "DXD thread group; all runs the concurrent single-thread group "
            "first, then the serial multi-thread (T8) group"
        ),
    )
    parser.add_argument("--case-list", type=Path)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--timeout", type=int, default=1500)
    parser.add_argument(
        "--rounds",
        type=int,
        help="repeat count; defaults to 10 for thread-compare and 1 otherwise",
    )
    parser.add_argument("--memory-gb", type=int, default=12)
    parser.add_argument("--limit", type=int, default=0)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument(
        "--resume",
        action="store_true",
        help="resume using --output-dir and skip completed run keys",
    )
    parser.add_argument(
        "--save-logs",
        action="store_true",
        help="store every solver stdout under the run directory",
    )
    parser.add_argument(
        "--dxz-stats",
        action="store_true",
        help=(
            "enable DXZ cache statistics for the exact suite and record "
            "cache_reads/cache_hits/cache_hit_rate"
        ),
    )
    args = parser.parse_args()

    if args.timeout < 1:
        parser.error("--timeout must be >= 1")
    if args.memory_gb < 0:
        parser.error("--memory-gb must be >= 0")
    if args.rounds is None:
        args.rounds = 10 if args.suite == "thread-compare" else 1
    if args.rounds < 1:
        parser.error("--rounds must be >= 1")
    if args.suite == "exact" and args.benchmark_set == "old":
        parser.error("the exact suite is intentionally limited to the new ROME set")
    if args.group != "all" and args.suite != "dxd-versions":
        parser.error("--group single-thread/multi-thread requires --suite dxd-versions")
    if args.dxz_stats and args.suite != "exact":
        parser.error("--dxz-stats requires --suite exact")
    if not MAIN_EXECUTABLE.is_file():
        parser.error(f"solver executable not found: {MAIN_EXECUTABLE}")

    try:
        benchmarks = select_benchmarks(
            read_manifest(), args.benchmark_set, args.case_list
        )
    except (OSError, ValueError) as error:
        parser.error(str(error))
    if args.suite == "exact":
        benchmarks = [entry for entry in benchmarks if entry.dataset == "new"]
        if not benchmarks:
            parser.error("the exact suite requires at least one new ROME instance")
    if args.limit:
        benchmarks = benchmarks[: args.limit]
    variants = variants_for(args.suite, args.group)
    if args.dxz_stats:
        variants = [
            Variant(
                variant.solver,
                variant.variant,
                variant.algorithm,
                variant.threads,
                (*variant.extra_args, "--enable-cc-stats"),
                variant.performance_mode,
            )
            if variant.algorithm == "dxz"
            else variant
            for variant in variants
        ]

    run_id = datetime.now().strftime("%Y%m%d_%H%M%S")
    if args.output_dir is None:
        output_dir = DEFAULT_RESULTS_ROOT / (
            f"{run_id}_{args.suite}_{args.benchmark_set}"
        )
    else:
        output_dir = args.output_dir.resolve()
    summary_path = output_dir / "summary.csv"
    log_dir = output_dir / "logs"

    planned = len(benchmarks) * len(variants) * args.rounds
    print(
        f"suite={args.suite} benchmarks={len(benchmarks)} "
        f"variants={len(variants)} rounds={args.rounds} planned_runs={planned}"
    )
    if args.suite == "dxd-versions":
        schedules = []
        if args.group in ("single-thread", "all"):
            schedules.append("threads_1_three_workers_concurrent")
        if args.group in ("multi-thread", "all"):
            schedules.append("threads_8_three_workers_serial")
        print(f"schedule={';'.join(schedules)}")
    elif args.suite == "exact":
        print(
            "schedule=DLX_DXZ_two_workers_concurrent;"
            "outputs=runs_DLX.csv,runs_DXZ.csv"
        )
    if args.dry_run:
        for benchmark in benchmarks:
            print(
                f"{benchmark.dataset}/{benchmark.category}/"
                f"{benchmark.instance} -> {benchmark.path}"
            )
        for variant in variants:
            print(
                f"variant={variant.variant} solver={variant.solver} "
                f"threads={variant.threads or '-'}"
            )
        return 0
    if args.resume and not args.output_dir:
        parser.error("--resume requires --output-dir")

    output_dir.mkdir(parents=True, exist_ok=True)
    if args.save_logs:
        log_dir.mkdir(parents=True, exist_ok=True)

    if args.suite == "exact":
        raw_paths = run_exact(
            benchmarks,
            variants,
            output_dir,
            run_id,
            args,
        )
    elif args.suite == "dxd-versions":
        raw_paths = run_dxd_versions(
            benchmarks,
            variants,
            output_dir,
            run_id,
            args,
        )
    else:
        raw_path = output_dir / "runs.csv"
        for index, variant in enumerate(variants):
            run_variant_benchmarks(
                benchmarks,
                variant,
                output_dir,
                run_id,
                args.suite,
                args.rounds,
                args.timeout,
                args.memory_gb,
                args.resume,
                args.save_logs,
                raw_path,
                index == 0,
            )
        raw_paths = [raw_path]

    finalize_result_files(raw_paths, summary_path)
    if args.suite == "exact":
        write_combined_results(raw_paths, output_dir / "runs.csv")
    config = {
        "run_id": run_id,
        "suite": args.suite,
        "benchmark_set": args.benchmark_set,
        "group": args.group,
        "benchmarks": len(benchmarks),
        "variants": [variant.__dict__ for variant in variants],
        "rounds": args.rounds,
        "timeout_s": args.timeout,
        "memory_gb": args.memory_gb,
        "dxz_stats": args.dxz_stats,
        "solver": str(MAIN_EXECUTABLE),
        "result_files": [str(path) for path in raw_paths],
    }
    if args.suite == "exact":
        config["schedule"] = {
            "execution": "DLX_and_DXZ_concurrent",
            "result_files": "one_csv_per_algorithm",
        }
    elif args.suite == "dxd-versions":
        config["schedule"] = {
            "selected_group": args.group,
            "threads_1": (
                "three_algorithm_workers_concurrent"
                if args.group in ("single-thread", "all")
                else "not_selected"
            ),
            "threads_8": (
                "three_algorithm_workers_serial"
                if args.group in ("multi-thread", "all")
                else "not_selected"
            ),
        }
    (output_dir / "config.json").write_text(
        json.dumps(config, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    for raw_path in raw_paths:
        print(f"raw results: {raw_path}")
    print(f"summary: {summary_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
