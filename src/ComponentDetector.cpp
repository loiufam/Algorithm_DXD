#include "../include/ComponentDetector.h"

// 定义 thread_local 静态成员
thread_local std::vector<CoverHistory> ComponentDetector::cover_stack_;
thread_local std::set<int> ComponentDetector::current_rows_;
thread_local std::vector<std::set<int>> ComponentDetector::current_components_;

ComponentDetector::ComponentDetector(const int n, const int m) 
    : num_rows(n),  num_cols(m) {

    ett = std::make_unique<SplayETT>(n);

    // 初始化邻接表和激活状态
    adj_list.resize(n);
    row_active.resize(n, true);  // 初始时所有行都激活

    for (int i = 1; i <= m; ++i) {
        active_cols.insert(i);
    }

}

void ComponentDetector::Initialize(const std::unordered_map<int, std::vector<int>>& col_rows_map) {
    col_to_rows = col_rows_map;
   
    // 构建 row_to_cols 和邻接表
    for (const auto& [col, rows] : col_to_rows) {
        for (int row : rows) {
            row_to_cols[row].insert(col);
        }
        
        // 构建邻接表（星形连接）
        if (rows.size() >= 2) {
            int center = rows[0];
            for (size_t i = 1; i < rows.size(); i++) {
                addEdgeToAdjList(center, rows[i]);
            }
        }
    }
    
    // BFS构建生成森林
    BuildSpanningForest();
    
    // 初始化线程私有状态
    current_rows_.clear();
    for (int i = 0; i < num_rows; i++) {
        if (row_active[i]) {
            current_rows_.insert(i);
        }
    }
    
    // 初始化当前连通分量
    if (component_info.vertices.size() == static_cast<size_t>(num_rows)) {
        // 单分量
        current_components_.clear();
        current_components_.push_back(current_rows_);
    }
}

void ComponentDetector::BuildSpanningForest() {
    std::vector<bool> visited(num_rows, false);
    
    // 收集所有边
    std::vector<std::pair<int, int>> all_edges;
    for (int u = 0; u < num_rows; u++) {
        if (!row_active[u]) continue;
        for (int v : adj_list[u]) {
            if (v > u && row_active[v]) {
                all_edges.push_back({u, v});
            }
        }
    }
    
    std::cout << "Total edges in graph: " << all_edges.size() << std::endl;
    
    // 用于记录找到的连通分量
    std::vector<std::set<int>> initial_components;
    
    // 对每个连通分量进行BFS
    for (int start = 0; start < num_rows; start++) {
        if (!row_active[start] || visited[start]) continue;
        
        // BFS构建这个分量的生成树
        std::queue<int> q;
        std::vector<int> component_vertices;
        std::vector<std::pair<int, int>> tree_edge_list;
        std::unordered_set<unsigned long long> all_edges_in_comp;
        
        q.push(start);
        visited[start] = true;
        component_vertices.push_back(start);
        
        while (!q.empty()) {
            int u = q.front();
            q.pop();
            
            for (int v : adj_list[u]) {
                if (!row_active[v]) continue;
                
                unsigned long long key = makeEdgeKey(u, v);
                all_edges_in_comp.insert(key);
                
                if (!visited[v]) {
                    visited[v] = true;
                    component_vertices.push_back(v);
                    tree_edge_list.push_back({u, v});
                    q.push(v);
                }
            }
        }
        
        // 记录这个连通分量
        std::set<int> comp_set(component_vertices.begin(), component_vertices.end());
        initial_components.push_back(comp_set);
        
        // 计算该分量的最大层级
        int max_level = calculateMaxLevel(component_vertices.size());
        
        // Link树边到ETT
        ett->batchLink(tree_edge_list);
        
        // 更新树边信息
        for (const auto& [u, v] : tree_edge_list) {
            unsigned long long key = makeEdgeKey(u, v);
            tree_edges.insert(key);
            edge_info_map[key] = {max_level, true};
        }
        
        // 识别非树边并添加到最高层
        for (unsigned long long key : all_edges_in_comp) {
            if (tree_edges.find(key) == tree_edges.end()) {
                non_tree_edges.insert(key);
                edge_info_map[key] = {max_level, false};
            }
        }
        
        // std::cout << "Component starting from " << start << ": " 
        //           << component_vertices.size() << " vertices, "
        //           << tree_edge_list.size() << " tree edges, "
        //           << (all_edges_in_comp.size() - tree_edge_list.size()) << " non-tree edges, "
        //           << "max level = " << max_level << std::endl;
    }
    
    // 如果只有一个分量，初始化component_info
    if (initial_components.size() == 1) {
        component_info.tree_root = 0;
        component_info.vertices = std::unordered_set<int>(
            initial_components[0].begin(), 
            initial_components[0].end()
        );
        
        int max_level = calculateMaxLevel(component_info.vertices.size());
        component_info.non_tree_edges = std::make_unique<LayeredNonTreeEdges>(max_level);
        
        // 将所有非树边加入分层结构
        for (unsigned long long key : non_tree_edges) {
            component_info.non_tree_edges->addEdge(key, max_level);
        }
        
        for (unsigned long long key : tree_edges) {
            component_info.tree_edges.insert(key);
        }
        
        current_components_ = initial_components;
    } else {
        // 多分量情况，current_components_会在GetBlocks首次调用时设置
        current_components_ = initial_components;
    }
    
    // std::cout << "Initial forest has " << initial_components.size() 
    //           << " connected component(s)" << std::endl;
}

