#!/usr/bin/env python3
"""Small correctness tests for runtime Hybrid, DXD BFS, and ETT lifecycle."""

from __future__ import annotations

import csv
import re
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
EXECUTABLE = ROOT / "bin" / "main"
SOLUTIONS = re.compile(
    r"^Solutions(?: \(scientific\))?:\s*(\S+)", re.MULTILINE
)
ETT_BUILDS = re.compile(r"^Hybrid ETT Build Calls:\s*(\d+)", re.MULTILINE)
DXD_BFS_CALLS = re.compile(r"^DXD BFS Calls:\s*(\d+)", re.MULTILINE)
HYBRID_BFS_CALLS = re.compile(r"^Hybrid BFS Calls:\s*(\d+)", re.MULTILINE)
HYBRID_ETT_CALLS = re.compile(r"^Hybrid ETT Calls:\s*(\d+)", re.MULTILINE)
HYBRID_DYN_QUERIES = re.compile(
    r"^Hybrid Dyn Query Calls:\s*(\d+)", re.MULTILINE
)
HYBRID_COMPONENT_QUERIES = re.compile(
    r"^Hybrid Component Query Calls:\s*(\d+)", re.MULTILINE
)
DYN_DECREMENTS = re.compile(
    r"^Hybrid Dyn Decrement Calls:\s*(\d+)", re.MULTILINE
)
DYN_INCREMENTS = re.compile(
    r"^Hybrid Dyn Increment Calls:\s*(\d+)", re.MULTILINE
)
MAX_BLOCKS = re.compile(r"^Max Blocks:\s*(\d+)", re.MULTILINE)
INITIAL_BFS_COMPONENTS = re.compile(
    r"^Hybrid Initial BFS Components:\s*(\d+)", re.MULTILINE
)
HYBRID_REPLACEMENT_SCANS = re.compile(
    r"^Hybrid Er Sum:\s*(\d+)", re.MULTILINE
)
HYBRID_TREE_CUTS = re.compile(
    r"^Hybrid Tree Edge Cuts:\s*(\d+)", re.MULTILINE
)
HYBRID_TREE_LINKS = re.compile(
    r"^Hybrid Tree Edge Links:\s*(\d+)", re.MULTILINE
)

CASES = (
    "data/benchmarks/graphs_set/AttMpls.txt",
    "data/benchmarks/graphs_set/Chinanet.txt",
    "data/benchmarks/dominoes_set/dominoes_8x8.txt",
    "test/parallel_components.ec",
    "test/dyn_path.ec",
    "data/benchmarks/set_partition_set/sppnw36.txt",
)


def run(algorithm: str, case: str, extra: list[str]) -> str:
    command = [
        str(EXECUTABLE),
        "-a",
        algorithm,
        "-i",
        str(ROOT / case),
        "-t",
        "1",
        "--time-limit",
        "60",
        "--skip-dnnf-size",
        *extra,
    ]
    completed = subprocess.run(
        command,
        cwd=ROOT,
        text=True,
        encoding="utf-8",
        errors="replace",
        capture_output=True,
        timeout=90,
    )
    output = completed.stdout + completed.stderr
    if completed.returncode != 0:
        raise AssertionError(
            f"{algorithm} failed on {case}:\n{output[-2000:]}"
        )
    match = SOLUTIONS.search(output)
    if not match:
        raise AssertionError(
            f"{algorithm} produced no solution count on {case}:\n"
            f"{output[-2000:]}"
        )
    return output


