#include "../include/ComponentDetector.h"

// 定义 thread_local 静态成员
thread_local std::vector<CoverHistory> ComponentDetector::cover_stack_;

ComponentDetector::ComponentDetector(const int n, const int m) : num_rows(n),  num_cols(m), global_parent(n) {
    ett = std::make_unique<SplayETT>();
    for (int i = 0; i < n; i++) {
        ett->make_vertex(i);
    }
    std::iota(global_parent.begin(), global_parent.end(), 0);
}

void ComponentDetector::Cover(int c) {
    // 执行 Cover 操作并获取历史记录
    CoverHistory history = DoCover(c);
    
    // 压入线程私有栈
    cover_stack_.push_back(std::move(history));
}

void ComponentDetector::Uncover() {
    // 检查栈是否为空
    if (cover_stack_.empty()) {
        throw std::runtime_error("Cannot uncover: cover stack is empty");
    }
    
    // 取出栈顶元素
    CoverHistory history = std::move(cover_stack_.back());
    cover_stack_.pop_back();
    
    // 执行 Uncover 操作
    DoUncover(history);
}

CoverHistory ComponentDetector::DoCover(int c) {
    CoverHistory history;
    history.col = c;

    // 1. 快速检查列是否有效 (读锁)
    {
        std::shared_lock<std::shared_mutex> lock(state_mutex);
        if (active_cols.find(c) == active_cols.end()) {
            return history; // 无效操作
        }
    }

    // 获取该列包含的行 (读锁，col_to_rows 初始化后不变)
    // col_to_rows 是静态的，这里不需要锁
    const auto& rows = col_to_rows.at(c); 

    // 2. 更新图连通性 (写锁)
    // 必须锁住 ETT 和 edge_columns，防止并发修改导致状态不一致
    {
        std::unique_lock<std::shared_mutex> lock(ett_graph_mutex);
        
        // 遍历该列产生的所有边
        for (size_t i = 0; i < rows.size(); i++) {
            for (size_t j = i + 1; j < rows.size(); j++) {
                int r1 = rows[i], r2 = rows[j];
                if (r1 > r2) std::swap(r1, r2);
                auto edge = std::make_pair(r1, r2);

                history.edges_affected.push_back(edge);
                
                auto& cols = edge_columns[edge];
                cols.erase(c); // 移除该列对边的贡献
                
                if (cols.empty()) {
                    // 如果没有列支撑这条边了，则断开
                    edge_columns.erase(edge); // 节省内存
                    history.edges_removed.push_back(edge);
                    ett->cut(r1, r2);
                    ett->remove_vertex(r1);
                    ett->remove_vertex(r2);
                }
            }
        }
    } // 释放 ett 锁

    // 3. 更新矩阵状态 (写锁)
    {
        std::unique_lock<std::shared_mutex> lock(state_mutex);
        active_cols.erase(c);
        
        for (int r : rows) {
            row_to_cols[r].erase(c);
            if (row_to_cols[r].empty()) {
                active_rows.erase(r);
                history.rows_deactivated.push_back(r);
            }
        }
    }

    return history;
}

void ComponentDetector::DoUncover(const CoverHistory& history) {
    int c = history.col;
    // 如果 history 为空（例如 Cover 时列不活跃），直接返回
    if (history.edges_affected.empty() && history.rows_deactivated.empty() && history.edges_removed.empty()) {
        bool was_active;
        {
            std::shared_lock<std::shared_mutex> lock(state_mutex);
            was_active = active_cols.count(c);
        }
        if(was_active) return; // 并没有真正执行过 Cover
    }

    // 1. 恢复 ETT 和 边数据 (写锁)
    {
        std::unique_lock<std::shared_mutex> lock(ett_graph_mutex);
        
        // 恢复 edge_columns
        for (const auto& edge : history.edges_affected) {
            edge_columns[edge].insert(c);
        }
        
        // 恢复被切断的边
        // 注意：必须按与 Cut 相反的顺序或任意顺序 Link 均可，因为图是无向的
        for (const auto& edge : history.edges_removed) {
            ett->make_vertex(edge.first);
            ett->make_vertex(edge.second);
            ett->link(edge.first, edge.second);
        }
    }

    // 2. 恢复矩阵状态 (写锁)
    {
        std::unique_lock<std::shared_mutex> lock(state_mutex);
        
        for (int r : history.rows_deactivated) {
            active_rows.insert(r);
        }
        
        // 这里的 col_to_rows 需要保证只读或受保护
        if (col_to_rows.count(c)) {
            for (int r : col_to_rows.at(c)) {
                row_to_cols[r].insert(c);
            }
        }
        active_cols.insert(c);
    }
}

vector<Block> ComponentDetector::detect_in_block(const set<int>& block_rows){
    if (block_rows.empty()) return {};
    
    // 1. 构建并查集
    UnionFind uf;
    std::unordered_set<int> active_set(block_rows.begin(), block_rows.end());
    
    for (int row : block_rows) {
        uf.make_set(row);
    }
    
    // 2. 合并连通的节点 - 只需遍历一次边
    for (int u : block_rows) {
        for (int v : adj_list[u]) {
            if (active_set.count(v)) {
                uf.unite(u, v);
            }
        }
    }
    
    // 3. 按根节点分组
    std::unordered_map<int, vector<int>> components;
    for (int row : block_rows) {
        int root = uf.find(row);
        components[root].push_back(row);
    }
    
    // 4. 构建结果
    vector<Block> result;
    result.reserve(components.size());
    
    for (auto& [root, component] : components) {
        set<int> rows_set(component.begin(), component.end());
        set<int> cols_set;
        
        for (int r : component) {
            const auto& cols = row_to_cols[r];
            cols_set.insert(cols.begin(), cols.end());
        }
        
        result.emplace_back(std::move(rows_set), std::move(cols_set));
    }
    
    return result;
};

vector<Block> ComponentDetector::detect_by_ett(const set<int>& block_rows){
    if (block_rows.empty()) return {}; 

    unordered_map<int, set<int>> components_group;
    for (int row : block_rows) {
        int comp_id = GetComponentId(row);
        if (comp_id != -1) {
            components_group[comp_id].insert(row);
        }
    }

    vector<Block> result;
    for (auto& [comp_id, rows] : components_group) {
        set<int> cols_set;
        for (int row : rows) {
            const auto& cols = row_to_cols[row];
            cols_set.insert(cols.begin(), cols.end());
        }
        result.emplace_back(std::move(rows), std::move(cols_set));
    }

    return result;
};

