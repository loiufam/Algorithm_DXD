# Benchmark 分类

本目录当前的实验全集由两部分组成。

## 旧测例

旧测例来自 `Final_Experiment_Report.xlsx`：

- `dominoes_set`：10 个
- `exact_cover_set`：报告中的 217 个
- `set_partition_set`：报告中的 52 个
- `graphs_set` 中的非 `grafo*` topology：87 个

旧 `grafo*` 共 350 个，已由 `rome3_matrices/matrices` 中的新 350 个 ROME 测例替换，不计入旧集合。

## 新 ROME

`rome3_matrices/matrices/*.txt` 共 350 个。精确覆盖求解器和模型计数器只运行这一部分。

## Manifest

运行 `Algorithm_DXD/bin/benchmark_catalog.py --write` 可重新生成：

- `manifests/*.txt`：脚本使用的实例名列表
- `benchmark_manifest.csv`：实例、分类、相对路径和来源
- `benchmark_manifest.json`：数量和选择规则

生成器会检查预期数量、路径存在性、重复 stem 以及新旧集合交集。