void ComponentDetector::Cover(int c) {
    CoverHistory history;
    history.col = c;
    
    // 保存Cover前的状态
    history.prev_rows = current_rows_;
    history.prev_components = current_components_;
    
    // 收集要删除的行
    auto it = col_to_rows.find(c);
    if (it != col_to_rows.end()) {
        const auto& rows = it->second;
        for (int row : rows) {
            if (row_active[row]) {
                history.removed_rows.push_back(row);
            }
        }
    }
    
    // 标记行为非激活（不执行增量算法，留到GetBlocks）
    for (int row : history.removed_rows) {
        row_active[row] = false;
        current_rows_.erase(row);
    }
    
    // 压入历史栈
    cover_stack_.push_back(std::move(history));
}

void ComponentDetector::Uncover() {
    if (cover_stack_.empty()) {
        throw std::runtime_error("Cannot uncover: cover stack is empty");
    }
    
    CoverHistory history = std::move(cover_stack_.back());
    cover_stack_.pop_back();
    
    if (history.isEmpty()) return;
    
    // 恢复替代边（降级回非树边）
    for (auto it = history.added_replacement_edges.rbegin(); 
         it != history.added_replacement_edges.rend(); ++it) {
        int u = it->first, v = it->second;
        unsigned long long key = makeEdgeKey(u, v);
        
        ett->cut(u, v);
        tree_edges.erase(key);
        non_tree_edges.insert(key);
        
        if (edge_info_map.count(key)) {
            EdgeInfo& info = edge_info_map[key];
            info.is_tree = false;
            
            if (component_info.non_tree_edges) {
                component_info.non_tree_edges->addEdge(key, info.level);
            }
        }
        
        component_info.tree_edges.erase(key);
    }

    // 恢复树边
    for (auto it = history.cut_tree_edges.rbegin(); 
         it != history.cut_tree_edges.rend(); ++it) {
        int u = it->first, v = it->second;
        unsigned long long key = makeEdgeKey(u, v);
        
        ett->link(u, v);
        tree_edges.insert(key);
        component_info.tree_edges.insert(key);
        
        if (edge_info_map.count(key)) {
            edge_info_map[key].is_tree = true;
        }
    }
    
    // 恢复非树边
    for (auto& edge : history.removed_nontree_edges) {
        int u = edge.first, v = edge.second;
        unsigned long long key = makeEdgeKey(u, v);
        
        non_tree_edges.insert(key);
        
        if (edge_info_map.count(key)) {
            EdgeInfo& info = edge_info_map[key];
            info.is_tree = false;
            
            if (component_info.non_tree_edges) {
                component_info.non_tree_edges->addEdge(key, info.level);
            }
        }
    }
    
    // 恢复行的激活状态
    for (int row : history.removed_rows) {
        row_active[row] = true;
    }
    
    // 恢复线程私有状态
    current_rows_ = history.prev_rows;
    current_components_ = history.prev_components;
}

