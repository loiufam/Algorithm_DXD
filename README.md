# Exact Cover Solvers
 
This repository implements algorithms for solving the **exact cover problem** efficiently.
Key components include:
 
- **Dancing Links (DLX)** matrix representation for compact constraint encoding
- **Decision-DNNF / ZDD compilation** (DXD, DXZ) for knowledge compilation over exact cover instances
- **Dynamic connected-component detection** via Splay-tree Euler Tour Trees (SplayETT)
- **Parallel search** with OpenMP across independently decomposed sub-problems

---
 
## Table of Contents
 
1. [Requirements](#requirements)
2. [Build](#build)
3. [Usage](#usage)
   - [Examples](#examples)
4. [Benchmarks](#benchmarks)

---

## Requirements
 
| Component | Minimum version |
|-----------|----------------|
| OS | Ubuntu 22.04 |
| GCC / G++ | 11.4 |
| CMake | 3.22.1 |
| OpenMP | 4.0 |
 
**Verify OpenMP availability:**
 
```bash
echo | g++ -fopenmp -dM -E - | grep _OPENMP
```
 
The output is a date-coded integer.  Example: `_OPENMP 201511` → OpenMP 4.5.
 
**Install build dependencies:**
 
```bash
sudo apt update
sudo apt install -y build-essential cmake
```
 
**Install the `dxd` library:**
 
```
https://github.com/loiufam/Algorithm_DXD
```

---

## Build
 
```bash
mkdir build && cd build
 
# Debug build (assertions enabled, no optimisation)
cmake -DCMAKE_BUILD_TYPE=Debug ..
 
# Release build (recommended for benchmarking)
cmake -DCMAKE_BUILD_TYPE=Release ..
 
make -j
```
 
The compiled binary is placed at `build/main`.
 
---


## Usage
 
```
./main -a <alg> -i <input> [-t <threads>] [-d] [-h]
```


### Examples
 
```bash
# Count solutions with single-thread DLX
./main -a dlx -i ../data/run_set/Aarnet.txt
 
# Compile a ZDD over the solution set
./main -a dxz -i ../data/exact_cover_benchmark/hard.txt
 
# Single-thread DXD (Decision-DNNF compilation)
./main -a dxd -i ../data/run_set/Aarnet.txt
 
# Multi-thread DXD with 8 threads (default)
./main -a mdxd -i ../data/run_set/Aarnet.txt
 
# Multi-thread DXD with 4 threads
./main -a mdxd -i ../data/run_set/Aarnet.txt -t 4
 
# Single-thread baseline for mdxd (useful for speedup measurement)
./main -a mdxd -i ../data/run_set/Aarnet.txt -t 1
 
```

---
 
## Benchmarks

### Single-thread CC timing experiment

Release builds place the executable in `bin/main`. The dedicated experiment
runner reads `data/batch_2/Final_Experiment_Report.xlsx`, selects instances for
which both `DXD-T1` and `DynDXD-T1` completed, and records wall-clock search
time plus CC CPU time in CSV format:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
python3 bin/run_cc_experiment.py
```

The default output is `results/cc_cpu_experiment.csv`. Use `--dry-run` to list
the selected inputs, or `--limit N` for a short smoke test.

### Full dynamic-connectivity statistics

The structural CC experiment has a dedicated runtime mode, so production
DynDXD keeps its adaptive shutdown while the unoptimized experiment remains
reproducible from the same revision:

```bash
python3 bin/run_cc_dynamics_experiment.py
```

The runner passes `--full-cc-stats`, which prevents DynDXD from disabling
dynamic updates and emits per-instance raw counters. It writes
`results/cc_dynamics_instances.csv` and dataset-level macro averages to
`results/cc_dynamics_summary.csv`. The summary separates merge/split counts
and rates, graph/update sizes, non-tree-edge sizes, replacement scan work, and
early-break/full-scan rates. Use `--limit N` for a smoke test; expect this mode
to be substantially slower than normal DynDXD. `--timeout SECONDS` is passed
to the solver as its internal bound. If that bound is reached, the instance CSV
keeps the partial counters with `status=timeout_partial` and
`stats_complete=0`, but dataset averages exclude that censored row. Increase
the timeout and rerun to obtain complete per-solve statistics.
Use `--resume` together with a larger timeout to retain completed rows and rerun
only timeout/error cases.

Two dataset collections are stored under the `benchmark/` directory.
 
| Directory | Format | Description |
|-----------|--------|-------------|
| `benchmark/exact_cover_benchmark/` | Standard exact-cover format | Classic exact cover instances |
| `benchmark/run_set/` | Graph-derived format | Instances generated from network graphs (Topology Zoo and Rome) |

The input format is detected automatically from the **parent directory name**:
 
- parent = `exact_cover_benchmark` → read mode 1
- parent = `run_set` → read mode 3
If a file is placed elsewhere the solver defaults to read mode 3.

---
