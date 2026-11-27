# ROME 350 与新算法实验运行手册

本文档是后续实验的唯一运行入口。所有实验由用户在本机执行；脚本负责清单校验、命令构造、状态记录和结果汇总。

## 1. 测例范围

| 集合 | 数量 | 内容 |
| --- | ---: | --- |
| 旧测例 | 366 | dominoes 10、旧 exact-cover 217、旧 set-partition 52、Topology Zoo 87 |
| 新 ROME | 350 | `data/benchmarks/rome3_350/*.txt` |
| 当前全部 | 716 | 旧 366 + 新 ROME 350 |

旧集合从 `Final_Experiment_Report.xlsx` 选名，并排除 xlsx 中将被新 ROME 替换的旧 `grafo*`。具体名单位于：

- `data/benchmarks/manifests/old_all.txt`
- `data/benchmarks/manifests/old_dominoes.txt`
- `data/benchmarks/manifests/old_exact_cover.txt`
- `data/benchmarks/manifests/old_set_partition.txt`
- `data/benchmarks/manifests/old_topology.txt`
- `data/benchmarks/manifests/new_rome3.txt`
- `data/benchmarks/manifests/all_current.txt`
- `data/benchmarks/benchmark_manifest.csv`

精确覆盖求解器和模型计数器只跑新 ROME 350；DXD 三版本、CC 实验和最终箱线图实验跑当前全部 716。

## 2. 统一约定

- 单次求解超时默认 1500 秒，可用 `--timeout` 覆盖。
- 性能实验默认省略 DNNF Size；需要结构规模时从命令中删除 `--skip-dnnf-size`。
- 超时、内存不足、崩溃、结果不完整和不同算法解数不一致分别记录，不伪造时间。
- 结果中 `time_s` 是求解器报告时间，`wall_time_s` 是进程墙钟时间。
- DXD 三版本固定为：`DXD`、`pureDynDXD`（`ddxd --pure-dyndxd`）、`HybridDXD`。
- 箱线图实验保留每次原始运行，每个测例、每个算法、每个线程配置各 10 个样本，不做平均。
- 当前模型计数器为 d4、SharpSAT-TD、ExactMC、GANAK、DeMoniaC、countAntom、DisCount。
- DisCount 的入口是 `MC_BINS/discount/build/discount`，必须通过 `mpirun -np 4` 运行；`MC_BINS/discount` 本身是源码目录，不是可执行文件。
- 模型计数器实验的 CNF 使用 Linux `memfd`，实验结束关闭描述符，不在工作区保存 CNF。

计划运行量用于检查是否遗漏：

| 实验 | 计算 | 运行数 |
| --- | --- | ---: |
| 新 ROME DLX/DXZ | 350 × 2 | 700 |
| 新 ROME D3X | 350 × 1 | 350 |
| 新 ROME 模型计数 | 350 × 7 × 4 | 9,800 |
| 全部 DXD 三版本 | 716 × 3 × 2 | 4,296 |
| CC 粗粒度 | 716 × 2 | 1,432 |
| CC 详细指标 | 716 × 2 | 1,432 |
| 箱线图 | 716 × 2 × 4 × 10 | 57,280 |

## 3. 首次准备与检查

1. 生成新测例：
```bash
cd /home/lyh/projects/lyh/Alg_DXD/GMLToMat

python3 generate_hybrid_matrix.py \
  --graph-dir graphs_graphml/rome_3 \
  --output-dir ../Algorithm_DXD/data/benchmarks/rome3_350 \
  --count 0 \
  --seed 20261127 \
  --min-blocks 20 --max-blocks 25 \
  --hard-blocks 1 \
  --pivot \
  --max-row-width 3 \
  --hard-min-rows-per-block 1350 --hard-max-rows-per-block 1400 \
  --hard-min-cols-per-block 23 --hard-max-cols-per-block 24 \
  --filler-min-rows-per-block 3600 --filler-max-rows-per-block 3700 \
  --filler-min-cols-per-block 13 --filler-max-cols-per-block 14 \
  --filler-max-row-width 8
```

2. 生成清单、构建并预检
```bash
cd /home/lyh/projects/lyh/Alg_DXD/Algorithm_DXD

python3 bin/benchmark_catalog.py --write
cmake --build build -j 16
make -C data/ToZdd -j 16
cmake --build ../D3X/build -j 16
python3 bin/check_experiment_setup.py --all-counters --memory-gb 8
```

必须看到清单为 350 个新 ROME 测例，且最后输出 preflight checks passed。

