#ifndef COMPONENT_DETECTOR_H
#define COMPONENT_DETECTOR_H

#pragma once

#include "TreapETT.h"
#include "SplayETT.h"
#include <vector>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <shared_mutex>
#include <memory>
#include <atomic>
#include <optional>
#include <cstdint>


// 检测上下文 - 存储某个递归层的检测结果
struct DetectionContext {
    set<int> block_rows;                          // 当前块包含的所有行
    vector<Block> components;                     // 检测到的连通分量
    unordered_map<int, size_t> row_to_component;  // 行 -> 分量索引的映射
    unordered_map<size_t, unordered_set<int>> bridge_nodes; // 分量索引 -> 该分量的桥梁节点集合
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

class ComponentDetector {
private:
    // ===============================================================
    // SplayETT
    // ===============================================================
    unique_ptr<SplayETT> splay_ett;
    unordered_map<int, unordered_set<int>> col_rows; // column -> rows present
    unordered_map<long long, int> edge_count; // multiplicity of edges between rows
    function<const set<int>&(int)> get_row_cols;

    // 多线程保护
    mutable shared_mutex ett_mutex;          // 保护ETT结构
    mutable shared_mutex col_rows_mutex;     // 保护col_rows和edge_count
    
    // 线程上下文管理
    mutable shared_mutex thread_ctx_mutex;
    unordered_map<uint64_t, shared_ptr<ThreadContext>> thread_contexts; // thread_id -> context
    atomic<uint64_t> next_thread_id{1};  // 自增的线程ID分配器

    // ===============================================================
    // TreapETT
    // ===============================================================
    shared_ptr<ETTree> ett;

    uint64_t global_version = 0;  // 全局版本号，每次ETT修改时递增

public:
    ComponentDetector(shared_ptr<ETTree> ett_ptr, function<const set<int>&(int)> cols_getter)
        : ett(ett_ptr), get_row_cols(cols_getter) {}

    ComponentDetector( function<const set<int>&(int)> cols_getter)
        : splay_ett(make_unique<SplayETT>()), get_row_cols(cols_getter) {}

    static long long edge_key(int a, int b) {
        if (a > b) swap(a,b);
        return ( (long long)a << 32 ) | (unsigned long long)b;
    }

    void add_row(int r) {
        // 多线程加锁
        {
            unique_lock ett_lock(ett_mutex);
            splay_ett->make_vertex(r);
        }

        const auto& cols = get_row_cols(r);
        unique_lock col_lock(col_rows_mutex);

        for (int c : cols) {
            auto &s = col_rows[c];
            for (int other : s) {
                if (other == r) continue;
                long long k = edge_key(r, other);
                if (++edge_count[k] == 1) {
                    unique_lock ett_lock(ett_mutex);
                    splay_ett->link(r, other);
                }
            }
            s.insert(r);
        }
    }

    void remove_row(int r) {
        // 多线程加锁
        const auto& cols = get_row_cols(r);
        unique_lock col_lock(col_rows_mutex);
        for (int c : cols) {
            auto it = col_rows.find(c);
            if (it == col_rows.end()) continue; // 跳过不存在的列
            auto &s = it->second;
            for (int other : s) {
                if (other == r) continue;
                long long k = edge_key(r, other);
                auto eit = edge_count.find(k);
                if (eit == edge_count.end()) continue; // 跳过不存在的边
                if (--(eit->second) == 0) {
                    edge_count.erase(eit);
                    unique_lock ett_lock(ett_mutex);
                    splay_ett->cut(r, other);
                }
            }
            s.erase(r);
            if (s.empty()) col_rows.erase(it);
        }

        {
            unique_lock ett_lock(ett_mutex);
            splay_ett->remove_vertex(r);
        }
    }

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

