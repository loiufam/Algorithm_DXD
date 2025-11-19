#ifndef COMPONENT_DETECTOR_H
#define COMPONENT_DETECTOR_H

#pragma once

#include "splay_tree_ett.hpp"
#include "TreapETT.h"
#include "SplayETT.h"
#include <vector>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <shared_mutex>
#include <memory>
#include <atomic>
#include <optional>
#include <cstdint>

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

class ComponentDetector {
private:

    // ===============================================================
    // Dancing Links
    // ===============================================================
    int num_rows;
    int num_cols;
    // 存储每行覆盖的列
    vector<vector<int>> row_to_cols;
    // Adjacency list: row -> set of adjacent rows
    std::vector<std::set<int>> adj_list;

    // Track which rows are currently active (not removed)
    std::unordered_set<int> active_rows;

    // ===============================================================
    // SplayETT
    // ===============================================================
    std::unique_ptr<splay_tree_ett::EulerTourTree> ett;
    unique_ptr<SplayETT> splay_ett;
    unordered_map<int, set<int>> col_to_rows; // column -> rows present
    // 记录每条边对应的列集合
    // edge_cols[(u,v)] = 同时包含u和v的所有列的集合
    map<pair<int, int>, set<int>> edge_cols;

    function<const set<int>&(int)> get_row_cols;

    // 多线程保护
    mutable shared_mutex ett_mutex;          // 保护ETT结构
    mutable shared_mutex col_rows_mutex;     // 保护col_rows和edge_count
    mutable shared_mutex active_rows_mutex;  // 保护active_rows
    mutable shared_mutex adj_list_mutex;     // 保护adj_list
    
    // 线程上下文管理
    mutable shared_mutex thread_ctx_mutex;
    unordered_map<uint64_t, shared_ptr<ThreadContext>> thread_contexts; // thread_id -> context
    atomic<uint64_t> next_thread_id{1};  // 自增的线程ID分配器

    mutable shared_mutex bridge_nodes_mutex;
    unordered_set<int> bridge_nodes; // 桥节点集合

    // // Helper: Build initial adjacency list from column-to-rows mapping
    void BuildAdjacencyList(const std::unordered_map<int, std::set<int>>& col_to_rows);
  
public:

    ComponentDetector(const std::unordered_map<int, std::set<int>>& col_to_rows, const int n, const int m);
  
    ~ComponentDetector() = default;

    // 辅助函数：规范化边的表示 (确保 u < v)
    pair<int, int> normalize_edge(int u, int v) const {
        return u < v ? make_pair(u, v) : make_pair(v, u);
    }

    void add_row(int r) {
        std::vector<int> cols_to_link;
        {
            std::unique_lock<std::shared_mutex> lk(active_rows_mutex);
            if (active_rows.find(r) != active_rows.end()) return; // 已活跃
            // 把 r 标记为活跃
            active_rows.insert(r);

            // 收集 r 对应的所有列（row_to_cols[r]）
            for (int col : row_to_cols[r]) {
            cols_to_link.push_back(num_rows + col); // 列节点 id = offset
            }
        }

        std::unique_lock<std::shared_mutex> ett_lk(ett_mutex);
        for (int col_node : cols_to_link) {
            // link r -- col_node
            ett->Link(r, col_node);
        }
    }

    void remove_row(int r) {
        std::vector<int> cols_to_cut;
        {
            std::unique_lock<std::shared_mutex> lk(active_rows_mutex);
            if (active_rows.find(r) == active_rows.end()) return;
            active_rows.erase(r);

            for (int col : row_to_cols[r]) {
            cols_to_cut.push_back(num_rows + col);
            }
        }


        std::unique_lock<std::shared_mutex> ett_lk(ett_mutex);
        for (int col_node : cols_to_cut) {
            ett->Cut(r, col_node);
        }
    }

    inline bool IsConnected(int u, int v) {
        shared_lock active_lock(active_rows_mutex);
        // 检查两个节点是否都是活跃的
        if (active_rows.find(u) == active_rows.end() || 
            active_rows.find(v) == active_rows.end()) {
            return false;
        }
        active_lock.unlock();
        
        shared_lock ett_lock(ett_mutex);
        return ett->IsConnected(u, v);
    }