## 4. 新 ROME 精确覆盖：DLX、DXZ 并行执行

```bash
cd /home/lyh/projects/lyh/Alg_DXD/Algorithm_DXD

python3 bin/batch_runner.py \
  --suite exact \
  --benchmark-set new \
  --timeout 1500 \
  --output-dir results/runs/20261127_exact_new \
  --dxz-stats \
  --save-logs
```

调度方式为两个 worker 同时启动：DLX、DXZ 各占一个并发任务，并分别写入独立文件。只有两个算法都结束后，脚本才执行解数一致性检查和汇总。
`--dxz-stats` 仅对 DXZ 追加 `--enable-cc-stats`；命中率写入
`runs_DXZ.csv` 的 `cache_reads`、`cache_hits`、`cache_hit_rate` 列，完整输出保存在 `logs/`。

输出目录为 `results/runs/<时间>_exact_new/`：

- `runs_DLX.csv`：仅 DLX 的原始结果
- `runs_DXZ.csv`：仅 DXZ 的原始结果
- `runs.csv`：将上述两个文件合并后的兼容结果，供现有校验器读取
- `summary.csv`：按算法统计成功、超时和错误
- `config.json`：命令参数和运行配置

预期规模：350 个测例 × 2 个求解器 = 700 次。

续跑时指定原输出目录并加 `--resume`：

脚本按 `instance + variant + threads + repeat` 判断已有结果，不会重新执行已经记录的测例。旧版 exact 运行目录只有合并的 `runs.csv` 时也可以直接续跑：脚本会先把其中记录拆分到 `runs_DLX.csv` 和 `runs_DXZ.csv`，再只补缺失项。

## 5. 编译新 ROME ZDD 与 D3X 启动

先将 350 个新矩阵逐个编译为 ZDD。已存在且非空的 ZDD 会跳过；单个失败不会终止整批：

```bash
cd /home/lyh/projects/lyh/Alg_DXD/Algorithm_DXD

python3 bin/prepare_rome_zdd.py \
  --case-file data/benchmarks/manifests/new_rome3.txt \
  --timeout 1500

awk -F, 'NR>1 && $5!="success"{n++} END{print "failures=" n+0}' \
  data/ToZdd/rome3_output/compile_results.csv
```

输出：

- ZDD：`data/ToZdd/rome3_output/<instance>.zdd`
- 编译状态：`data/ToZdd/rome3_output/compile_results.csv`

确认failures = 0 后运行 D3X：

```bash
cd /home/lyh/projects/lyh/Alg_DXD

python3 D3X/bin/run_D3X.py \
  --case-file Algorithm_DXD/data/benchmarks/manifests/new_rome3.txt \
  --input-dir Algorithm_DXD/data/ToZdd/rome3_output \
  --timeout 1500
```

预期规模：350 次。DLX/DXZ/D3X 都完成后核对三份结果中的 `solutions`；成功记录必须一致。

## 6. 新 ROME 七个模型求解器并行跑四种 CNF 编码

默认父进程一次处理一个 worker，workerd 内每个计数器一个线程，四种编码并行提交，同时最多运行 4 个求解子进程。这个配置兼顾 16 核机器上的吞吐和 62 GiB 内存：

```bash
cd /home/lyh/projects/lyh/Alg_DXD/MC_BINS

python3 run_counters.py \
  --case-file ../Algorithm_DXD/data/benchmarks/manifests/new_rome3.txt \
  --workers 1 \
  --batch-size 1 \
  --solver-jobs 7 \
  --memory-gb 8 \
  --timeout 1500 \
  --output-dir results/rome3_counters_20261127
```

输出位于 `MC_BINS/results/rome3_counters_<时间>/`：

- `mc_runs.csv`：测例 × 计数器 × 四种编码的原始记录
- `mc_best.csv`：每个计数器在成功且解数一致的编码中取最快者
- `mc_summary.csv`：每个计数器的成功、超时、错误及获胜编码次数
- `config.json`：并行度和内存配置

`--solver-jobs 7` 让 7 个模型求解子进程同时处于活动状态，每个求解器
依次覆盖 4 种编码；预期规模仍为 350 × 7 × 4 = 9,800 次。

并行度不要只看 CPU 核数。建议总并发满足：

```text
workers × solver_jobs × memory_gb <= 可用内存的安全范围
```

例如 62 GiB 机器可用 `workers=1, solver_jobs=7, memory_gb=8`。
若要 7 个求解器 × 4 种编码共 28 个进程同时展开，将
`--batch-size 1 --solver-jobs 28`，但 62 GiB 机器不建议直接使用该配置。

