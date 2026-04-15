This repository implements algorithms for solving the exact cover problem efficiently. The key components include: Dynamic generating connected components based on SplayETT and Parallel search.

## Build Instructions

System requirements: `Ubuntu22.04`

Environment requirements: `GCC 11.4 or higher, G++ 11.4 or higher, CMake 3.22.1 or higher, OpenMP 4.0 or higher`

To check whether OpenMP is available and verify its version, run:
```bash
echo | g++ -fopenmp -dM -E - | grep _OPENMP
```

The output is an integer macro indicating the supported OpenMP version.
For example: _OPENMP 201511 → OpenMP 4.5

To install all required dependencies, run:
```bash
sudo apt update
sudo apt install -y build-essential cmake
```

Install dxd from https://github.com/loiufam/alg-lab


### Compile and Run the code with the following command:

To compile the solver:
```bash
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Debug .. # or `cmake -DCMAKE_BUILD_TYPE=Release ..` 
make -j
```

To run the solver, you can use the script "main" in this
directory with the following arguments:
```bash
./main <alg_name> <test_case_path> [thread_num]
```

## Arguments

- **`alg_name`**  
  Specifies the algorithm to be used. Supported options include:
  - `dxd`: the DXD algorithm without dynamically updating connected components, instead of recomputing the whole graph by BFS (single-thread execution by default).
  - `mdxd`: the multi-threaded DXD algorithm with dynamically updating connected components (parallel execution with 8 threads by default).

- **`test_case_path`**  
  The path to the input test case file.

- **`thread_num`** *(optional)*  
  Specifies the number of threads to be used during execution.  
  This parameter is effective only for multi-threaded configurations (e.g., `mdxd`).


For example:
```bash
./main dxd ../data/run_set/Aarnet.txt # run DXD
./main mdxd ../data/run_set/Aarnet.txt 1 # run a benchmark by single-thread DynDXD
./main mdxd ../data/run_set/Aarnet.txt 8 # run a benchmark by 8 threads DynDXD
```

## Benchmarks

We use two types of exact cover instance datasets.
One is the exact cover benchmark dataset in the `exact_cover_benchmark` directory, and the other is the graph benchmark dataset in the `run_set` directory.

All datasets are stored in the `benchmark` folder.