std::vector<Block> ComponentDetector::GetBlocks(const std::set<int>& block_rows) {
    // 首次调用或初始化后
    // if (cover_stack_.empty()) {
        // 检查初始森林
        if (current_components_.size() > 1) {
            // 多分量，返回子矩阵
            return convertComponentsToBlocks(current_components_);
        } else {
            // 单分量，返回空（主算法继续）
            return {};
        }
    // }
    
    // 计算被删除的顶点
    std::set<int> deleted_vertices;
    std::set_difference(
        current_rows_.begin(), current_rows_.end(),
        block_rows.begin(), block_rows.end(),
        std::inserter(deleted_vertices, deleted_vertices.begin())
    );
    
    if (deleted_vertices.empty()) {
        // 没有变化，返回当前分量
        if (current_components_.size() > 1) {
            return convertComponentsToBlocks(current_components_);
        } else {
            return {};
        }
    }
    
    // 更新当前行集合
    current_rows_ = block_rows;
    
    // std::vector<std::set<int>> new_components;
    
    // if (current_components_.size() == 1) {
    //     // 单分量情况
    //     new_components = DecGenerateCC(deleted_vertices, current_components_[0]);
    // } else {
    //     // 多分量情况（理论上不应该出现，因为已经分割）
    //     for (const auto& comp : current_components_) {
    //         std::set<int> comp_deleted;
    //         std::set_intersection(
    //             deleted_vertices.begin(), deleted_vertices.end(),
    //             comp.begin(), comp.end(),
    //             std::inserter(comp_deleted, comp_deleted.begin())
    //         );
            
    //         if (!comp_deleted.empty()) {
    //             auto sub_comps = DecGenerateCC(comp_deleted, comp);
    //             new_components.insert(new_components.end(), sub_comps.begin(), sub_comps.end());
    //         } else {
    //             new_components.push_back(comp);
    //         }
    //     }
    // }

    // 调用增量算法生成新的连通分量
    auto new_components = DecGenerateCC(deleted_vertices, current_components_[0]); 
    
    // 更新历史记录
    if (!cover_stack_.empty()) {
        cover_stack_.back().new_components = new_components;
    }
    
    // 更新当前连通分量
    current_components_ = new_components;
    
    // 转换为Block并返回
    if (new_components.size() > 1) {
        return convertComponentsToBlocks(new_components);
    } else {
        return {};  // 仍是单分量
    }
}

// 增量式分解实现
std::vector<std::set<int>> ComponentDetector::DecGenerateCC(
    const std::set<int>& deleted_vertices,
    const std::set<int>& prev_component ) {
    
    std::vector<std::set<int>> affected_components;
    
    if (deleted_vertices.empty()) {
        // 无删除，返回原分量
        if (!prev_component.empty()) {
            affected_components.push_back(prev_component);
        }
        return affected_components;
    }
      
    // 收集所有涉及删除顶点的边
    std::vector<std::pair<int, int>> edges_to_process;
    CoverHistory* current_history = cover_stack_.empty() ? nullptr : &cover_stack_.back();
    
    for (int u : deleted_vertices) {
        for (int v : adj_list[u]) {
            if (!row_active[v] || deleted_vertices.count(v)) continue;
            if (u > v) continue;  // 避免重复
            
            unsigned long long key = makeEdgeKey(u, v);
            
            if (tree_edges.count(key)) {
                edges_to_process.push_back({u, v});
                if (current_history) {
                    current_history->cut_tree_edges.push_back({u, v});
                }
            } else if (non_tree_edges.count(key)) {
                // 删除非树边
                if (component_info.non_tree_edges) {
                    component_info.non_tree_edges->removeEdge(key);
                }
                non_tree_edges.erase(key);
                edge_info_map.erase(key);
                
                if (current_history) {
                    current_history->removed_nontree_edges.push_back({u, v});
                }
            }
        }
    }
    
    // 处理树边删除
    for (const auto& [u, v] : edges_to_process) {
        unsigned long long key = makeEdgeKey(u, v);
        
        if (!edge_info_map.count(key)) continue;
        
        EdgeInfo& info = edge_info_map[key];
        
        // 从分量中移除树边
        component_info.tree_edges.erase(key);
        
        // Cut操作
        ett->cut(u, v);
        tree_edges.erase(key);
        
        // 确定两个新分量
        int active_vertex = deleted_vertices.count(u) ? v : u;
        int comp_u = ett->getComponentId(active_vertex);
        
        // 查找替代边（从高层向低层）
        auto replacement = FindReplacementLayered(comp_u, comp_u);
          
        if (replacement.has_value()) {
            // 找到替代边，恢复连通性
            int ru = replacement->first;
            int rv = replacement->second;
            unsigned long long repl_key = makeEdgeKey(ru, rv);
            
            ett->link(ru, rv);
            
            if (edge_info_map.count(repl_key)) {
                EdgeInfo& repl_info = edge_info_map[repl_key];
                repl_info.is_tree = true;
                
                // 从非树边升级到树边
                if (component_info.non_tree_edges) {
                    component_info.non_tree_edges->removeEdge(repl_key);
                }
                component_info.tree_edges.insert(repl_key);
                
                non_tree_edges.erase(repl_key);
                tree_edges.insert(repl_key);
            }
            
            if (current_history) {
                current_history->added_replacement_edges.push_back({ru, rv});
            }
        }
        
        edge_info_map.erase(key);
    }

    // 收集剩余的活跃顶点并重新分组
    std::set<int> remaining_vertices;
    for (int v : prev_component) {
        if (row_active[v] && !deleted_vertices.count(v)) {
            remaining_vertices.insert(v);
        }
    }
    
    if (remaining_vertices.empty()) {
        return affected_components;
    }
    
    // 根据ETT重新分组
    std::unordered_map<int, std::set<int>> new_components_map;
    for (int vertex : remaining_vertices) {
        int comp_id = ett->getComponentId(vertex);
        new_components_map[comp_id].insert(vertex);
    }
    
    // 转换为vector
    for (const auto& [comp_id, vertices] : new_components_map) {
        if (!vertices.empty()) {
            affected_components.push_back(vertices);
        }
    }
    
    // 如果只有一个分量且大小等于剩余顶点，说明没有分裂
    if (affected_components.size() == 1 && 
        affected_components[0].size() == remaining_vertices.size()) {
        return affected_components;
    }
    
    // 有分裂，返回所有新分量
    return affected_components;
}