    // 并行找到所有桥梁节点
    unordered_map<size_t, unordered_set<int>> compute_all_bridges(const vector<Block>& components) {
        unordered_map<size_t, unordered_set<int>> bridges;
        int n = components.size();

        // 用 vector 存储结果，避免并发写 unordered_map
        vector<unordered_set<int>> local_results(n);

        // --- 并行处理每个 component ---
        #pragma omp parallel for schedule(dynamic)
        for (int idx = 0; idx < n; ++idx) {
            const auto& comp = components[idx];

            if (comp.rows.size() <= 2) continue; // 不可能有割点

            // 1. 为 rows 建 index: rowID -> (0..m-1)
            vector<int> nodes(comp.rows.begin(), comp.rows.end());
            unordered_map<int, int> id;
            id.reserve(nodes.size() * 2);
            for (int i = 0; i < (int)nodes.size(); ++i) {
                id[nodes[i]] = i;
            }

            int m = nodes.size();

            // 2. 构建邻接表
            vector<vector<int>> nbrs(m);

            {
                // 读 col_rows，需要上共享锁
                shared_lock lock(col_rows_mutex);
                for (int i = 0; i < m; ++i) {
                    int r = nodes[i];
                    const auto &cols = get_row_cols(r);
                    for (int c : cols) {
                        auto it = col_rows.find(c);
                        if (it == col_rows.end()) continue;
                        for (int other : it->second) {
                            auto jt = id.find(other);
                            if (jt != id.end()) {
                                int j = jt->second;
                                if (j != i) nbrs[i].push_back(j);
                            }
                        }
                    }
                }
            }

            // 3. bridge node detection: 对每个节点 i 检查删除 i 并删除其邻居后的连通性
            unordered_set<int> comp_bridges;

            // 遍历每个节点
            for (int i = 0; i < m; ++i) {
                // 构造 removed 标志：包括 i 本身和它的直接邻居
                vector<char> removed(m, 0);
                removed[i] = 1;
                for (int nb : nbrs[i]) removed[nb] = 1;

                // 计算 remaining_count
                int remaining = 0;
                int start = -1;
                for (int t = 0; t < m; ++t) {
                    if (!removed[t]) {
                        remaining++;
                        if (start == -1) start = t;
                    }
                }

                if (remaining <= 1) {
                    // 剩余 <=1 个节点不可能分裂，继续下一个 i
                    continue;
                }

                // 从 start 做 DFS（跳过 removed 节点）
                int reached = 0;
                // 使用栈迭代以避免递归深度问题
                vector<int> stack;
                stack.reserve(remaining);
                stack.push_back(start);
                vector<char> seen(m, 0);
                seen[start] = 1;

                while (!stack.empty()) {
                    int u = stack.back(); stack.pop_back();
                    reached++;
                    for (int v : nbrs[u]) {
                        if (removed[v] || seen[v]) continue;
                        seen[v] = 1;
                        stack.push_back(v);
                    }
                }

                if (reached < remaining) {
                    // 删除 i 及其冲突行会导致分裂
                    comp_bridges.insert(nodes[i]); // 插入真实 row id
                }
            }

            if (!comp_bridges.empty()) {
                local_results[idx] = std::move(comp_bridges);
            }

        } // end of parallel region

        // 4. 将结果合并到 unordered_map（串行，O(n)）
        for (int i = 0; i < n; ++i) {
            if (!local_results[i].empty()) {
                bridges[i] = move(local_results[i]);
                // cout << "Bridge nodes of component " << i << ": { ";
                // for (int node : bridges[i]) {
                //     cout << node << " ";
                // }
                // cout << "}" << endl;
            }
        }
        
        return bridges;
    }

    unordered_map<size_t, unordered_set<int>> allocateIndexForBridges(const vector<Block>& components, const unordered_set<int>& currentBridges) {
        unordered_map<size_t, unordered_set<int>> result;

        // 如果没有bridge，直接返回空
        if (currentBridges.empty()) {
            return result;
        }

        for (int bridge : currentBridges) {
            for (size_t compIdx = 0; compIdx < components.size(); ++compIdx) {
                if (components[compIdx].rows.count(bridge)) {
                    result[compIdx].insert(bridge);
                    break; // 一个bridge只属于唯一组件
                }
            }
        }
        
        return result;
    }

