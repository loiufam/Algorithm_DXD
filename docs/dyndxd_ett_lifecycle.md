# DynDXD 分解策略与延迟 ETT 生命周期

本文描述当前 `ddxd` 的实际状态机。重点变化是：根层先用矩阵原生 BFS
找到初始独立块，只为选中的一个 ETT 探测块建立动态图；完整行投影 clique
不再物化。

## 1. 启动与根层分块

算法模式由枚举 `AlgorithmMode` 统一表达：`DXZ=0`、`DXD=1`、
`DYNDXD=2`。`-a ddxd` 构造 `DYNDXD` 模式时跳过
`buildGraphFromMatrix()`。因此启动阶段只读取矩阵和舞蹈链，不创建
`Σ C(column_degree, 2)` 个投影边。MDLX 的静态和动态模式使用枚举中的
后续值，不再通过独立的布尔 flag 区分。

第一次 `DXD()` 调用在根层执行 `getComponentsByBFS()`：

1. BFS 直接在矩阵行列交叉链表上求连通块；
2. 若得到多个块，按 `bfs_area_threshold` 只选一个块作为 ETT 探测目标；
3. 仅对该目标块执行 `prepareDynamicCCForBlock()`；
4. 其他初始块使用 `CCProbePhase::Disabled`，直接求解；
5. 若根层只有一个块，则为该块建立 ETT，并继续后续动态探测。

## 2. 按需邻居与生成森林

选中块只保存：

- 每个矩阵行对应的图顶点状态；
- 一棵或多棵 ETT 生成森林；
- 该块的活跃行集合。

边的来源保留在原有 `col_to_rows` 和 `row_to_cols` 中。需要邻居时，
`implicitProjectedNeighbors(row)` 扫描该行涉及的列，并返回同一选中块内
仍然活跃的行。非树边不会复制到以下三个位置：

- `Graph` 邻接表；
- `EulerTourTree::nonTreeEdges`；
- `EulerTourTree::nonTreeNeighbors`。

删除树边后，`EulerTourTree::findReplacementEdge()` 通过替换边提供器查询
按需邻居；恢复行时也通过同一提供器重新发现可连接的边。

## 3. 自适应策略

`DecompositionPolicy` 仍包含 `Ett`、`Bfs`、`Disabled` 三个阶段：

| 字段 | 含义 |
| --- | --- |
| `phase` | 当前查询阶段。 |
| `ettFailureAction` | 当前子树 ETT 连续未分块后的动作。 |
| `noSplitCount` | 当前阶段连续未分块次数。 |
| `skipNextProbe` | 当前帧是否跳过一次查询。 |
| `PROBE_LIMIT` | 自适应探测上限，当前为 3。 |

ETT 预算和 BFS 预算是独立的：

| 参数 | 实际作用 |
| --- | --- |
| `--cc-ett-max-calls N` | 最多执行 N 次 ETT 查询；默认 3，0 表示无限。耗尽后切换到 BFS。 |
| `--cc-bfs-max-calls M` | 最多执行 M 次 BFS 查询；默认 10，0 表示无限。耗尽后进入 `Disabled`。 |
| `--bfs-area-threshold N` | 根层出现多个块时选择探测块。 |

ETT 查询仍会计入 ETT 预算。预算耗尽时只停止 ETT，不会同时关闭 BFS。BFS
预算规则同样独立；只有 BFS 也被关闭，或自适应策略判定不再探测时，才进入
`Disabled`。

## 4. 动态更新的配对

一个递归帧只有在执行 `DecUpdateCC()` 后才需要执行配对的 `IncUpdateCC()`。
若当前帧查询后切换到 BFS 或 `Disabled`，后续递归帧不再维护 ETT，但已经发生
的帧级更新仍必须在回溯时恢复。

若某一层的所有子策略都已不是 `Ett`，进入串行或并行子搜索前会调用
`releaseDynamicCCStorage()`，释放动态图、ETT 森林和选中块活跃集合，避免与
DNNF 搜索图长期共存。

## 5. DNNF Size

DNNF 节点数在 `buildDecisionNode()` 和 `buildDecomposableNode()` 创建节点时
在线累计，不再在搜索结束后 DFS 整张 DAG。

性能实验默认传入 `--skip-dnnf-size`，只输出解数、时间和块数。需要节点数时
去掉该参数，求解器输出在线累计的 `DNNF Size`。

## 6. 退出语义

求解器异常会打印 `DXD内部错误` 或 `DynDXD内部错误`，并由 `main()` 返回非零
退出码。实验脚本把非零退出码记录为 `crashed`；外部进程 timeout 会被记录在
日志中。这样 `bad_alloc`、输入错误和正常完成不会再被混为成功。

## 7. 源码索引

| 位置 | 作用 |
| --- | --- |
| `src/main.cpp` | DynDXD 延迟 ETT 初始化、CLI 参数和异常退出。 |
| `src/DXDSolver.cpp` | 根层 BFS、预算切换、选中块 ETT 和 DNNF 在线计数。 |
| `src/DancingMatrix.cpp` | 按需投影邻居、选中块生成森林和 CC 存储释放。 |
| `include/DynamicConnectivity.h` | 隐式邻居提供器。 |
| `include/SplayTree.h`、`src/SplayTree.cpp` | 按需替换边查询。 |
| `include/DXD.h` | ETT/BFS 预算和自适应策略。 |
