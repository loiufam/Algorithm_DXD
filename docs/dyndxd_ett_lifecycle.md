# DynDXD 中 ETT、BFS 与 DXZ 的切换规则

本文描述当前 `ddxd` 实现实际执行的状态机，而不是三种彼此独立的算法开关。

## 1. ETT 的建立和维护

矩阵初始化时，行投影图先建立生成森林；每个连通分量对应一棵
`EulerTourTree`。搜索覆盖列或选择行后，`DecUpdateCC` 删除失活行及其边；
回溯时 `IncUpdateCC` 恢复同一批行及边。因此，某一递归帧一旦执行过删除，
即使更深层随后切换到 BFS 或 DXZ，该帧仍必须执行配对的恢复。

发生分块后：

- 单线程搜索暂存外层森林，每次只把当前分量的 ETT 和子图交给该子块，
  子块结束后再放回森林；
- 多线程搜索把每个分量的 ETT 移交给对应 OpenMP 任务。任务使用
  `thread_local ThreadLocalState` 保存自己的森林、子图和自适应状态，结束后
  把 ETT 归还外层；
- 嵌套分块会暂存当前任务的 TLS，再为子任务建立新的 TLS，避免不同分量
  共同修改同一棵 ETT。

## 2. ETT 查询切换为 BFS

`NO_SPLIT_LIMIT` 当前为 3。一个状态连续三次连通分量查询都只得到一个块后，
设置 `bfs_fallback=true`。从下一次查询开始：

- 连通分量由舞蹈矩阵 BFS 计算，而不是从 ETT 森林读取；
- ETT **仍继续随 cover/uncover 做减量和增量维护**，以保证回溯一致性，并为
  后续真实分块时向子任务移交分量树；
- 若 BFS 找到多个块，`no_split_count` 清零。新创建的并行子块 TLS 会从
  `bfs_fallback=false` 开始；未创建新 TLS 的主线程状态不会自动清除该标志。

因此，“降为 BFS”目前只表示更换 CC 查询方式，并不等于立即销毁或停止维护
ETT。

## 3. 停止分解与切换到 DXZ

进入 BFS fallback 时会记录一个面积阈值：初始块或线程子块的
`rows * cols / 2`。在 BFS 模式下再次连续三次没有分块，并且当前块面积不大于
该阈值后：

- 主线程设置 `decomposition_disabled=true` 和 `dxz_fallback_mode=true`，关闭图
  同步；后续递归改用全局舞蹈矩阵的 DXZ 遍历，不再维护 ETT；
- 线程局部子块只设置 `decomposition_disabled=true`，不会进入 DXZ。原因是 DXZ
  使用全局矩阵遍历，不能安全地把搜索限制在并行任务的 `Block::cols` 中；该
  子块继续使用普通的块内 DXD 决策搜索。

此外，行数不超过 20 或列集合为空时不会再尝试连通分量分解，但这条小块规则
本身不会设置 DXZ fallback，也不会关闭 ETT 维护。

## 4. 两类典型实例

### 初始就是多个分块

第一次 ETT 查询直接得到多个块，不增加连续未分块计数。单线程依次搜索每个
分量；多线程把分量 ETT 分发给任务。每个新建线程子块拥有独立的“ETT 查询 →
BFS fallback → 停止分解”计数。

### 始终只有一个分块

前三次使用 ETT 查询；连续三次无分块后改用 BFS 查询。BFS 再连续三次无分块
且面积达到阈值时，主线程切换到 DXZ；线程子块则停止 CC 查询但不切换 DXZ。

## 5. 统计口径

- `ETT DXD Vertex/Edge Sum`：执行动态删除前的活跃森林规模之和，作为静态
  DXD 完整 CC 扫描的比较基线；
- `Dyn ETT Updated Vertex/Edge Sum`：只统计前向删除，不重复统计回溯恢复；
- `Dyn ETT Replacement Scan Steps`：删除树边时，从切割较小侧实际检查的跨侧
  非树边候选数。
