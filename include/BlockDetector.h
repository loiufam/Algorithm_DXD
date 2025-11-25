#ifndef BLOCKDETECTOR_H
#define BLOCKDETECTOR_H
#pragma once

#include "ETT.h"
#include "Block.h"

class BlockDetector {
private:
    Graph& graph_ptr;
    ETTUnionFind macro_union_find;
    StaticEulerTourTree static_ett;

    // 用于维护矩阵行到列的动态映射
    std::unordered_map<int, std::set<int>> row_to_cols;
    
public:

    explicit BlockDetector(Graph& input_graph, const std::unordered_map<int, std::set<int>>& rtc) : graph_ptr(input_graph), row_to_cols(rtc) {
        build_from_graph();
    }

    void build_from_graph() {
        int num_nodes = graph_ptr.num_nodes;
        
        // Build macro components using Union-Find
        macro_union_find.initialize(num_nodes);
        for (int u = 0; u < num_nodes; ++u) {
            for (int v : graph_ptr.adjacency_list[u]) {
                if (u < v) {
                    macro_union_find.unite(u, v);
                }
            }
        }
        
        // Build static ETT for spanning forest
        static_ett.build(graph_ptr.adjacency_list, num_nodes);
    }

    vector<Block> detect_blocks(const set<int>& active_rows) const {

        int num_nodes = graph_ptr.num_nodes;
        graph_ptr.print();
        
        // Step 1: Group active nodes by macro component (ETT tree ID)
        unordered_map<int, vector<int>> macro_groups;
        macro_groups.reserve(active_rows.size());
        
        for (int node : active_rows) {
            // if (node < 0 || node >= num_nodes) continue;
            int tree_idx = static_ett.get_tree_id(node);
            macro_groups[tree_idx].push_back(node);
        }
        
        // Step 2: Create active set for O(1) lookup
        unordered_set<int> active_set(active_rows.begin(), active_rows.end());
        
        // Step 3: BFS within each macro group to find actual components
        return find_components_via_bfs(macro_groups, active_set, num_nodes);
    }

private:

    vector<Block> find_components_via_bfs(
        const unordered_map<int, vector<int>>& macro_groups,
        const unordered_set<int>& active_set,
        int num_nodes) const {
        
        vector<Block> results;
        results.reserve(macro_groups.size());

        graph_ptr.print();

        // visited mark array for all nodes (fast O(1) mark/clear via touched list)
        vector<uint8_t> visited(num_nodes, 0);
        vector<int> touched; touched.reserve(256);

        // Process each macro group
        for (const auto& [tree_idx, group_nodes] : macro_groups) {
            for (int start_node : group_nodes) {
                // safety: check start_node in range and active
                if (start_node < 0 || start_node >= num_nodes) {
                    continue;
                }
                if (visited[start_node] || !active_set.count(start_node)) continue;

                // BFS to find connected component (fills visited & clears touched)
                vector<int> comp_rows; comp_rows.reserve(64);
                deque<int> q;
                q.push_back(start_node);
                visited[start_node] = 1;
                touched.push_back(start_node);

                while (!q.empty()) {
                    int cur = q.front(); q.pop_front();
                    comp_rows.push_back(cur);

                    // iterate neighbors -- *must* check neighbor bounds
                    const auto &nbrs = graph_ptr.adjacency_list[cur];
                    for (int nb : nbrs) {
                        if (nb < 0 || nb >= num_nodes) {
                            continue;
                        }
                        if (!active_set.count(nb) || visited[nb]) continue;
                        visited[nb] = 1;
                        touched.push_back(nb);
                        q.push_back(nb);
                    }
                }

                // build cols set from rows (your original logic)
                if (!comp_rows.empty()) {
                    set<int> rows_set(comp_rows.begin(), comp_rows.end());
                    set<int> cols_set;
                    for (int row : comp_rows) {
                        const auto &cols = row_to_cols.at(row); // assume exists
                        cols_set.insert(cols.begin(), cols.end());
                    }
                    results.emplace_back(std::move(rows_set), std::move(cols_set));
                }

                // clear visited marks for touched nodes
                for (int v : touched) visited[v] = 0;
                touched.clear();
            }
        }

        return results;
    }
    
};

#endif // BLOCKDETECTOR_H