    // 对子集进行全量检测
    vector<Block> detect_full_subset(const set<int>& rows) {
        if (rows.empty()) return {};
                
        shared_lock ett_lock(ett_mutex);

        unordered_map<intptr_t, vector<int>> component_groups;
        for (int r : rows) {
            intptr_t comp_id = splay_ett->get_component_id(r);
            if (comp_id != -1) {
                component_groups[comp_id].push_back(r);
            } 
        }
        ett_lock.unlock();
        
        vector<Block> result;
        result.reserve(component_groups.size());
        
        for (auto& [comp_id, rows_vec] : component_groups) {
            set<int> rows_set(rows_vec.begin(), rows_vec.end());
            set<int> cols_set;
            
            for (int r : rows_vec) {
                const auto& rc = get_row_cols(r);
                cols_set.insert(rc.begin(), rc.end());
            }
            result.emplace_back(move(rows_set), move(cols_set));
        }
        
        return result;
    }

    // 全量检测（用于首次检测）
    vector<Block> detect_full(const set<int>& block_rows, uint64_t thread_id, int depth = 0);

    // 增量检测：基于上次结果和变化的行进行增量更新
    vector<Block> detect_incremental(const set<int>& current_rows, 
                                     uint64_t thread_id,
                                     int parent_depth, 
                                     int current_depth);

    // 分配新的线程ID（主线程和子线程都需要调用）
    uint64_t allocate_thread_id() {
        return next_thread_id.fetch_add(1);
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
        
        // 子线程只继承它要处理的那个分量
        child_det_ctx->bridge_nodes[0] = parent_context->bridge_nodes[block_index];
        
        child_det_ctx->build_index();
        // 存入子线程的缓存
        auto child_ctx = get_thread_context(child_thread_id);
        unique_lock child_lock(child_ctx->mutex);
        child_ctx->cache[depth] = child_det_ctx;
    }
    
    // 清理指定深度之后的缓存（回溯时可选调用）
    void cleanup_depth_after(uint64_t thread_id, int depth) {
        auto thread_ctx = get_thread_context(thread_id);
        unique_lock lock(thread_ctx->mutex);
        
        for (auto it = thread_ctx->cache.begin(); it != thread_ctx->cache.end();) {
            if (it->first > depth) {
                it = thread_ctx->cache.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    // 清理整个线程的缓存（线程结束时调用）
    void cleanup_thread(uint64_t thread_id) {
        unique_lock lock(thread_ctx_mutex);
        thread_contexts.erase(thread_id);
    }

    // ===============================================
    // 调试
    // ===============================================
    // 打印当前边表与分量概览
    void debug_dump_state() {
        cerr << "=== BlockDetector dump ===\n";
        cerr << "col_rows:\n";
        for (auto &p : col_rows) {
            cerr << " col " << p.first << ":";
            for (int r : p.second) cerr << ' ' << r;
            cerr << '\n';
        }
        cerr << "edge_count (pair -> cnt):\n";
        for (auto &e : edge_count) {
            long long k = e.first;
            int a = int(k >> 32);
            int b = int(k & 0xffffffff);
            cerr << " (" << a << "," << b << ") -> " << e.second << '\n';
        }
        cerr << "ETT vertex_occurrence keys:\n";
        for (auto &v : splay_ett->vertex_occurrence) cerr << v.first << ' ';
        cerr << '\n';
        cerr << "ETT edges (edge_map):\n";
        for (auto &em : splay_ett->edge_map) {
            auto k = em.first;
            int a = k.first;
            int b = k.second;
            cerr << " (" << a << "," << b << ")\n";
        }
        cerr << "=========================\n";
    }

    // 获取桥梁节点信息（调试用）
    unordered_set<int> get_bridges(uint64_t thread_id, int depth) {
        auto thread_ctx = get_thread_context(thread_id);
        shared_lock lock(thread_ctx->mutex);
        
        unordered_set<int> bridges;
        auto it = thread_ctx->cache.find(depth);
        if (it != thread_ctx->cache.end()) {
            // 收集所有组件的桥梁节点
            for (auto &[index, bridge_nodes] : it->second->bridge_nodes) {
                bridges.insert(bridge_nodes.begin(), bridge_nodes.end());
            }
        }
        return bridges;
    }

};

#endif // COMPONENT_DETECTOR_H