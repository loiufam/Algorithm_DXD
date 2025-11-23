#ifndef COMPONENT_DETECTOR_H
#define COMPONENT_DETECTOR_H

#pragma once

#include "SplayETT.h"
#include "TreapETT.h"
#include <map>
#include <set>
#include <stack>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <shared_mutex>
#include <memory>
#include <atomic>
#include <optional>
#include <cstdint>
#include <stdexcept>

// 检测上下文 - 存储某个递归层的检测结果
struct DetectionContext {
    set<int> block_rows;                          // 当前块包含的所有行
    vector<Block> components;                     // 检测到的连通分量
    unordered_map<int, size_t> row_to_component;  // 行 -> 分量索引的映射
    int depth;                                    // 递归深度
    
    DetectionContext(int d = 0) : depth(d) {}
    
    // 根据连通分量结果构建索引
    void build_index() {
        row_to_component.clear();
        for (size_t i = 0; i < components.size(); ++i) {
            for (int r : components[i].rows) {
                row_to_component[r] = i;
            }
        }
    }
};

// 线程上下文：每个线程独立的缓存空间
struct ThreadContext {
    uint64_t thread_id;                                       // 线程唯一ID
    unordered_map<int, shared_ptr<DetectionContext>> cache;   // depth -> context
    mutable shared_mutex mutex;                               // 保护该线程的缓存
    
    ThreadContext(uint64_t id) : thread_id(id) {}
};

// 并查集实现
class UnionFind {
private:
    std::unordered_map<int, int> parent;
    std::unordered_map<int, int> rank;
    
public:
    void make_set(int x) {
        if (parent.find(x) == parent.end()) {
            parent[x] = x;
            rank[x] = 0;
        }
    }
    
    int find(int x) {
        if (parent[x] != x) {
            parent[x] = find(parent[x]); // 路径压缩
        }
        return parent[x];
    }
    
    void unite(int x, int y) {
        int px = find(x);
        int py = find(y);
        if (px == py) return;
        
        // 按秩合并
        if (rank[px] < rank[py]) {
            parent[px] = py;
        } else if (rank[px] > rank[py]) {
            parent[py] = px;
        } else {
            parent[py] = px;
            rank[px]++;
        }
    }

    unordered_map<int, set<int>> get_components() {
        unordered_map<int, set<int>> components;
        for (const auto& [node, _] : parent) {
            int root = find(node);
            components[root].insert(node);
        }
        return components;
    }
};

// 用于保存 Cover 操作的历史记录，由调用线程持有
struct CoverHistory {
    int col;
    // 记录被完全移除的边
    std::vector<std::pair<int, int>> edges_removed;
    // 记录该列参与的所有边（用于 Uncover 时恢复引用计数）
    std::vector<std::pair<int, int>> edges_affected;
    // 记录因此变为空而被标记为不活跃的行
    std::vector<int> rows_deactivated;
};

class ComponentDetector {
private:

    unique_ptr<SplayETT> ett;

    // ===============================================================
    // Dancing Links
    // ===============================================================
    int num_rows;
    int num_cols;

    std::unordered_map<int, std::vector<int>> col_to_rows;
    std::unordered_map<int, std::unordered_set<int>> row_to_cols;

    // 边与列的映射：Edge(u, v) -> {Columns supporting this edge}
    // 只有当 set 为空时，边才在 ETT 中断开
    std::map<std::pair<int, int>, std::unordered_set<int>> edge_columns;

    // Adjacency list: row -> set of adjacent rows
    std::vector<std::set<int>> adj_list;

    // Track which rows are currently active (not removed)
    std::unordered_set<int> active_rows;
    std::unordered_set<int> active_cols;

    // Union-Find 结构（用于快速分组）
    mutable std::vector<int> global_parent;
    mutable bool global_uf_valid = false;

    // function<const set<int>&(int)> get_row_cols;

    // 锁机制
    // ett_mutex 保护 ETT 实例以及 edge_columns 映射，因为它们是强耦合的
    mutable std::shared_mutex ett_graph_mutex; 
    
    // 保护矩阵状态 (row_to_cols, active_rows 等)
    mutable std::shared_mutex state_mutex;

    // 线程私有的历史栈
    static thread_local std::vector<CoverHistory> cover_stack_;
    
    // 线程上下文管理
    mutable shared_mutex thread_ctx_mutex;
    unordered_map<uint64_t, shared_ptr<ThreadContext>> thread_contexts; // thread_id -> context
    atomic<uint64_t> next_thread_id{1};  // 自增的线程ID分配器

private:
    void BuildInitialGraph() {
        for (const auto& [col, rows] : col_to_rows) {
            for (size_t i = 0; i < rows.size(); i++) {
                for (size_t j = i + 1; j < rows.size(); j++) {
                    int r1 = rows[i], r2 = rows[j];
                    if (r1 > r2) std::swap(r1, r2);
                    edge_columns[{r1, r2}].insert(col);
                }
            }
        }
        
        for (const auto& [edge, cols] : edge_columns) {
            if (!cols.empty() && !ett->connected(edge.first, edge.second)) {
                ett->link(edge.first, edge.second);
            }
        }
    }

