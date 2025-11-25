#include "../include/ComponentDetector.h"

// 定义 thread_local 静态成员
thread_local std::vector<CoverHistory> ComponentDetector::cover_stack_;
thread_local std::vector<CoverHistory> ComponentDetector::history_pool_;

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
    // if (debug_mode) {
    //     debug_log << "\n>>> DoCover(col=" << c << ")" << std::endl;
    // }

    CoverHistory history;
    history.col = c;

    // col_to_rows 是静态的，这里不需要锁
    auto it = col_to_rows.find(c);
    const auto& rows = it->second;  // 获取与列 c 相连的所有行

    // if (debug_mode) {
    //     debug_log << "  Rows containing col " << c << ": {";
    //     for (size_t i = 0; i < rows.size(); i++) {
    //         if (i > 0) debug_log << ", ";
    //         debug_log << rows[i];
    //     }
    //     debug_log << "}" << std::endl;
    // }

    for (int row : rows) {
        if (!row_active[row]) {
            // if (debug_mode) {
            //     debug_log << "  Row " << row << " already inactive, skipping" 
            //                 << std::endl;
            // }
            continue;
        }

        // if (debug_mode) {
        //     debug_log << "  Processing row " << row << std::endl;
        //     debug_log << "    Neighbors: {";
        //     bool first = true;
        //     for (int neighbor : adj_list[row]) {
        //         if (!first) debug_log << ", ";
        //         debug_log << neighbor << (row_active[neighbor] ? "(active)" : "(inactive)");
        //         first = false;
        //     }
        //     debug_log << "}" << std::endl;
        // }
        
        // Cut该行的所有边
        for (int neighbor : adj_list[row]) {
            if (row_active[neighbor]) {
                int u = row, v = neighbor;
                if (u > v) std::swap(u, v);
                
                // 检查边是否存在
                // bool edge_exists = ett->hasEdge(u, v);
                // if (debug_mode) {
                //     debug_log << "  Attempting to cut edge (" << u << ", " << v << ")";
                //     if (!edge_exists) {
                //         debug_log << " [EDGE NOT FOUND IN ETT!]";
                //     }
                //     debug_log << std::endl;
                // }

                history.cut_edges.push_back({u, v});
                ett->cut(u, v);
                // if (edge_exists) {
                //     history.cut_edges.push_back({u, v});
                //     bool cut_success = ett->cut(u, v);
                    
                    // if (debug_mode && !cut_success) {
                    //     debug_log << "    ❌ Cut failed!" << std::endl;
                    // }
                // }

                // if (debug_mode) {
                //     debug_log << "    Cut edge (" << u << ", " << v << ")" << std::endl;
                // }
            }
        }
        
        // 标记为非激活
        row_active[row] = false;
        history.removed_rows.push_back(row);

        // if (debug_mode) {
        //     debug_log << "    Deactivated row " << row << std::endl;
        // }
    }

    return history;
}

void ComponentDetector::DoUncover(const CoverHistory& history) {
    // 快速路径：空历史直接返回
    if (history.isEmpty()) {
        return;
    }

    // if (debug_mode) {
    //     debug_log << "\n<<< Uncover(col=" << history.col << ")" << std::endl;
    // }
    // 重新连接边
    // for (const auto& [u, v] : history.cut_edges) {
    //     if (debug_mode) {
    //         debug_log << "  Re-linked edge (" << u << ", " << v << ")" << std::endl;
    //     }
    // }

    // 第一步：恢复所有被cut的边
    ett->batchLink(history.cut_edges);
    
    // 第二步：恢复行的激活状态
    for (int row : history.removed_rows) {
        // if (debug_log) {
        //     debug_log << "  Reactivating row " << row << std::endl;
        // }
        row_active[row] = true;
    }
}

vector<Block> ComponentDetector::detect_by_ett(const set<int>& block_rows){
    if (block_rows.empty()) return {}; 

    // if (debug_mode) {
    //     debug_log << "\n=== detect_by_ett called ===" << std::endl;
    //     debug_log << "Input rows: {";
    //     bool first = true;
    //     for (int r : block_rows) {
    //         if (!first) debug_log << ", ";
    //         debug_log << r;
    //         first = false;
    //     }
    //     debug_log << "}" << std::endl;
    // }

    // 转换为 vector 以使用批量接口
    std::vector<int> rows_vec(block_rows.begin(), block_rows.end());

    // 在调用ETT之前，先验证这些行是否应该连通
    // if (debug_mode) {
    //     debug_log << "Checking connectivity via adj_list:" << std::endl;
    //     for (int row : rows_vec) {
    //         debug_log << "  Row " << row << " neighbors: {";
    //         bool first = true;
    //         for (int neighbor : adj_list[row]) {
    //             if (block_rows.count(neighbor)) {
    //                 if (!first) debug_log << ", ";
    //                 debug_log << neighbor;
    //                 first = false;
    //             }
    //         }
    //         debug_log << "}" << std::endl;
    //     }
    // }
    
    // 批量获取连通分量分组
    auto components_map = ett->batchGroupByComponent(rows_vec);

    // if (debug_mode) {
    //     debug_log << "ETT returned " << components_map.size() << " components" 
    //               << std::endl;
    // }

    vector<Block> result;
    result.reserve(components_map.size());

    for (auto& [comp_id, rows] : components_map) {
        set<int> cols_set;
        for (int row : rows) {
            const auto& cols = row_to_cols[row];
            cols_set.insert(cols.begin(), cols.end());
        }
        result.emplace_back(std::move(rows), std::move(cols_set));
    }

    return result;
};

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

