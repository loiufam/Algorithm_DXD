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

using comps = vector<Block>;

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
    std::vector<int> removed_rows;                
    
    // 分别记录树边和非树边的操作
    std::vector<std::pair<int, int>> cut_tree_edges;     // 被cut的树边
    std::vector<std::pair<int, int>> removed_nontree_edges; // 被删除的非树边
    
    // 记录替代边的操作（用于回滚）
    std::vector<std::pair<int, int>> added_replacement_edges;  // 新增的替代边
    
    void clear() {
        col = -1;
        removed_rows.clear();
        cut_tree_edges.clear();
        removed_nontree_edges.clear();
        added_replacement_edges.clear();
    }
    
    bool isEmpty() const {
        return removed_rows.empty();
    }
};

class ComponentDetector {
private:

    // 边的层级信息(用于分层搜索替代边)
    struct EdgeInfo {
        int level;           // 边的层级
        bool is_tree;        // 是否为树边
        int component_root;  // 所属连通分量的root节点
    };

    // 连通分量信息
    struct ComponentInfo {
        std::unordered_set<int> vertices;      // 分量中的顶点
        std::unordered_set<unsigned long long> tree_edges;     // 分量的树边
        std::unordered_set<unsigned long long> non_tree_edges; // 分量的非树边
    };

    std::unordered_map<unsigned long long, EdgeInfo> edge_info_map;  // edge_key -> EdgeInfo
    std::unordered_map<int, ComponentInfo> component_map;            // root -> ComponentInfo
    
    // 获取边所属的连通分量root
    int getEdgeComponentRoot(int u, int v) {
        std::shared_lock lock(ett_graph_mutex);
        return ett->getComponentId(u);
    }
    
    // 更新边的分量归属
    void updateEdgeComponent(unsigned long long key, int new_root) {
        if (edge_info_map.count(key)) {
            edge_info_map[key].component_root = new_root;
        }
    }

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
    std::unordered_set<unsigned long long> tree_edges;      // ETT中的实际树边
    std::unordered_set<unsigned long long> non_tree_edges;  // 逻辑存在但不在ETT中的边
    
    vector<bool> row_active;               // O(1)查询和修改
    std::unordered_set<int> active_cols;

    mutable std::vector<int> global_parent;
    mutable bool global_uf_valid = false;

    // 锁机制
    // ett_mutex 保护 ETT 实例以及 edge_columns 映射，因为它们是强耦合的
    mutable std::shared_mutex ett_graph_mutex; 
    
    // 保护矩阵状态 (row_to_cols, active_rows 等)
    mutable std::shared_mutex state_mutex;

    // 线程私有的历史栈
    static thread_local std::vector<CoverHistory> cover_stack_;

    static thread_local std::vector<Block> block_cache_;
    static thread_local bool need_rebuild; // 用于标记是否需要重新计算
    

    // 添加调试标志
    bool debug_mode = false;
    std::ofstream debug_log;

    inline unsigned long long makeEdgeKey(int u, int v) const {
        if (u > v) std::swap(u, v);
        return (static_cast<unsigned long long>(u) << 32) | 
               static_cast<unsigned long long>(v);
    }

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
            if (rows.size() < 2) continue;
            
            // 选择第一个行作为中心
            int center = rows[0];
            for (size_t i = 1; i < rows.size(); i++) {
                addEdgeToAdjList(center, rows[i]);
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

        std::cout << "Initial ETT graph built with " << edges.size() << " edges." << std::endl;

        std::vector<int> parent(num_rows);
        for (int i = 0; i < num_rows; i++) parent[i] = i;
        
        std::function<int(int)> find = [&](int x) {
            return parent[x] == x ? x : (parent[x] = find(parent[x]));
        };
        
        auto unite = [&](int x, int y) -> bool {
            x = find(x); y = find(y);
            if (x == y) return false;
            parent[y] = x;
            return true;
        };
        
        // 分类边
        std::vector<std::pair<int, int>> tree_edge_list;
        
        for (const auto& [u, v] : edges) {
                unsigned long long key = makeEdgeKey(u, v);
                
                if (unite(u, v)) {
                    // 树边
                    tree_edge_list.push_back({u, v});
                    tree_edges.insert(key);
                    edge_info_map[key] = {0, true, -1};  // level=0, is_tree=true
                } else {
                    // 非树边
                    non_tree_edges.insert(key);
                    edge_info_map[key] = {0, false, -1}; // level=0, is_tree=false
                }
            }
            
            ett->batchLink(tree_edge_list);
            
            // 初始化连通分量映射
            for (int i = 0; i < num_rows; i++) {
                if (row_active[i]) {
                    int root = ett->getComponentId(i);
                    component_map[root].vertices.insert(i);
                }
            }
            
            // 记录每条边所属的分量
            for (auto& [key, info] : edge_info_map) {
                int u = static_cast<int>(key >> 32);
                info.component_root = ett->getComponentId(u);
                
                if (info.is_tree) {
                    component_map[info.component_root].tree_edges.insert(key);
                } else {
                    component_map[info.component_root].non_tree_edges.insert(key);
                }
            }
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

    static thread_local bool is_updated;  // 标识图是否发生更新

    ComponentDetector(const int n, const int m);

    void Initialize(const std::unordered_map<int, std::vector<int>>& col_rows_map){
        col_to_rows = col_rows_map;
       
        // 构建 row_to_cols
        // if (debug_mode) debug_log << "printing intitial matrix state:" << std::endl;
        for (const auto& [col, rows] : col_to_rows) {
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
        if (!is_updated) return {};

        // 构造当前分量

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

    // 增量式分解矩阵 - 返回受影响的连通分量
    std::vector<std::set<int>> IncDecomposeMatrix(
        const std::vector<std::pair<int,int>>& deleted_edges);
    
    // 在指定分量中查找替代边
    std::optional<std::pair<int,int>> FindReplacementInComponent(
        int u, int v, int comp_root);
    
    vector<Block> detect_blocks(const set<int>& block_rows);

    // 验证ETT状态的一致性
    void validateETTState(const std::string& operation, int col = -1);

};

#endif // COMPONENTDETECTOR_H