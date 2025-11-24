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

private:

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

        vector<pair<int,int>> edges;
        for (int u = 0; u < num_rows; u++) {
            for (int v : adj_list[u]) {
                if (v > u) {  // 只添加一次
                    edges.push_back({u, v});
                }
            }
        }

        ett->batchLink(edges);
    }

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

    // 内部cover/uncover操作
    CoverHistory DoCover(int c);
    void DoUncover(const CoverHistory& history);

public:

    ComponentDetector(const int n, const int m);

    void Initialize(const std::unordered_map<int, std::vector<int>>& col_rows_map){
        col_to_rows = col_rows_map;
       
        // 构建 row_to_cols
        for (const auto& [col, rows] : col_to_rows) {
            for (int row : rows) {
                row_to_cols[row].insert(col);
            }
        }
        BuildInitialGraph();
    };
  
    ~ComponentDetector() = default;


    void Cover(int c);
    
    void Uncover();

    // 在特定行集合内部寻找分块(统一对外接口)
    std::vector<Block> GetBlocks(const std::set<int>& block_rows) {
        // return detect_by_ett(block_rows);
        return detect_by_uf(block_rows);
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
    vector<Block> detect_by_uf(const set<int>& block_rows);


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

};

#endif // COMPONENTDETECTOR_H