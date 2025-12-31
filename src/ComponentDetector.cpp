#include "../include/ComponentDetector.h"

// 定义 thread_local 静态成员
thread_local std::vector<CoverHistory> ComponentDetector::cover_stack_;
thread_local std::vector<Block> ComponentDetector::block_cache_;
thread_local bool ComponentDetector::is_updated = false;

ComponentDetector::ComponentDetector(const int n, const int m) : num_rows(n),  num_cols(m), global_parent(n) {
    ett = std::make_unique<SplayETT>(n);
    
    // 初始化邻接表和激活状态
    adj_list.resize(n);
    row_active.resize(n, true);  // 初始时所有行都激活

    for (int i = 1; i <= m; ++i) {
        active_cols.insert(i);
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

    // col_to_rows 是静态的，这里不需要锁
    auto it = col_to_rows.find(c);

    const auto& rows = it->second;  // 获取与列 c 相连的所有行
    if (rows.size() < 2) return history;

    // 收集要删除的边
    std::vector<std::pair<int,int>> edges_to_delete;
    
    for (int row : rows) {
        if (!row_active[row]) continue;
        
        for (int neighbor : adj_list[row]) {
            if (!row_active[neighbor] || row > neighbor) continue;
            
            edges_to_delete.push_back({row, neighbor});
        }
        
        history.removed_rows.push_back(row);
    }
    
    // 使用增量式分解处理边删除
    auto affected_comps = IncDecomposeMatrix(edges_to_delete);
    
    // 标记行为非激活
    for (int row : history.removed_rows) {
        row_active[row] = false;
    }
    
    return history;
}

void ComponentDetector::DoUncover(const CoverHistory& history) {
    // 快速路径：空历史直接返回
    if (history.isEmpty()) {
        return;
    }

    unique_lock lock(ett_graph_mutex);
    // for (auto it = history.added_replacement_edges.rbegin(); 
    //      it != history.added_replacement_edges.rend(); ++it) {
    //     int u = it->first, v = it->second;
    //     unsigned long long key = makeEdgeKey(u, v);
        
    //     // 将替代边从树边降级为非树边
    //     ett->cut(u, v);
    //     tree_edges.erase(key);
    //     non_tree_edges.insert(key);
    // }

    for (auto& edge : history.cut_tree_edges) {
        int u = edge.first, v = edge.second;
        unsigned long long key = makeEdgeKey(u, v);
        
        // 恢复树边
        ett->link(u, v);
        tree_edges.insert(key);
    }
    
    for (auto& edge : history.removed_nontree_edges) {
        int u = edge.first, v = edge.second;
        unsigned long long key = makeEdgeKey(u, v);
        
        non_tree_edges.insert(key);
    }
    lock.unlock();
    
    // 恢复行的激活状态
    for (int row : history.removed_rows) {
        row_active[row] = true;
    }
}

// 增量式分解实现
std::vector<std::set<int>> ComponentDetector::IncDecomposeMatrix(
    const std::vector<std::pair<int,int>>& deleted_edges) {
    
    std::vector<std::set<int>> affected_components;
    std::vector<std::pair<int,int>> tree_edges_to_process;
    
    unique_lock lock(ett_graph_mutex);
    
    // Phase 1: 处理非树边(不影响连通性)
    for (const auto& [u, v] : deleted_edges) {
        unsigned long long key = makeEdgeKey(u, v);
        
        if (!edge_info_map.count(key)) continue;
        
        EdgeInfo& info = edge_info_map[key];
        
        if (!info.is_tree) {
            // 从分量的非树边集合中移除
            if (component_map.count(info.component_root)) {
                component_map[info.component_root].non_tree_edges.erase(key);
            }
            non_tree_edges.erase(key);
            edge_info_map.erase(key);
        } else {
            // 树边留待Phase 2处理
            tree_edges_to_process.push_back({u, v});
        }
    }
    
    // Phase 2: 处理树边(可能分裂分量)
    for (const auto& [u, v] : tree_edges_to_process) {
        unsigned long long key = makeEdgeKey(u, v);
        
        if (!edge_info_map.count(key)) continue;
        
        EdgeInfo& info = edge_info_map[key];
        int old_comp_root = info.component_root;
        int level = info.level;
        
        // 从旧分量中移除树边
        if (component_map.count(old_comp_root)) {
            component_map[old_comp_root].tree_edges.erase(key);
        }
        
        // Cut操作
        ett->cut(u, v);
        tree_edges.erase(key);
        
        // 查找替代边
        auto replacement = FindReplacementInComponent(u, v, old_comp_root);
        
        if (replacement.has_value()) {
            // 找到替代边,连通性保持
            int ru = replacement->first;
            int rv = replacement->second;
            unsigned long long repl_key = makeEdgeKey(ru, rv);
            
            // Link替代边
            ett->link(ru, rv);
            
            // 更新边信息:替代边从非树边升级为树边
            if (edge_info_map.count(repl_key)) {
                EdgeInfo& repl_info = edge_info_map[repl_key];
                repl_info.is_tree = true;
                repl_info.level = level;
                
                // 从非树边集合移到树边集合
                component_map[old_comp_root].non_tree_edges.erase(repl_key);
                component_map[old_comp_root].tree_edges.insert(repl_key);
                
                non_tree_edges.erase(repl_key);
                tree_edges.insert(repl_key);
            }
            
            edge_info_map.erase(key);
            
        } else {
            // 无替代边,分量分裂
            int root_u = ett->getComponentId(u);
            int root_v = ett->getComponentId(v);
            
            // 收集两个新分量的顶点
            std::set<int> comp_u, comp_v;
            
            ComponentInfo& old_comp = component_map[old_comp_root];
            for (int vertex : old_comp.vertices) {
                if (!row_active[vertex]) continue;
                
                int curr_root = ett->getComponentId(vertex);
                if (curr_root == root_u) {
                    comp_u.insert(vertex);
                } else if (curr_root == root_v) {
                    comp_v.insert(vertex);
                }
            }
            
            // 重新分配边到新分量
            auto redistribute_edges = [&](const std::unordered_set<unsigned long long>& edges,
                                          bool is_tree) {
                for (unsigned long long e_key : edges) {
                    int e_u = static_cast<int>(e_key >> 32);
                    int e_v = static_cast<int>(e_key & 0xFFFFFFFF);
                    
                    if (!row_active[e_u] || !row_active[e_v]) continue;
                    
                    int e_root = ett->getComponentId(e_u);
                    
                    if (edge_info_map.count(e_key)) {
                        edge_info_map[e_key].component_root = e_root;
                    }
                    
                    if (e_root == root_u) {
                        if (is_tree) component_map[root_u].tree_edges.insert(e_key);
                        else component_map[root_u].non_tree_edges.insert(e_key);
                    } else if (e_root == root_v) {
                        if (is_tree) component_map[root_v].tree_edges.insert(e_key);
                        else component_map[root_v].non_tree_edges.insert(e_key);
                    }
                }
            };
            
            // 创建新分量
            component_map[root_u].vertices =
                std::unordered_set<int>(comp_u.begin(), comp_u.end());

            component_map[root_v].vertices =
                std::unordered_set<int>(comp_v.begin(), comp_v.end());
            component_map[root_u].tree_edges.clear();
            component_map[root_u].non_tree_edges.clear();
            component_map[root_v].tree_edges.clear();
            component_map[root_v].non_tree_edges.clear();
            
            redistribute_edges(old_comp.tree_edges, true);
            redistribute_edges(old_comp.non_tree_edges, false);
            
            // 删除旧分量
            component_map.erase(old_comp_root);
            edge_info_map.erase(key);
            
            // 记录受影响的分量
            affected_components.push_back(comp_u);
            affected_components.push_back(comp_v);
        }
    }
    
    // 如果没有分量变化,返回整个图作为一个分量
    if (affected_components.empty()) {
        std::set<int> all_vertices;
        for (int i = 0; i < num_rows; i++) {
            if (row_active[i]) all_vertices.insert(i);
        }
        affected_components.push_back(all_vertices);
    }
    
    return affected_components;
}

// 在指定分量中查找替代边
std::optional<std::pair<int,int>> ComponentDetector::FindReplacementInComponent(
    int u, int v, int comp_root) {
    
    if (!component_map.count(comp_root)) return std::nullopt;
    
    int comp_u = ett->getComponentId(u);
    int comp_v = ett->getComponentId(v);
    
    if (comp_u == comp_v) return std::nullopt; // 仍然连通
    
    // 在较小的分量中搜索
    int size_u = ett->componentSize(u);
    int size_v = ett->componentSize(v);
    if (size_u > size_v) {
        std::swap(u, v);
        std::swap(comp_u, comp_v);
    }
    
    // 在分量的非树边中查找
    const auto& non_tree_edge_set = component_map[comp_root].non_tree_edges;
    
    for (unsigned long long key : non_tree_edge_set) {
        int e_u = static_cast<int>(key >> 32);
        int e_v = static_cast<int>(key & 0xFFFFFFFF);
        
        if (!row_active[e_u] || !row_active[e_v]) continue;
        
        int root_eu = ett->getComponentId(e_u);
        int root_ev = ett->getComponentId(e_v);
        
        // 检查是否连接两个分裂的分量
        if ((root_eu == comp_u && root_ev == comp_v) ||
            (root_eu == comp_v && root_ev == comp_u)) {
            return std::make_pair(e_u, e_v);
        }
    }
    
    return std::nullopt;
}

vector<Block> ComponentDetector::detect_blocks(const set<int> &block_rows) {
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
};

void ComponentDetector::validateETTState(const std::string& operation, int col) {
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
    std::set<int> active_rows;
    for (size_t i = 0; i < row_active.size(); i++) {
        if (row_active[i]) active_rows.insert(i);
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