    int GetComponentId(int r) {
        shared_lock active_lock(active_rows_mutex);
        if (active_rows.find(r) == active_rows.end()) {
            return -1;
        }
        active_lock.unlock();
        
        shared_lock ett_lock(ett_mutex);
        return ett->GetComponentId(r);
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

    // 并行找到所有桥梁节点
    void compute_all_bridges(const vector<Block>& components) {

        int n = components.size();

        // --- 并行处理每个 component ---
        #pragma omp parallel for schedule(dynamic)
        for (int idx = 0; idx < n; ++idx) {
            const auto& comp = components[idx];

            if (comp.rows.size() <= 2) continue; // 不可能有割点

            // 1. 为 rows 建 index: rowID -> (0..m-1)
            vector<int> nodes(comp.rows.begin(), comp.rows.end());
            unordered_map<int, int> id;
            id.reserve(nodes.size() * 2);
            for (int i = 0; i < (int)nodes.size(); ++i) {
                id[nodes[i]] = i;
            }

            int m = nodes.size();

            // 2. 构建邻接表
            vector<vector<int>> nbrs(m);

            {
                // 读 col_rows，需要上共享锁
                shared_lock lock(col_rows_mutex);
                for (int i = 0; i < m; ++i) {
                    int r = nodes[i];
                    const auto &cols = get_row_cols(r);
                    for (int c : cols) {
                        auto it = col_to_rows.find(c);
                        if (it == col_to_rows.end()) continue;
                        for (int other : it->second) {
                            auto jt = id.find(other);
                            if (jt != id.end()) {
                                int j = jt->second;
                                if (j != i) nbrs[i].push_back(j);
                            }
                        }
                    }
                }
            }

            // 3. bridge node detection: 对每个节点 i 检查删除 i 并删除其邻居后的连通性

            // 遍历每个节点
            for (int i = 0; i < m; ++i) {
                // 构造 removed 标志：包括 i 本身和它的直接邻居
                vector<char> removed(m, 0);
                removed[i] = 1;
                for (int nb : nbrs[i]) removed[nb] = 1;

                // 计算 remaining_count
                int remaining = 0;
                int start = -1;
                for (int t = 0; t < m; ++t) {
                    if (!removed[t]) {
                        remaining++;
                        if (start == -1) start = t;
                    }
                }

                if (remaining <= 1) {
                    // 剩余 <=1 个节点不可能分裂，继续下一个 i
                    continue;
                }

                // 从 start 做 DFS（跳过 removed 节点）
                int reached = 0;
                // 使用栈迭代以避免递归深度问题
                vector<int> stack;
                stack.reserve(remaining);
                stack.push_back(start);
                vector<char> seen(m, 0);
                seen[start] = 1;

                while (!stack.empty()) {
                    int u = stack.back(); stack.pop_back();
                    reached++;
                    for (int v : nbrs[u]) {
                        if (removed[v] || seen[v]) continue;
                        seen[v] = 1;
                        stack.push_back(v);
                    }
                }

                if (reached < remaining) {
                    unique_lock lock(bridge_nodes_mutex);
                    // 删除 i 及其冲突行会导致分裂
                    bridge_nodes.insert(nodes[i]); // 插入真实 row id
                    lock.unlock();
                }
            }

        } // end of parallel region

    }


    // 对子集进行全量检测
    vector<Block> detect_full_subset(const set<int>& rows) {
        if (rows.empty()) return {};
                
        std::unordered_set<int> active_snapshot;
        {
            std::shared_lock<std::shared_mutex> lk(active_rows_mutex);
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

    // 全量检测（用于首次检测）
    vector<Block> detect_full(const set<int>& block_rows, uint64_t thread_id, int depth = 0);

    // 增量检测：基于上次结果和变化的行进行增量更新
    vector<Block> detect_incremental(const set<int>& current_rows, 
                                     uint64_t thread_id,
                                     int parent_depth, 
                                     int current_depth);

    // 分配新的线程ID（主线程和子线程都需要调用）
    uint64_t allocate_thread_id() {
        return next_thread_id.fetch_add(1);
    }

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

#endif // COMPONENT_DETECTOR_H