#include "../include/ComponentDetector.h"

// 应该保证只有一次全量检测，由主线程调用
vector<Block> ComponentDetector::detect_full(const set<int>& block_rows, uint64_t thread_id, int depth) {
    
    if (block_rows.empty()) return {};
    
    // 执行全量检测
    auto components = detect_full_subset(block_rows);

    auto det_ctx = make_shared<DetectionContext>(depth);
    det_ctx->block_rows = block_rows;
    det_ctx->components = components;

    // if (depth == 0) {
    det_ctx->bridge_nodes = compute_all_bridges(components); // 应当计算所有组件内部的桥梁节点
    // } else {
    //     // 从父层继承桥梁节点
    //     auto thread_ctx = get_thread_context(thread_id);
    //     shared_lock lock(thread_ctx->mutex);
    //     auto parent_it = thread_ctx->cache.find(depth - 1);
    //     if (parent_it != thread_ctx->cache.end()) {
    //         det_ctx->bridge_nodes = parent_it->second->bridge_nodes;
    //     }
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
                                
    // cout << "detect_incremental" << endl;
    // 获取该线程的上下文
    auto thread_ctx = get_thread_context(thread_id);
        
    // 获取父层上下文
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
        
        unique_lock lock(thread_ctx->mutex);
        thread_ctx->cache[current_depth] = parent_ctx;
        return parent_ctx->components;
    }

    // 检查remove是否有bridge_nodes
    unordered_set<int> removed_bridge = {};
    auto bridge_it = parent_ctx->bridge_nodes.find(0);
    if (bridge_it != parent_ctx->bridge_nodes.end()) {
        const auto& bridge_set = bridge_it->second;
        for (int r : removed_rows) {
            if (bridge_set.count(r)) {
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
        
        // if (!remaining_rows.empty()) {
        set<int> cols_set;
        for (int r : current_rows) {
            const auto& rc = get_row_cols(r);
            cols_set.insert(rc.begin(), rc.end());
        }
        result.emplace_back(move(current_rows), move(cols_set));
        // }
        
        // 缓存结果（继承桥梁信息）
        auto det_ctx = make_shared<DetectionContext>(current_depth);
        det_ctx->block_rows = current_rows;
        det_ctx->components = result;
        det_ctx->bridge_nodes = parent_ctx->bridge_nodes;  // 直接继承
        det_ctx->build_index();
        
        unique_lock lock(thread_ctx->mutex);
        thread_ctx->cache[current_depth] = det_ctx;
        
        return result;
    } else {
        // 删除了桥梁节点，重新检测当前所有行
        result = detect_full_subset(current_rows); // 一定会得到多分块

        unordered_set<int> curBridgeNodes;
        set_difference(bridge_it->second.begin(), bridge_it->second.end(),
                       removed_bridge.begin(), removed_bridge.end(),
                       inserter(curBridgeNodes, curBridgeNodes.begin()));

        auto new_bridges = allocateIndexForBridges(result, curBridgeNodes);

        // 缓存结果
        auto det_ctx = make_shared<DetectionContext>(current_depth);
        det_ctx->block_rows = current_rows;
        det_ctx->components = result;
        det_ctx->bridge_nodes = move(new_bridges);
        det_ctx->build_index();
        
        unique_lock lock(thread_ctx->mutex);
        thread_ctx->cache[current_depth] = det_ctx;
        
        return result;
    }

}