最终一致性检查
```bash
cd /home/lyh/projects/lyh/Alg_DXD/Algorithm_DXD

python3 bin/validate_results.py exact \
  --batch-runs results/runs/20261127_exact_new/runs.csv \
  --d3x-runs ../D3X/output/rome3_20261127/D3X_results.csv

python3 bin/validate_results.py mc \
  --best ../MC_BINS/results/rome3_counters_20261127/mc_best.csv
```

## 7. 全部 716 个测例的 DXD 三版本分组

### 7.1 单线程分组（并发）

```bash
cd /home/lyh/projects/lyh/Alg_DXD/Algorithm_DXD
python3 bin/batch_runner.py \
  --suite dxd-versions \
  --group single-thread \
  --benchmark-set all \
  --timeout 1500 \
  --output-dir results/runs/20261117_120000_dxd_single-thread
```

`DXD`、`pureDynDXD`、`HybridDXD` 的 T1 实验各由一个 worker 并发执行；并发粒度是算法进程，每个 worker 内部仍按测例串行。本分组只运行单线程版本，预期 716 × 3 = 2,148 次。`--memory-gb` 分别作用于每个并发求解进程，因此该分组的总内存上限约为 `3 × --memory-gb`。

### 7.2 多线程分组（串行）

```bash
cd /home/lyh/projects/lyh/Alg_DXD/Algorithm_DXD
python3 bin/batch_runner.py \
  --suite dxd-versions \
  --group multi-thread \
  --benchmark-set all \
  --timeout 1500 \
  --output-dir results/runs/20261117_120000_dxd_multi-thread
```

三个算法的 T8 实验依次串行执行，不与单线程版本同时运行。本分组预期 716 × 3 = 2,148 次。

两个分组分别写入各自的输出目录，每个算法一个文件，避免并发 CSV 写冲突：

- `runs_DXD.csv`
- `runs_pureDynDXD.csv`
- `runs_HybridDXD.csv`
- `summary.csv`：三个文件完成一致性检查后的汇总统计

如需在一次调用中跑完整实验，可省略 `--group` 或显式使用 `--group all`：

```bash
cd /home/lyh/projects/lyh/Alg_DXD/Algorithm_DXD
python3 bin/batch_runner.py \
  --suite dxd-versions \
  --group all \
  --benchmark-set all \
  --timeout 1500
```

`all` 仍分成两个独立阶段：先并发完成全部 T1 算法，再串行执行全部 T8 算法；T1 与 T8 不会混合并发。完整实验预计 716 × 6 = 4,296 次。仅须补齐某一分组时，应使用对应的 `--group single-thread` 或 `--group multi-thread`。

完成后必须检查三个 `runs_*.csv`：

- `status=success` 的记录可作为性能样本。
- `status=solution_mismatch` 表示同一测例不同版本/线程返回不同解数，不得进入性能结论。
- 原始 `solutions` 保留在 CSV 中，便于定位算法问题。

## 8. 单线程动态连通实验

粗粒度 DXD 与 Hybrid CC 时间对比：

```bash
cd /home/lyh/projects/lyh/Alg_DXD/Algorithm_DXD
python3 bin/run_cc_experiment.py \
  --benchmark-set all \
  --timeout 1500
```

每个测例生成 DXD、Hybrid 两行，预期 1,432 次运行。结果为 `results/cc_cpu_experiment_<时间>.csv`，同时记录总时间、BFS、ETT 构建/减量/抽取/释放时间和解数一致性。

详细 CC 指标分两次执行：

```bash
cd /home/lyh/projects/lyh/Alg_DXD/Algorithm_DXD
python3 bin/run_cc_dynamics_experiment.py \
  --algorithm dxd \
  --benchmark-set all \
  --timeout 1500

python3 bin/run_cc_dynamics_experiment.py \
  --algorithm hybrid \
  --benchmark-set all \
  --timeout 1500
```

每次 716 次，共 1,432 次。结果分别为 `results/cc_metrics_dxd_<时间>.csv` 和 `results/cc_metrics_hybrid_<时间>.csv`。

## 9. DXD/Hybrid 箱线图数据

```bash
cd /home/lyh/projects/lyh/Alg_DXD/Algorithm_DXD
python3 bin/batch_runner.py \
  --suite thread-compare \
  --benchmark-set all \
  --rounds 10 \
  --timeout 1500
```

配置为 DXD/Hybrid × 1/2/4/8 线程 × 每个配置 10 次。每个测例产生 80 条原始样本，全部 716 个测例共 57,280 次运行。

