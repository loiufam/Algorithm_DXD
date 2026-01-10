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
#include <algorithm>

using comps = vector<Block>;

// 分层非树边存储结构
struct LayeredNonTreeEdges {
    std::vector<std::unordered_set<unsigned long long>> levels;  // levels[i] = 第i层的非树边集合
    std::unordered_map<unsigned long long, int> edge_to_level;   // 边 -> 当前所在层级
    int max_level;
    
    LayeredNonTreeEdges(int max_lvl) : max_level(max_lvl) {
        levels.resize(max_lvl + 1);
    }
    
    // 添加边到指定层级
    void addEdge(unsigned long long key, int level) {
        if (level < 0 || level > max_level) return;
        levels[level].insert(key);
        edge_to_level[key] = level;
    }
    
    // 删除边
    void removeEdge(unsigned long long key) {
        auto it = edge_to_level.find(key);
        if (it != edge_to_level.end()) {
            int level = it->second;
            levels[level].erase(key);
            edge_to_level.erase(it);
        }
    }
    
    // 降级边到下一层
    bool demoteEdge(unsigned long long key) {
        auto it = edge_to_level.find(key);
        if (it == edge_to_level.end()) return false;
        
        int curr_level = it->second;
        if (curr_level <= 0) return false;  // 已经在最底层
        
        levels[curr_level].erase(key);
        levels[curr_level - 1].insert(key);
        edge_to_level[key] = curr_level - 1;
        return true;
    }
    
    // 获取边的当前层级
    int getLevel(unsigned long long key) const {
        auto it = edge_to_level.find(key);
        return (it != edge_to_level.end()) ? it->second : -1;
    }
    
    // 获取指定层级的所有边
    const std::unordered_set<unsigned long long>& getEdgesAtLevel(int level) const {
        static const std::unordered_set<unsigned long long> empty_set;
        if (level < 0 || level > max_level) return empty_set;
        return levels[level];
    }
    
    void clear() {
        for (auto& level : levels) {
            level.clear();
        }
        edge_to_level.clear();
    }
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

// 连通分量信息（线程私有）
struct ComponentInfo {
    std::unordered_set<int> vertices;
    std::unordered_set<unsigned long long> tree_edges;
    std::unique_ptr<LayeredNonTreeEdges> non_tree_edges;
    int tree_root;
    
    ComponentInfo() : tree_root(-1) {}
};

// 用于保存 Cover 操作的历史记录（线程私有）
struct CoverHistory {
    int col;
    std::set<int> prev_rows;  // Cover前的行集合
    std::vector<int> removed_rows;  // 被删除的行
    
    // ETT操作记录（用于回滚）
    std::vector<std::pair<int, int>> cut_tree_edges;
    std::vector<std::pair<int, int>> removed_nontree_edges;
    std::vector<std::pair<int, int>> added_replacement_edges;
    
    // 连通分量信息
    std::vector<std::set<int>> prev_components;  // Cover前的连通分量
    std::vector<std::set<int>> new_components;   // Cover后的连通分量
    
    void clear() {
        col = -1;
        prev_rows.clear();
        removed_rows.clear();
        cut_tree_edges.clear();
        removed_nontree_edges.clear();
        added_replacement_edges.clear();
        prev_components.clear();
        new_components.clear();
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

    std::unordered_map<unsigned long long, EdgeInfo> edge_info_map;  // edge_key -> EdgeInfo
    ComponentInfo component_info;  // 单个分量信息（线程私有，无需锁）
    // std::unordered_map<int, ComponentInfo> component_map;            // root -> ComponentInfo
    
    // std::vector<std::set<int>> last_affected_components_; // 缓存最近一次分解的结果

    // 获取边所属的连通分量root
    // int getEdgeComponentRoot(int u, int v) {
    //     std::shared_lock lock(ett_graph_mutex);
    //     return ett->getComponentId(u);
    // }
    
    // 更新边的分量归属
    // void updateEdgeComponent(unsigned long long key, int new_root) {
    //     if (edge_info_map.count(key)) {
    //         edge_info_map[key].component_root = new_root;
    //     }
    // }

    unique_ptr<SplayETT> ett;

    // ===============================================================
    // Dancing Links
    // ===============================================================
    int num_rows;
    int num_cols;

    // 列到行的映射（静态，初始化后不变）
    std::unordered_map<int, std::vector<int>> col_to_rows;
    std::unordered_map<int, std::unordered_set<int>> row_to_cols;

    // 边与列的映射：Edge(u, v) -> {Columns supporting this edge}
    // std::map<std::pair<int, int>, std::unordered_set<int>> edge_columns;

    // 图的邻接表
    vector<unordered_set<int>> adj_list;  // adj_list[row] = {邻居}
    std::unordered_set<unsigned long long> tree_edges;      // ETT中的实际树边
    std::unordered_set<unsigned long long> non_tree_edges;  // 逻辑存在但不在ETT中的边
    
    vector<bool> row_active;               // O(1)查询和修改
    std::unordered_set<int> active_cols;

    // 锁机制
    // ett_mutex 保护 ETT 实例以及 edge_columns 映射，因为它们是强耦合的
    // mutable std::shared_mutex ett_graph_mutex; 
    
    // 保护矩阵状态 (row_to_cols, active_rows 等)
    // mutable std::shared_mutex state_mutex;

    // 线程私有的历史栈
    static thread_local std::vector<CoverHistory> cover_stack_;

    // 线程私有的状态追踪（无需锁）
    static thread_local std::set<int> current_rows_;      // 当前活跃行集合
    static thread_local std::vector<std::set<int>> current_components_;  // 当前连通分量

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

    // 计算分层的最大层级
    int calculateMaxLevel(int num_vertices) {
        if (num_vertices <= 1) return 0;
        return static_cast<int>(std::ceil(std::log2(num_vertices)));
    }

    // BFS构建生成森林
    void BuildSpanningForest();

    // 减量式生成连通分量 - 返回受影响的连通分量
    std::vector<std::set<int>> DecGenerateCC(
        const std::set<int>& deleted_vertices,
        const std::set<int>& prev_component);
    
    // 在指定分量中查找替代边
    // std::optional<std::pair<int,int>> FindReplacementInComponent(
    //     int u, int v, int comp_root);

    // 分层查找替代边 - 从高层向低层查找
    std::optional<std::pair<int,int>> FindReplacementLayered(int comp_u, int comp_v);

    std::vector<Block> convertComponentsToBlocks(const std::vector<std::set<int>>& components);

public:

    // static thread_local bool is_updated;

    ComponentDetector(const int n, const int m);
    ~ComponentDetector() = default;

    void Initialize(const std::unordered_map<int, std::vector<int>>& col_rows_map);
  
    void enableDebug(const std::string& log_file = "../logs/ett_debug.log") {
        debug_mode = true;
        debug_log.open(log_file);
        debug_log << "=== ETT Debug Log ===" << std::endl;
    }

    void Cover(int c);
    void Uncover();

    // 在特定行集合内部寻找分块(统一对外接口)
    std::vector<Block> GetBlocks(const std::set<int>& block_rows);
  
    // 工具函数
    bool IsConnected(int u, int v) const {
        return ett->connected(u, v);
    }

    int GetComponentId(int u) const {
        return ett->getComponentId(u);
    }

    vector<Block> detect_blocks(const set<int>& block_rows);

};

#endif // COMPONENTDETECTOR_H