// 在指定分量中查找替代边
// std::optional<std::pair<int,int>> ComponentDetector::FindReplacementInComponent(
//     int u, int v, int comp_root) {
    
//     if (!component_map.count(comp_root)) return std::nullopt;
    
//     int comp_u = ett->getComponentId(u);
//     int comp_v = ett->getComponentId(v);
    
//     if (comp_u == comp_v) return std::nullopt; // 仍然连通
    
//     // 在较小的分量中搜索
//     int size_u = ett->componentSize(u);
//     int size_v = ett->componentSize(v);
//     if (size_u > size_v) {
//         std::swap(u, v);
//         std::swap(comp_u, comp_v);
//     }
    
//     // 在分量的非树边中查找
//     const auto& non_tree_edge_set = component_map[comp_root].non_tree_edges;
    
//     for (unsigned long long key : non_tree_edge_set) {
//         int e_u = static_cast<int>(key >> 32);
//         int e_v = static_cast<int>(key & 0xFFFFFFFF);
        
//         if (!row_active[e_u] || !row_active[e_v]) continue;
        
//         int root_eu = ett->getComponentId(e_u);
//         int root_ev = ett->getComponentId(e_v);
        
//         // 检查是否连接两个分裂的分量
//         if ((root_eu == comp_u && root_ev == comp_v) ||
//             (root_eu == comp_v && root_ev == comp_u)) {
//             return std::make_pair(e_u, e_v);
//         }
//     }
    
//     return std::nullopt;
// }

// 分层查找替代边
std::optional<std::pair<int,int>> ComponentDetector::FindReplacementLayered(int comp_u, int comp_v) {
    
    if (comp_u == comp_v) return std::nullopt; // 仍然连通
    
    if (!component_info.non_tree_edges) return std::nullopt;
    
    LayeredNonTreeEdges* non_tree = component_info.non_tree_edges.get();
    
    // 从最高层向下查找
    for (int level = non_tree->max_level; level >= 0; level--) {
        const auto& edges_at_level = non_tree->getEdgesAtLevel(level);
        
        std::vector<unsigned long long> edges_to_demote;
        
        for (unsigned long long key : edges_at_level) {
            int e_u = static_cast<int>(key >> 32);
            int e_v = static_cast<int>(key & 0xFFFFFFFF);
            
            if (!row_active[e_u] || !row_active[e_v]) {
                edges_to_demote.push_back(key);
                continue;
            }
            
            int root_eu = ett->getComponentId(e_u);
            int root_ev = ett->getComponentId(e_v);
            
            // 检查是否连接两个分裂的分量
            if ((root_eu == comp_u && root_ev == comp_v) ||
                (root_eu == comp_v && root_ev == comp_u)) {
                return std::make_pair(e_u, e_v);
            }
            
            // 如果两端点在同一分量，降级
            if (root_eu == root_ev) {
                edges_to_demote.push_back(key);
            }
        }
        
        // 执行降级
        for (unsigned long long key : edges_to_demote) {
            non_tree->demoteEdge(key);
            if (edge_info_map.count(key)) {
                edge_info_map[key].level = level - 1;
            }
        }
    }
    
    return std::nullopt;
}

