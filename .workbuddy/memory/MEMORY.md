# MEMORY.md - Algorithm_DXD 长期记忆

## 项目背景

Algorithm_DXD（DynDXD）是 DXZ 算法的并行改进版本，通过动态维护图连通性（Splay-tree based ETT + 增量子图）快速识别独立子矩阵，进行 AND-分解并行求解，最终输出 Decision-ZDNNF。

## 关键设计不变量（不要误判为 bug）

- **`DecUpdateCC` 使用 `comps[0]`** 是经过精心设计的单分量约束。所有调用上下文（serialSearch / parallelSearchUseOmp / MDLX / dxd_mode 顶层）都通过 stash.swap 或 tlsState 保证当前分量独占 `[0]` 位置。
- **DXD 主路径 cover/uncover 严格对称**，递归返回时 `comps` 应只剩 1 棵树。如果出现多棵，那是 IncUpdateCC/cutWithReplacement 的真实 bug，不要"合并掩盖"，应让它暴露（serialSearch 中已加 cerr 报警）。
- **图-矩阵一致性**：行 r 在矩阵中激活 ⇔ 顶点 r 在某棵活跃 ETT 中。`uncoverInBlock` 恢复行后，`IncUpdateCC` 应只对 `findEulerTourTree(u) != nullptr || restoredSet.count(u)` 的邻居恢复边，否则会形成幽灵边。
- 独立子矩阵在并发时天然无数据竞态，因为 `tlsState` 提供线程局部 ETT 副本，舞蹈链状态由各 block 不交的列集合自然隔离。
- 缓存键使用 `hashBlockState(block.cols)`：用户认为 cols 已包含足够语义。

## 构建环境

### Linux（原生支持）
- g++ + 标准 GMP / OpenMP / TBB

### macOS arm64（已扩展支持）

依赖：
```bash
brew install cmake gmp libomp tbb
```

CMake 已支持自动检测 Homebrew prefix（`/opt/homebrew` 或 `/usr/local`），AppleClang 下用 `-Xpreprocessor -fopenmp` + `libomp.dylib`。

构建：
```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j
ctest --output-on-failure
```

注意事项：
- 头文件不要使用 GCC 私有的 `<bits/stdc++.h>`；libc++ 比 libstdc++ 严格，需要显式 include `<fstream>` 等。
- macOS clang 不支持 `-static-libasan`，CMake 已条件化处理。
- `src/DynamicGraph.cpp` 通过 `#include` 在 `DancingMatrix.cpp` 中引入；不要在 CMake 中单独把它加进 source list（会触发 ODR）。

## 算法入口

- `-a dxz`：DXZ 串行（输出 ZDD）
- `-a dlx`：标准 DLX 计数
- `-a dxd`：DXD 串行（ETT + 分解）
- `-a ddxd -t N`：DynDXD 多线程（OMP，N 线程）
- `-a mdlx -t N`：多线程 DLX

## 测试

- `test/test_p1_fixes.cpp` → 目标 `test_p1_fixes` → ctest `p1_fixes`
- 真实实例差分：`./main -a dxz/dxd/ddxd -i data/dxz_set/dominoes_10x10.txt`，三者解数应 bit-equal。

## 已知约束

- gomonkey、ETT 性能层级未引入；当前是 O(n) nonTreeEdges 线性扫描的 findReplacementEdge，可作为后续优化项。
