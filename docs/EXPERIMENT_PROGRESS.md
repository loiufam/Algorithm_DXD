# 实验实施进度

更新日期：2026-11-13

| 项目 | 状态 | 说明 |
| --- | --- | --- |
| 旧/新测例划分 | 完成 | 旧 366、新 350、全部 716；新旧交集 0 |
| 测例清单生成器 | 完成 | `bin/benchmark_catalog.py` |
| 精确覆盖/DXD batch runner | 完成 | 精确覆盖、DXD 三版本、箱线图 10 次重复长表 |
| 四种 CNF 编码 | 完成 | 修复 ladder 编码缺失 at-least-one 的问题 |
| ZDD 批量编译 | 完成 | 单文件失败不终止整批，支持跳过已有产物 |
| D3X 运行脚本 | 完成 | 按新 ROME 清单过滤并写入统一状态 |
| 六计数器内存实验 | 完成 | 父进程分批、worker 多线程、四 CNF 使用 memfd |
| DisCount | 等待二进制 | 已预留接口位置，当前选择运行会明确报不可用 |
| DXD/Hybrid CC 脚本 | 完成 | 已迁移到统一 manifest，Hybrid 指标已解析 |
| 中文 Runbook | 完成 | `docs/EXPERIMENT_RUNBOOK.md` |
| 当前全量实验 | 未执行 | 按 Runbook 由用户运行 |

## 已执行验证

- `python3 bin/benchmark_catalog.py --write`：计数和路径校验通过。
- `python3 bin/check_experiment_setup.py`：pairwise/ladder/bitwise/matrix 解数一致，D3X 探针通过。
- 15 个随机小 exact-cover 实例 × 4 种编码均与穷举覆盖数一致。
- `python3 bin/check_experiment_setup.py --all-counters --memory-gb 8`：六计数器在内存 CNF 上均返回模型数 1。
- DXD 三版本单例：DXD、pureDynDXD T1、Hybrid 结果一致。
- DXD/Hybrid CC 单例：两份 CC 结果脚本均正常写 CSV，DXD/Hybrid 解数一致。
- `bell-06.ec -> zdd_compiler -> D3X` 小样本链路通过，解数为 203。
- 箱线图 runner 以 2 轮配置完成 16 条样本验证，每个配置恰好 2 条。

## 待处理

- 用户补充 DisCount 二进制和输出格式后注册第七个计数器。
- 用户执行全量实验并进行最终结果一致性审计。

## 结果记录约定

每次正式运行后在表中追加一行：

| 日期 | 实验 | 命令/配置 | 结果路径 | 计划数 | 实际数 | 失败数 | 结论 |
| --- | --- | --- | --- | ---: | ---: | ---: | --- |
| 待填 | 待填 | 待填 | 待填 | 待填 | 待填 | 待填 | 待填 |