输出 `runs.csv` 直接保留每一轮的 `variant, threads, repeat, time_s, status`，不得再按测例或线程数平均后保存。画图时建议：

- 只把 `status=success` 的 `time_s` 放入时间箱线图；
- 单独统计 `timeout`、`error` 和 `solution_mismatch`；
- 按 `variant × threads` 分组；
- 使用 `instance` 和 `repeat` 保留配对关系。

## 10. 断点续跑

DXD 分组续跑必须使用原输出目录、相同 `--group` 和 `--resume`。例如：

```bash
cd /home/lyh/projects/lyh/Alg_DXD/Algorithm_DXD
python3 bin/batch_runner.py \
  --suite dxd-versions \
  --group single-thread \
  --benchmark-set all \
  --timeout 1500 \
  --output-dir results/runs/20261117_120000_dxd_single-thread \
  --resume

python3 bin/batch_runner.py \
  --suite dxd-versions \
  --group multi-thread \
  --benchmark-set all \
  --timeout 1500 \
  --output-dir results/runs/20261117_120000_dxd_multi-thread \
  --resume
```

batch runner 的续跑以 `instance + variant + threads + repeat` 作为完成键。所有已写入 CSV 的状态都视为已有结果，包括 `success`、`timeout`、`memout` 和 `error`，不会自动重跑；如需重试某条失败记录，应使用新的输出目录，或明确删除该记录后在该输出目录续跑。exact 分组也使用 `--output-dir ... --resume`，见第 4 节。

模型计数器续跑：

```bash
cd /home/lyh/projects/lyh/Alg_DXD/MC_BINS
python3 run_counters.py \
  --case-file ../Algorithm_DXD/data/benchmarks/manifests/new_rome3.txt \
  --output-dir results/rome3_counters_20261113_120000 \
  --resume
```

CC 实验续跑：

```bash
cd /home/lyh/projects/lyh/Alg_DXD/Algorithm_DXD
python3 bin/run_cc_experiment.py \
  --benchmark-set all \
  --timeout 1500 \
  --output-dir results \
  --resume

python3 bin/run_cc_dynamics_experiment.py \
  --algorithm dxd \
  --benchmark-set all \
  --timeout 1500 \
  --output-dir results \
  --resume
```

ZDD 编译会自然跳过已存在文件。D3X 续跑时用 `--run-dir` 指定原运行目录并加 `--resume`：

```bash
cd /home/lyh/projects/lyh/Alg_DXD
python3 D3X/bin/run_D3X.py \
  --input-dir Algorithm_DXD/data/ToZdd/rome3_output \
  --case-file Algorithm_DXD/data/benchmarks/manifests/new_rome3.txt \
  --run-dir D3X/output/rome3_20261113_120000 \
  --resume
```

batch runner 用 `--output-dir` 指定原运行目录；DXD 三版本还必须保持与首次运行相同的 `--group`。

## 11. DisCount 集成

DisCount 已接入统一计数器实验：

1. 可执行文件为 `MC_BINS/discount/build/discount`；
2. 运行命令为 `mpirun -np 4 <discount> -m counter -i <cnf>`；
3. 模型数解析 `^s <整数>$`，成功退出码为 0；
4. DisCount 不自报总时间，汇总排名使用 `wall_time_s`；
5. `--all-counters` 已覆盖 DisCount 的小 CNF 端到端探针。

## 12. 最终验收

每次全量实验完成后至少检查：

1. 清单计数仍为 10/217/52/87/366/350/716。
2. 每个计划的测例、算法、线程配置和 repeat 都有记录。
3. 成功记录的模型数在不同算法、线程和编码之间一致。
4. 超时、错误和 mismatch 有独立计数。
5. DLX、DXZ、D3X 在新 ROME 上的成功解数一致。
6. `thread-compare` 每个测例每个配置恰好 10 条记录。
7. 工作区没有因模型计数器产生持久化 CNF。

可直接运行结果校验器：

```bash
cd /home/lyh/projects/lyh/Alg_DXD/Algorithm_DXD
python3 bin/validate_results.py exact \
  --batch-runs results/runs/<exact_run>/runs.csv \
  --d3x-runs ../D3X/output/<d3x_run>/D3X_results.csv

python3 bin/validate_results.py dxd \
  --runs results/runs/<dxd_run>/runs.csv

python3 bin/validate_results.py thread \
  --runs results/runs/<thread_run>/runs.csv \
  --rounds 10

python3 bin/validate_results.py mc \
  --best ../MC_BINS/results/rome3_counters_<run>/mc_best.csv
```
