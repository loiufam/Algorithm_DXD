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
./main -a dlx -i ../data/graphs_set/AttMpls.txt
 
# Compile a ZDD over the solution set
./main -a dxz -i ../data/graphs_set/AttMpls.txt
 
# Single-thread DXD (Decision-DNNF compilation)
./main -a dxd -i ../data/graphs_set/AttMpls.txt
 
# Multi-thread DXD with 8 threads (default)
./main -a mdxd -i ../data/graphs_set/AttMpls.txt
 
# Multi-thread DXD with 4 threads
./main -a mdxd -i ../data/graphs_set/AttMpls.txt -t 4
 
# Single-thread baseline for mdxd (useful for speedup measurement)
./main -a mdxd -i ../data/rugraphs_setn_set/AttMpls.txt -t 1
 
```

---
 
## Benchmarks

Four dataset collections are stored under the `benchmark/` directory.
 
| Directory | Format | Description |
|-----------|--------|-------------|
| `benchmark/exact_cover_benchmark/` | Standard exact-cover format | Classic exact cover instances |
| `benchmark/run_set/` | Graph-derived format | Instances generated from network graphs (Topology Zoo and Rome) |

The input format is detected automatically from the **parent directory name**:
 
- parent = `exact_cover_benchmark` → read mode 1
- parent = `run_set` → read mode 3
If a file is placed elsewhere the solver defaults to read mode 3.

---
