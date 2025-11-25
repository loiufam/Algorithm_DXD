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
#include <thread>
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
    int col;                                      // 被cover的列
    std::vector<int> removed_rows;                 // 被删除的行
    std::vector<std::pair<int, int>> cut_edges;   // 被cut的边 (保证 u < v)
    
    void clear() {
        col = -1;
        removed_rows.clear();
        cut_edges.clear();
    }
    
    bool isEmpty() const {
        return removed_rows.empty();
    }
};

class ComponentDetector {
private:

    unique_ptr<SplayETT> ett;

    // ===============================================================
    // Dancing Links
    // ===============================================================
    int num_rows;
    int num_cols;

    // 列到行的映射（静态，初始化后不变）
    std::unordered_map<int, std::vector<int>> col_to_rows;

    // 动态状态
    std::unordered_map<int, std::unordered_set<int>> row_to_cols;
    // 边与列的映射：Edge(u, v) -> {Columns supporting this edge}
    std::map<std::pair<int, int>, std::unordered_set<int>> edge_columns;

    // 图的邻接表
    vector<unordered_set<int>> adj_list;  // adj_list[row] = {邻居}

    // Track which rows are currently active (not removed)
    vector<bool> row_active;               // O(1)查询和修改
    std::unordered_set<int> active_cols;

    // Union-Find 结构（用于快速分组）
    mutable std::vector<int> global_parent;
    mutable bool global_uf_valid = false;

    // 锁机制
    // ett_mutex 保护 ETT 实例以及 edge_columns 映射，因为它们是强耦合的
    mutable std::shared_mutex ett_graph_mutex; 
    
    // 保护矩阵状态 (row_to_cols, active_rows 等)
    mutable std::shared_mutex state_mutex;

    // 线程私有的历史栈
    static thread_local std::vector<CoverHistory> cover_stack_;

    // 历史记录对象池
    static thread_local std::vector<CoverHistory> history_pool_;
    
    // 线程上下文管理
    mutable shared_mutex thread_ctx_mutex;
    unordered_map<uint64_t, shared_ptr<ThreadContext>> thread_contexts; // thread_id -> context
    atomic<uint64_t> next_thread_id{1};  // 自增的线程ID分配器

    // 添加调试标志
    bool debug_mode = false;
    std::ofstream debug_log;

    /**
     * 添加一条边到邻接表（双向）
     */
    void addEdgeToAdjList(int u, int v) {
        if (u == v) return;
        adj_list[u].insert(v);
        adj_list[v].insert(u);
    }
    
    /**
     * 从邻接表中删除一条边（双向）
     */
    void removeEdgeFromAdjList(int u, int v) {
        if (u == v) return;
        adj_list[u].erase(v);
        adj_list[v].erase(u);
    }

    void BuildInitialGraph() {
        
        // 构建邻接表和 ETT
        for (const auto& [col, rows] : col_to_rows) {
            for (size_t i = 0; i < rows.size(); i++) {
                for (size_t j = i + 1; j < rows.size(); j++) {
                    addEdgeToAdjList(rows[i], rows[j]);
                }
            }
        }

        // if (debug_mode) {
        //     debug_log << "initial adjacency list:" << std::endl;
        // }

        vector<pair<int,int>> edges;
        for (int u = 0; u < num_rows; u++) {
            for (int v : adj_list[u]) {
                if (v > u) {  // 只添加一次
                    edges.push_back({u, v});
                    // if (debug_mode) {
                    //     debug_log << " Add Edge (" << u << ", " << v << ")" << std::endl;
                    // }
                }
            }
        }

        std::cout << "Initial ETT graph built with " << edges.size() << " edges." << std::endl;
        ett->batchLinkFast(edges);
        // if (debug_mode) ett->debugPrintEdges(debug_log);
    }

    void BuildInitialGraphParallel() {
        const int num_threads = std::thread::hardware_concurrency();
        
        // 将列分成多个批次
        std::vector<std::vector<std::pair<int, std::vector<int>>>> batches(num_threads);
        int idx = 0;
        for (const auto& col_rows : col_to_rows) {
            batches[idx % num_threads].push_back(col_rows);
            idx++;
        }
        
        // 每个线程的局部边集合
        std::vector<std::vector<std::pair<int,int>>> thread_edges(num_threads);
        std::vector<std::unordered_set<uint64_t>> thread_edge_sets(num_threads);
        
        // 并行处理
        std::vector<std::thread> threads;
        for (int t = 0; t < num_threads; t++) {
            threads.emplace_back([&, t]() {
                auto& local_edges = thread_edges[t];
                auto& local_edge_set = thread_edge_sets[t];
                
                for (const auto& [col, rows] : batches[t]) {
                    for (size_t i = 0; i < rows.size(); i++) {
                        for (size_t j = i + 1; j < rows.size(); j++) {
                            int u = rows[i], v = rows[j];
                            if (u > v) std::swap(u, v);
                            
                            uint64_t edge_key = (static_cast<uint64_t>(u) << 32) | v;
                            if (local_edge_set.insert(edge_key).second) {
                                local_edges.push_back({u, v});
                            }
                        }
                    }
                }
            });
        }
        
        // 等待所有线程完成
        for (auto& thread : threads) {
            thread.join();
        }
        
        // 合并结果（需要再次去重）
        std::unordered_set<uint64_t> final_edge_set;
        std::vector<std::pair<int,int>> edges;
        
        for (int t = 0; t < num_threads; t++) {
            for (const auto& [u, v] : thread_edges[t]) {
                uint64_t edge_key = (static_cast<uint64_t>(u) << 32) | v;
                if (final_edge_set.insert(edge_key).second) {
                    edges.push_back({u, v});
                    adj_list[u].insert(v);
                    adj_list[v].insert(u);
                }
            }
        }
        
        std::cout << "Initial ETT graph built with " << edges.size() << " edges (parallel)." << std::endl;
        ett->batchLink(edges);
    }