std::vector<Block> ComponentDetector::convertComponentsToBlocks(
    const std::vector<std::set<int>>& components) {
    
    std::vector<Block> blocks;
    blocks.reserve(components.size());
    
    for (const auto& comp_rows : components) {
        std::set<int> component_cols;
        std::vector<int> component_rows_vec(comp_rows.begin(), comp_rows.end());
        
        for (int r : comp_rows) {
            auto it = row_to_cols.find(r);
            if (it != row_to_cols.end()) {
                for (int col : it->second) {
                    if (active_cols.count(col)) {
                        component_cols.insert(col);
                    }
                }
            }
        }
        
        if (!component_rows_vec.empty()) {
            blocks.emplace_back(std::move(component_rows_vec), 
                               std::move(component_cols));
        }
    }
    
    return blocks;
}

// vector<Block> ComponentDetector::detect_blocks(const set<int> &block_rows) {
//     if (block_rows.empty()) return {}; // 如果没有行，则返回空集合
//     std::shared_lock<std::shared_mutex> lock(state_mutex);

//     // 创建局部 Union-Find
//     std::unordered_map<int, int> local_parent;
//     for (int r : block_rows) {
//         local_parent[r] = r;
//     }
    
//     // 迭代版本的 find（带路径压缩）
//     auto local_find = [&](int x) -> int {
//         int root = x;
//         // 找到根节点
//         while (local_parent[root] != root) {
//             root = local_parent[root];
//         }
//         // 路径压缩
//         while (x != root) {
//             int next = local_parent[x];
//             local_parent[x] = root;
//             x = next;
//         }
//         return root;
//     };
    
//     auto local_unite = [&](int x, int y) {
//         x = local_find(x);
//         y = local_find(y);
//         if (x != y) {
//             local_parent[x] = y;
//         }
//     };
    
//     std::unordered_map<int, std::vector<int>> col_to_active_rows_in_block;
    
//     for (int r : block_rows) {
//         auto it = row_to_cols.find(r);
//         if (it == row_to_cols.end()) continue;

//         for (int c : it->second) {
//             if (active_cols.count(c)) {
//                 // 记录该列包含当前块内的哪些行
//                 // 只有当一列包含 >= 2 个当前块的行时，它才提供连通性
//                 col_to_active_rows_in_block[c].push_back(r);
//             }
//         }
//     }

//     // 根据列进行合并
//     for (const auto& [col, rows_in_col] : col_to_active_rows_in_block) {
//         if (rows_in_col.size() < 2) continue;
        
//         // 将该列下的所有行合并到第一个行
//         int first_row = rows_in_col[0];
//         for (size_t i = 1; i < rows_in_col.size(); ++i) {
//             local_unite(first_row, rows_in_col[i]);
//         }
//     }

//     // 按代表元分组
//     std::unordered_map<int, std::vector<int>> rep_to_rows_list;
//     for (int r : block_rows) {
//         rep_to_rows_list[local_find(r)].push_back(r);
//     }
    
//     // 构建 Block
//     std::vector<Block> blocks;
//     blocks.reserve(rep_to_rows_list.size());
//     for (const auto& [rep, rows] : rep_to_rows_list) {
//         std::set<int> component_cols;
//         std::vector<int> component_rows_vec = rows;
        
//         for (int r : rows) {
//             auto it = row_to_cols.find(r);
//             if (it != row_to_cols.end()) {
//                 for (int col : it->second) {
//                     if (active_cols.count(col)) {
//                         component_cols.insert(col);
//                     }
//                 }
//             }
//         }
        
//         // 只有非空的块才有意义
//         if (!component_rows_vec.empty()) {
//             blocks.emplace_back(std::move(component_rows_vec), std::move(component_cols));
//         }
//     }
    
//     return blocks;
// };