def main() -> int:
    for case in CASES:
        dxd_output = run("dxd", case, [])
        dxd_threshold_output = run(
            "dxd",
            case,
            ["--dxd-threshold", "--threshold-ratio", "0.5"],
        )
        with tempfile.TemporaryDirectory() as temp_dir:
            decision_log = Path(temp_dir) / "decisions.log"
            legacy_decision_log = Path(temp_dir) / "legacy_decisions.log"
            hybrid_output = run(
                "hybrid",
                case,
                [
                    "--hybrid-runtime-policy",
                    "--threshold-ratio",
                    "0.5",
                    "--enable-cc-stats",
                    "--hybrid-decision-log",
                    str(decision_log),
                ],
            )
            with decision_log.open(encoding="utf-8", newline="") as stream:
                decisions = list(csv.DictReader(stream))
            legacy_output = run(
                "hybrid",
                case,
                [
                    "--hybrid-engine",
                    "legacy",
                    "--threshold-ratio",
                    "0.5",
                    "--enable-cc-stats",
                    "--hybrid-decision-log",
                    str(legacy_decision_log),
                ],
            )
            with legacy_decision_log.open(
                encoding="utf-8", newline=""
            ) as stream:
                legacy_decisions = list(csv.DictReader(stream))
        counts = [
            SOLUTIONS.search(output).group(1)
            for output in (
                dxd_output,
                dxd_threshold_output,
                hybrid_output,
                legacy_output,
            )
        ]
        assert len(set(counts)) == 1, (case, counts)
        assert "Hybrid Runtime Policy: 1" in hybrid_output
        assert (
            "Hybrid Decomposition Strategy: "
            "per-block-incidence-bfs-dyn-cost"
        ) in hybrid_output, case
        build_match = ETT_BUILDS.search(hybrid_output)
        assert build_match is not None, case
        max_blocks_match = MAX_BLOCKS.search(hybrid_output)
        initial_bfs_match = INITIAL_BFS_COMPONENTS.search(hybrid_output)
        bfs_match = HYBRID_BFS_CALLS.search(hybrid_output)
        assert max_blocks_match is not None, case
        assert initial_bfs_match is not None, case
        assert bfs_match is not None, case
        assert int(max_blocks_match.group(1)) >= int(
            initial_bfs_match.group(1)
        ), case
        # The single-thread policy reuses the parent incidence-BFS snapshot
        # to seed every split block, so one parent BFS can serve many blocks.
        assert int(bfs_match.group(1)) >= 1, case
        cost_decisions = [
            decision for decision in decisions
            if decision["decision_kind"] == "cost"
        ]
        assert cost_decisions, case
        decrement_match = DYN_DECREMENTS.search(hybrid_output)
        assert decrement_match is not None, case
        if any(
            decision["choice"] == "Dyn"
            for decision in cost_decisions
        ):
            assert int(build_match.group(1)) >= 1, case
        for decision in decisions:
            assert decision["choice"] in {"Dyn", "BFS"}, case
            assert decision["operative_mode"] == decision["choice"], case
            if decision["decision_kind"] in {"locked_dyn", "final_dyn"}:
                assert decision["choice"] == "Dyn", case
                assert decision["dyn_locked"] == "1", case
                assert decision["C_dyn"] == "", case
                assert decision["C_bfs"] == "", case
                assert decision["Er"] != "", case
                continue
            assert decision["decision_kind"] == "cost", case
            if decision["dyn_locked"] == "1":
                assert decision["choice"] == "Dyn", case
            assert decision["C_dyn"], case
            assert decision["C_bfs"], case
            expected_dyn_cost = (
                int(decision["ETd"])
                * float(decision["log2_V_after"])
                + int(decision["ENd"])
                + int(decision["Vd"])
            )
            assert abs(
                float(decision["C_dyn"]) - expected_dyn_cost
            ) <= 1e-3 * max(expected_dyn_cost, 1.0), case
            assert float(decision["C_bfs"]) == (
                float(decision["V_after"])
                + float(decision["E_after"])
            ), case
        central_decisions = [
            {
                "output": hybrid_output,
                "rows": decisions,
            },
            {
                "output": legacy_output,
                "rows": legacy_decisions,
            },
        ]
        for variant in central_decisions:
            output = variant["output"]
            rows = variant["rows"]
            bfs_match = HYBRID_BFS_CALLS.search(output)
            ett_match = HYBRID_ETT_CALLS.search(output)
            component_match = HYBRID_COMPONENT_QUERIES.search(output)
            assert bfs_match is not None, case
            assert ett_match is not None, case
            assert component_match is not None, case
            assert int(component_match.group(1)) == (
                int(bfs_match.group(1)) + int(ett_match.group(1))
            ), case
            for decision in rows:
                assert decision["choice"] in {"Dyn", "BFS"}, case
                assert decision["Er"] != "", case
        if legacy_decisions:
            expected_legacy_dyn_cost = (
                int(legacy_decisions[0]["ETd"])
                * (
                    float(legacy_decisions[0]["log2_V_after"])
                    + 0.1 * int(legacy_decisions[0]["EN_after"])
                )
                + int(legacy_decisions[0]["Vd"])
            )
            assert abs(
                float(legacy_decisions[0]["C_dyn"])
                - expected_legacy_dyn_cost
            ) <= 1e-3 * max(expected_legacy_dyn_cost, 1.0), case
            for decision in legacy_decisions:
                assert decision["C_dyn"], case
                assert decision["C_bfs"], case
                assert decision["operative_mode"] == "Dyn", case
        bfs_match = HYBRID_BFS_CALLS.search(hybrid_output)
        ett_match = HYBRID_ETT_CALLS.search(hybrid_output)
        dyn_query_match = HYBRID_DYN_QUERIES.search(hybrid_output)
        assert dyn_query_match is not None, case
        if case == "test/dyn_path.ec":
            dyn_match = DYN_DECREMENTS.search(hybrid_output)
            inc_match = DYN_INCREMENTS.search(hybrid_output)
            assert int(initial_bfs_match.group(1)) == 1, case
            assert len(cost_decisions) >= 1, case
            assert cost_decisions[0]["choice"] == "Dyn", case
            assert dyn_match is not None and int(dyn_match.group(1)) >= 2, case
            assert inc_match is not None, case
            assert int(inc_match.group(1)) == 0, case
            assert int(build_match.group(1)) >= 1, case
            replacement_scans = HYBRID_REPLACEMENT_SCANS.search(
                hybrid_output
            )
            tree_cuts = HYBRID_TREE_CUTS.search(hybrid_output)
            tree_links = HYBRID_TREE_LINKS.search(hybrid_output)
            assert replacement_scans is not None, case
            assert tree_cuts is not None, case
            assert tree_links is not None, case
            assert int(replacement_scans.group(1)) != int(
                tree_cuts.group(1)
            ), case
            choices = {row["choice"] for row in legacy_decisions}
            assert choices == {"Dyn", "BFS"}, case
        no_threshold_calls = DXD_BFS_CALLS.search(dxd_output)
        threshold_calls = DXD_BFS_CALLS.search(dxd_threshold_output)
        if no_threshold_calls and threshold_calls:
            assert int(no_threshold_calls.group(1)) >= int(
                threshold_calls.group(1)
            ), case
        print(f"PASS {case} solutions={counts[0]}")

    switching_case = (
        "../GMLToMat/experiments/test/"
        "hybrid_range_20261120/case01.txt"
    )
    with tempfile.TemporaryDirectory() as temp_dir:
        decision_log = Path(temp_dir) / "switching_decisions.log"
        output = run(
            "hybrid",
            switching_case,
            [
                "--hybrid-runtime-policy",
                "--threshold-ratio",
                "0.75",
                "--enable-cc-stats",
                "--hybrid-decision-log",
                str(decision_log),
            ],
        )
        with decision_log.open(encoding="utf-8", newline="") as stream:
            decisions = list(csv.DictReader(stream))
    choices = {decision["choice"] for decision in decisions}
    assert choices == {"Dyn", "BFS"}, switching_case
    bfs_match = HYBRID_BFS_CALLS.search(output)
    dyn_match = HYBRID_DYN_QUERIES.search(output)
    component_match = HYBRID_COMPONENT_QUERIES.search(output)
    assert bfs_match is not None, switching_case
    assert dyn_match is not None, switching_case
    assert component_match is not None, switching_case
    assert int(component_match.group(1)) == (
        int(bfs_match.group(1)) + int(dyn_match.group(1))
    ), switching_case
    print(f"PASS dynamic switching {switching_case}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