    void RebuildGlobalUnionFind() const {
        if (global_uf_valid) return;
        
        global_parent.resize(num_rows);
        std::iota(global_parent.begin(), global_parent.end(), 0);
        
        for (const auto& [edge, cols] : edge_columns) {
            int r1 = edge.first;
            int r2 = edge.second;
            
            if (active_rows.count(r1) && active_rows.count(r2)) {
                GlobalUnite(r1, r2);
            }
        }
        
        global_uf_valid = true;
    }
    
    int GlobalFind(int x) const {
        if (global_parent[x] != x) {
            global_parent[x] = GlobalFind(global_parent[x]);
        }
        return global_parent[x];
    }
    
    void GlobalUnite(int x, int y) const {
        x = GlobalFind(x);
        y = GlobalFind(y);
        if (x != y) {
            global_parent[x] = y;
        }
    }

    // 内部cover/uncover操作
    CoverHistory DoCover(int c);
    void DoUncover(const CoverHistory& history);

public:

    ComponentDetector(const int n, const int m);

    void Initialize(const std::unordered_map<int, std::vector<int>>& col_rows_map){
        col_to_rows = col_rows_map;
        
        for (const auto& [col, rows] : col_to_rows) {
            active_cols.insert(col);
            for (int r : rows) {
                row_to_cols[r].insert(col);
                active_rows.insert(r);
            }
        }
        
        BuildInitialGraph();
    };
  
    ~ComponentDetector() = default;

    void add_row(int r) {
        std::vector<int> cols_to_link;
        {
            std::unique_lock<std::shared_mutex> lk(state_mutex);
            if (active_rows.find(r) != active_rows.end()) return; // 已活跃
            // 把 r 标记为活跃
            active_rows.insert(r);

            // 收集 r 对应的所有列（row_to_cols[r]）
            for (int col : row_to_cols[r]) {
            cols_to_link.push_back(num_rows + col); // 列节点 id = offset
            }
        }

        std::unique_lock<std::shared_mutex> ett_lk(ett_graph_mutex);
        for (int col_node : cols_to_link) {
            // link r -- col_node
            ett->link(r, col_node);
        }
    }

    void remove_row(int r) {
        std::vector<int> cols_to_cut;
        {
            std::unique_lock<std::shared_mutex> lk(state_mutex);
            if (active_rows.find(r) == active_rows.end()) return;
            active_rows.erase(r);

            for (int col : row_to_cols[r]) {
            cols_to_cut.push_back(num_rows + col);
            }
        }


        std::unique_lock<std::shared_mutex> ett_lk(ett_graph_mutex);
        for (int col_node : cols_to_cut) {
            ett->cut(r, col_node);
        }
    }

    void Cover(int c);
    
    void Uncover();

    // 在特定行集合内部寻找分块
    std::vector<Block> GetBlocks(const std::set<int>& block_rows) const {
        if (block_rows.empty()) return {}; // 如果没有行，则返回空集合
        std::shared_lock<std::shared_mutex> lock(state_mutex);

        // 创建局部 Union-Find
        std::unordered_map<int, int> local_parent;
        for (int r : block_rows) {
            local_parent[r] = r;
        }
        
        // 迭代版本的 find（带路径压缩）
        auto local_find = [&](int x) -> int {
            int root = x;
            // 找到根节点
            while (local_parent[root] != root) {
                root = local_parent[root];
            }
            // 路径压缩
            while (x != root) {
                int next = local_parent[x];
                local_parent[x] = root;
                x = next;
            }
            return root;
        };
        
        auto local_unite = [&](int x, int y) {
            x = local_find(x);
            y = local_find(y);
            if (x != y) {
                local_parent[x] = y;
            }
        };
        
        std::unordered_map<int, std::vector<int>> col_to_active_rows_in_block;
        
        for (int r : block_rows) {
            auto it = row_to_cols.find(r);
            if (it == row_to_cols.end()) continue;

            for (int c : it->second) {
                if (active_cols.count(c)) {
                    // 记录该列包含当前块内的哪些行
                    // 只有当一列包含 >= 2 个当前块的行时，它才提供连通性
                    col_to_active_rows_in_block[c].push_back(r);
                }
            }
        }

        // 根据列进行合并
        for (const auto& [col, rows_in_col] : col_to_active_rows_in_block) {
            if (rows_in_col.size() < 2) continue;
            
            // 将该列下的所有行合并到第一个行
            int first_row = rows_in_col[0];
            for (size_t i = 1; i < rows_in_col.size(); ++i) {
                local_unite(first_row, rows_in_col[i]);
            }
        }

        // 按代表元分组
        std::unordered_map<int, std::vector<int>> rep_to_rows_list;
        for (int r : block_rows) {
            rep_to_rows_list[local_find(r)].push_back(r);
        }
        
        // 构建 Block
        std::vector<Block> blocks;
        blocks.reserve(rep_to_rows_list.size());
        for (const auto& [rep, rows] : rep_to_rows_list) {
            std::set<int> component_cols;
            std::vector<int> component_rows_vec = rows;
            
            for (int r : rows) {
                auto it = row_to_cols.find(r);
                if (it != row_to_cols.end()) {
                    for (int col : it->second) {
                        if (active_cols.count(col)) {
                            component_cols.insert(col);
                        }
                    }
                }
            }
            
            // 只有非空的块才有意义
            if (!component_rows_vec.empty()) {
                blocks.emplace_back(std::move(component_rows_vec), std::move(component_cols));
            }
        }
        
        return blocks;
    }

