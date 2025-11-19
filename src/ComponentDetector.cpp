#include "../include/ComponentDetector.h"

ComponentDetector::ComponentDetector(const std::unordered_map<int, std::set<int>>& col_to_rows, const int n, const int m) : col_to_rows(col_to_rows), 
                                        num_rows(n),  num_cols(m), row_to_cols(n) {
    
    for (const auto& [col, rows] : col_to_rows) {
        for (int row : rows) {
            row_to_cols[row].push_back(col);
        }
    }

    // Initially all rows are active
    for (int i = 0; i < num_rows; i++) {
        active_rows.insert(i);
    }
  
    adj_list.assign(num_rows, set<int>());

    for (const auto& [col, rows] : col_to_rows) {
        if (rows.empty()) continue;
        vector<int> row_vec(rows.begin(), rows.end());

        const size_t k = row_vec.size();
        // 其他所有行都连接到代表行
        for (size_t i = 0; i < k; i++) {
            for (size_t j = i + 1; j < k; j++) {
                adj_list[row_vec[i]].insert(row_vec[j]);
                adj_list[row_vec[j]].insert(row_vec[i]);
            }
        }
    }

    ett = std::make_unique<splay_tree_ett::EulerTourTree>(num_rows + num_cols);

    // 在 ETT 中建立所有边
    {
        std::unique_lock<std::shared_mutex> lock(ett_mutex);
        for (const auto& [col, rows] : col_to_rows) {
            int col_node = num_rows + col;
            for (int r : rows) {
                // Link row r with its column node
                ett->Link(r, col_node);
            }
        }
    }
}

void ComponentDetector::BuildAdjacencyList(const std::unordered_map<int, std::set<int>>& col_to_rows) {
  
  // For each column, connect all pairs of rows that cover it
  for (const auto& [col, rows] : col_to_rows) {
    if (rows.size() <= 1) continue; // Skip columns with less than 2 rows

    std::vector<int> row_vec(rows.begin(), rows.end());
    
    // Connect each pair of rows covering the same column
    for (size_t i = 0; i < row_vec.size(); i++) {
      for (size_t j = i + 1; j < row_vec.size(); j++) {
        int u = row_vec[i];
        int v = row_vec[j];
        
        // Add bidirectional edge
        adj_list[u].insert(v);
        adj_list[v].insert(u);
      }
    }
  }
}

// 应该保证只有一次全量检测，由主线程调用
vector<Block> ComponentDetector::detect_full(const set<int>& block_rows, uint64_t thread_id, int depth) {
    
    if (block_rows.empty()) return {};

    // 执行全量检测
    auto components = detect_full_subset(block_rows);

    auto det_ctx = make_shared<DetectionContext>(depth);
    det_ctx->block_rows = block_rows;
    det_ctx->components = components;

    // if (depth == 0) {
    //     compute_all_bridges(components); // 应当计算所有组件内部的桥梁节点
    // }

    det_ctx->build_index();
    
    // 存入线程的缓存
    auto thread_ctx = get_thread_context(thread_id);
    unique_lock lock(thread_ctx->mutex);
    thread_ctx->cache[depth] = det_ctx;
    
    return components;
}

// 增量检测：基于父层检测结果和删除的行进行快速判断
// parent_depth: 父Block的递归深度
// current_rows: 当前Block的行集合（已经删除了某些行）
// current_depth: 当前Block的递归深度
vector<Block> ComponentDetector::detect_incremental(const set<int>& current_rows, 
                                    uint64_t thread_id,                                 
                                    int parent_depth, 
                                    int current_depth) {  
                                        
    if (bridge_nodes.empty()) return detect_full(current_rows, thread_id, current_depth);
                                        
    // 获取该线程的上下文
    auto thread_ctx = get_thread_context(thread_id);
        
    // 获取父层上下文,必须确保继承时正确复制
    shared_ptr<DetectionContext> parent_ctx;
    {
        shared_lock lock(thread_ctx->mutex);
        auto it = thread_ctx->cache.find(parent_depth);
        
        // if (it == thread_ctx->cache.end()) {
        //     throw runtime_error("Parent context not found for depth " + to_string(parent_depth));
        // }
        parent_ctx = it->second;
    }

    assert(parent_ctx->components.size() == 1);

    // 计算删除的行（只会删除，不会添加）
    set<int> removed_rows;
    set_difference(parent_ctx->block_rows.begin(), parent_ctx->block_rows.end(),
                    current_rows.begin(), current_rows.end(),
                    inserter(removed_rows, removed_rows.begin()));
    
    // 如果没有变化，直接返回缓存结果(子线程首次执行会直接返回缓存结果)
    if (removed_rows.empty()) {
        // 即使没有变化，也要为当前depth创建缓存，供下一层使用
        auto det_ctx = make_shared<DetectionContext>(current_depth);
        det_ctx->block_rows = current_rows;
        det_ctx->components = parent_ctx->components;
        det_ctx->build_index();

        unique_lock lock(thread_ctx->mutex);
        thread_ctx->cache[current_depth] = det_ctx;
        return parent_ctx->components;
    }

    // 检查remove是否有bridge_nodes
    unordered_set<int> removed_bridge = {};
    {
        shared_lock lock(bridge_nodes_mutex);
        for (int r : removed_rows) {
            if (bridge_nodes.count(r)) {
                removed_bridge.insert(r);
            }
        }
    }

    vector<Block> result;

    // 如果没有删除桥梁节点，连通分量不变
    if (removed_bridge.empty()) {
        // const auto& parent_comp = parent_ctx->components[0];
        
        // set<int> remaining_rows;
        // set_intersection(parent_comp.rows.begin(), parent_comp.rows.end(),
        //                 current_rows.begin(), current_rows.end(),
        //                 inserter(remaining_rows, remaining_rows.begin()));
        
        set<int> cols_set;
        for (int r : current_rows) {
            const auto& rc = get_row_cols(r);
            cols_set.insert(rc.begin(), rc.end());
        }
        result.emplace_back(move(current_rows), move(cols_set));
        
        // 缓存结果（继承桥梁信息）
        auto det_ctx = make_shared<DetectionContext>(current_depth);
        det_ctx->block_rows = current_rows;
        det_ctx->components = result;
        det_ctx->build_index();
        
        unique_lock lock(thread_ctx->mutex);
        thread_ctx->cache[current_depth] = det_ctx;
        
        return result;
    } else {
        // 如果删除了桥梁节点，连通分量可能改变
        return detect_full(current_rows, thread_id, current_depth);
    }

}