    // 内部cover/uncover操作
    CoverHistory DoCover(int c);
    void DoUncover(const CoverHistory& history);

public:

    ComponentDetector(const int n, const int m);

    void Initialize(const std::unordered_map<int, std::vector<int>>& col_rows_map){
        col_to_rows = col_rows_map;
       
        // 构建 row_to_cols
        // if (debug_mode) debug_log << "printing intitial matrix state:" << std::endl;
        for (const auto& [col, rows] : col_to_rows) {
            // if (debug_mode) {
            //     debug_log << "Column " << col << " has rows: { ";
            //     for (int row : rows) {
            //         debug_log << row << " ";
            //     }
            //     debug_log << "}" << std::endl;
            // }
            for (int row : rows) {
                row_to_cols[row].insert(col);
            }
        }
        // if (debug_mode) debug_log << "=== End of initial matrix state ===" << std::endl;
        BuildInitialGraph();
    };
  
    ~ComponentDetector() = default;

    void enableDebug(const std::string& log_file = "../logs/ett_debug.log") {
        debug_mode = true;
        debug_log.open(log_file);
        debug_log << "=== ETT Debug Log ===" << std::endl;
    }

    void Cover(int c);
    
    void Uncover();

    // 在特定行集合内部寻找分块(统一对外接口)
    std::vector<Block> GetBlocks(const std::set<int>& block_rows) {
        // return detect_by_ett(block_rows);
        return detect_blocks(block_rows);
    }

    // 此函数需要独占锁，因为内部有 Splay 操作
    bool IsConnected(int u, int v) const {
        std::unique_lock<std::shared_mutex> lock(ett_graph_mutex);
        return ett->connected(u, v);
    }

    int GetComponentId(int u) const {
        std::shared_lock<std::shared_mutex> lock(ett_graph_mutex);
        return ett->getComponentId(u);
    }

    vector<Block> detect_by_ett(const set<int>& block_rows);

    // 增量检测：基于上次结果和变化的行进行增量更新
    vector<Block> detect_blocks(const set<int>& block_rows);

    // 验证ETT状态的一致性
    void validateETTState(const std::string& operation, int col = -1) {
        if (!debug_mode) return;
        
        debug_log << "\n--- After " << operation;
        if (col >= 0) debug_log << " (col=" << col << ")";
        debug_log << " ---" << std::endl;
        
        // 1. 统计激活的行数
        int active_count = 0;
        for (size_t i = 0; i < row_active.size(); i++) {
            if (row_active[i]) active_count++;
        }
        debug_log << "Active rows: " << active_count << std::endl;
        
        // 2. 检查每对激活行之间的连通性
        std::vector<int> active_rows;
        for (size_t i = 0; i < row_active.size(); i++) {
            if (row_active[i]) active_rows.push_back(i);
        }
        
        // 3. 验证图的连通分量
        auto components = ett->batchGroupByComponent(active_rows);
        debug_log << "ETT reports " << components.size() << " components:" << std::endl;
        
        for (auto& [comp_id, rows] : components) {
            debug_log << "  Component " << comp_id << ": {";
            for (size_t i = 0; i < rows.size(); i++) {
                if (i > 0) debug_log << ", ";
                debug_log << rows[i];
            }
            debug_log << "}" << std::endl;
        }
        
        // 4. 手动验证连通性（通过邻接表DFS）
        std::vector<bool> visited(row_active.size(), false);
        std::vector<std::vector<int>> manual_components;
        
        for (int start : active_rows) {
            if (visited[start]) continue;
            
            std::vector<int> component;
            std::vector<int> stack = {start};
            visited[start] = true;
            
            while (!stack.empty()) {
                int u = stack.back();
                stack.pop_back();
                component.push_back(u);
                
                for (int v : adj_list[u]) {
                    if (row_active[v] && !visited[v]) {
                        visited[v] = true;
                        stack.push_back(v);
                    }
                }
            }
            
            std::sort(component.begin(), component.end());
            manual_components.push_back(component);
        }
        
        debug_log << "Manual DFS finds " << manual_components.size() 
                  << " components:" << std::endl;
        
        for (size_t i = 0; i < manual_components.size(); i++) {
            debug_log << "  Component " << i << ": {";
            for (size_t j = 0; j < manual_components[i].size(); j++) {
                if (j > 0) debug_log << ", ";
                debug_log << manual_components[i][j];
            }
            debug_log << "}" << std::endl;
        }
        
        // 5. 比较结果
        if (components.size() != manual_components.size()) {
            debug_log << "❌ MISMATCH: ETT reports " << components.size() 
                      << " components but DFS finds " << manual_components.size() 
                      << std::endl;
        } else {
            debug_log << "✓ Component count matches" << std::endl;
        }
        
        debug_log.flush();
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

private:

    void RebuildGlobalUnionFind() const {
        if (global_uf_valid) return;
        
        global_parent.resize(num_rows);
        std::iota(global_parent.begin(), global_parent.end(), 0);
        
        for (const auto& [edge, cols] : edge_columns) {
            int r1 = edge.first;
            int r2 = edge.second;
            
            if (row_active[r1] && row_active[r2]) {
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

};

#endif // COMPONENTDETECTOR_H