    int GetComponentId(int r) {
        shared_lock active_lock(state_mutex);
        if (active_rows.find(r) == active_rows.end()) {
            return -1;
        }
        active_lock.unlock();
        
        shared_lock ett_lock(ett_graph_mutex);
        return ett->get_component_id(r);
    }

    // 此函数需要独占锁，因为内部有 Splay 操作
    bool IsConnected(int u, int v) const {
        std::unique_lock<std::shared_mutex> lock(ett_graph_mutex);
        return ett->connected(u, v);
    }

    // 获取或创建线程上下文
    shared_ptr<ThreadContext> get_thread_context(uint64_t thread_id) {
        shared_lock lock(thread_ctx_mutex);
        auto it = thread_contexts.find(thread_id);
        if (it != thread_contexts.end()) {
            return it->second;
        }
        
        // 需要创建新的线程上下文
        lock.unlock();
        unique_lock write_lock(thread_ctx_mutex);
        
        auto ctx = make_shared<ThreadContext>(thread_id);
        thread_contexts[thread_id] = ctx;
        return ctx;
    }

    
    // 对子集进行全量检测
    vector<Block> detect_full_subset(const set<int>& rows) {
        if (rows.empty()) return {};
                
        std::unordered_set<int> active_snapshot;
        {
            std::shared_lock<std::shared_mutex> lk(state_mutex);
            for (int r : rows) {
            if (active_rows.find(r) != active_rows.end())
                active_snapshot.insert(r);
            }
        }

        vector<Block> result;
        if (active_snapshot.empty()) return result;

        std::unordered_set<int> visited;
        for (int start : active_snapshot) {
            if (visited.count(start)) continue;
 
            std::vector<int> stack;
            stack.push_back(start);
            visited.insert(start);
            for (size_t idx = 0; idx < stack.size(); ++idx) {
                int u = stack[idx];
                for (int v : adj_list[u]) {
                    if (active_snapshot.count(v) && !visited.count(v)) {
                        visited.insert(v);
                        stack.push_back(v);
                    }
                }
            }
            // stack contains rows in this component
            set<int> rows_set(stack.begin(), stack.end());
            // collect cols covering these rows
            set<int> cols_set;
            for (int r : stack) {
                for (int c : row_to_cols[r]) cols_set.insert(c);
            }
            result.emplace_back(move(rows_set), move(cols_set));
        }

        return result;
    }

    vector<Block> detect_in_block(const set<int>& block_rows);

     vector<Block> detect_by_ett(const set<int>& block_rows);

    // 全量检测（用于首次检测）
    vector<Block> detect_full(const set<int>& block_rows, uint64_t thread_id, int depth = 0);

    // 增量检测：基于上次结果和变化的行进行增量更新
    vector<Block> detect_incremental(const set<int>& current_rows, 
                                     uint64_t thread_id,
                                     int parent_depth, 
                                     int current_depth);

    // 从父线程继承初始缓存（用于子线程启动时）
    // block_index: 子线程要处理的分块索引
    void inherit_context(uint64_t parent_thread_id, uint64_t child_thread_id, 
                        int depth, size_t block_index) {
        // 获取父线程的上下文
        auto parent_ctx = get_thread_context(parent_thread_id);
        shared_lock parent_lock(parent_ctx->mutex);
        
        auto it = parent_ctx->cache.find(depth);
        if (it == parent_ctx->cache.end()) {
            throw runtime_error("inherit_context: parent depth not found");
        }

        auto parent_context = it->second;

        parent_lock.unlock();
        
        // 为子线程创建新的上下文，只包含它要处理的那个分块
        auto child_det_ctx = make_shared<DetectionContext>(depth);
        
        // 子线程的block_rows是单个分块的行
        const auto& target_block = parent_context->components[block_index];
        child_det_ctx->block_rows = target_block.rows;
        
        // 子线程的components只有一个元素：它要处理的分块
        child_det_ctx->components = {target_block};
        
        child_det_ctx->build_index();
        // 存入子线程的缓存
        auto child_ctx = get_thread_context(child_thread_id);
        unique_lock child_lock(child_ctx->mutex);
        child_ctx->cache[depth] = child_det_ctx;
    }

    // 清理整个线程的缓存（线程结束时调用）
    void cleanup_thread(uint64_t thread_id) {
        unique_lock lock(thread_ctx_mutex);
        thread_contexts.erase(thread_id);
    }

};

#endif // COMPONENTDETECTOR_H