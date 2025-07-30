#ifndef OPTIMIZED_DXD_H
#define OPTIMIZED_DXD_H

#include "DancingMatrix.h"

// 优化1: 使用BitSet优化行连通性检测
class FastBitSet {
private:
    std::vector<uint64_t> bits;
    size_t bit_count;
    
public:
    FastBitSet(size_t size = 0) : bit_count(size) {
        bits.resize((size + 63) / 64, 0);
    }
    
    void resize(size_t size) {
        bit_count = size;
        bits.resize((size + 63) / 64, 0);
    }
    
    void set(size_t pos) {
        if (pos < bit_count) {
            bits[pos / 64] |= (1ULL << (pos % 64));
        }
    }
    
    bool test(size_t pos) const {
        return pos < bit_count && (bits[pos / 64] & (1ULL << (pos % 64)));
    }
    
    bool intersects(const FastBitSet& other) const {
        size_t min_size = std::min(bits.size(), other.bits.size());
        for (size_t i = 0; i < min_size; ++i) {
            if (bits[i] & other.bits[i]) return true;
        }
        return false;
    }
    
    void clear_all() {
        std::fill(bits.begin(), bits.end(), 0);
    }
    
    size_t count() const {
        size_t result = 0;
        for (uint64_t b : bits) {
            result += __builtin_popcountll(b);
        }
        return result;
    }
    
    std::vector<int> get_set_bits() const {
        std::vector<int> result;
        for (size_t i = 0; i < bit_count; ++i) {
            if (test(i)) {
                result.push_back(i);
            }
        }
        return result;
    }
};

// 优化2: 高效的分块结构扩展
struct OptimizedBlock : public Block {
    // 预计算的连通性信息
    mutable std::vector<FastBitSet> col_row_bitsets;  // 每列的行BitSet
    mutable bool connectivity_cached = false;
    mutable std::chrono::steady_clock::time_point last_update;
    
    OptimizedBlock() : Block() {
        last_update = std::chrono::steady_clock::now();
    }
    
    OptimizedBlock(const std::set<int>& r, const std::set<int>& c) : Block(r, c) {
        last_update = std::chrono::steady_clock::now();
    }
    
    // 标记需要重新计算连通性
    void invalidate_connectivity() const {
        connectivity_cached = false;
        last_update = std::chrono::steady_clock::now();
    }
    
    // 检查连通性是否需要更新
    bool needs_connectivity_update() const {
        auto now = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_update);
        return !connectivity_cached || duration.count() > 100; // 100ms缓存过期
    }
};

// 优化3: 改进的连通性检测算法
class FastConnectivityDetector {
private:
    std::vector<int> parent, rank;
    
    int find(int x) {
        if (parent[x] != x) {
            parent[x] = find(parent[x]);  // 路径压缩
        }
        return parent[x];
    }
    
public:
    // 快速检测连通分量
    std::vector<std::vector<int>> detect_components(const OptimizedBlock& block, 
                                                   DancingMatrix* dm) {
        if (block.rows.empty() || block.cols.empty()) {
            return {};
        }
        
        size_t max_row = *std::max_element(block.rows.begin(), block.rows.end()) + 1;
        parent.resize(max_row);
        rank.resize(max_row);
        
        // 初始化并查集
        std::iota(parent.begin(), parent.end(), 0);
        std::fill(rank.begin(), rank.end(), 0);
        
        // 使用BitSet加速连通性检测
        std::unordered_map<int, FastBitSet> col_rows;
        
        for (int col : block.cols) {
            FastBitSet& row_bitset = col_rows[col];
            row_bitset.resize(max_row);
            
            // 访问父类的ColIndex
            ColunmHeader* cur = dm->getColHeader(col);  // 通过公有接口访问
            Node* node = cur->down;
            while (node != cur) {
                if (block.rows.count(node->row)) {
                    row_bitset.set(node->row);
                }
                node = node->down;
            }
        }
        
        // 快速合并相交的行
        std::vector<int> col_list(block.cols.begin(), block.cols.end());
        for (size_t i = 0; i < col_list.size(); ++i) {
            const auto& bitset_i = col_rows[col_list[i]];
            auto rows_i = bitset_i.get_set_bits();
            
            for (size_t j = 0; j < rows_i.size(); ++j) {
                for (size_t k = j + 1; k < rows_i.size(); ++k) {
                    unite(rows_i[j], rows_i[k]);
                }
            }
        }
        
        // 收集连通分量
        std::unordered_map<int, std::vector<int>> components;
        for (int row : block.rows) {
            components[find(row)].push_back(row);
        }
        
        std::vector<std::vector<int>> result;
        for (auto& [root, component] : components) {
            if (component.size() > 0) {
                result.push_back(std::move(component));
            }
        }
        
        return result;
    }
    
private:
    bool unite(int a, int b) {
        int pa = find(a), pb = find(b);
        if (pa == pb) return false;
        
        if (rank[pa] < rank[pb]) std::swap(pa, pb);
        parent[pb] = pa;
        if (rank[pa] == rank[pb]) rank[pa]++;
        return true;
    }
};

// 优化4: 智能缓存管理器
class SmartCacheManager {
private:
    struct CacheEntry {
        size_t hash;
        std::shared_ptr<DNNFNode> result;
        std::chrono::steady_clock::time_point timestamp;
        std::atomic<size_t> access_count{0};
        
        CacheEntry() = default;
        CacheEntry(size_t h, std::shared_ptr<DNNFNode> r) 
            : hash(h), result(r), timestamp(std::chrono::steady_clock::now()) {}

         // 禁用拷贝构造和拷贝赋值
        CacheEntry(const CacheEntry&) = delete;
        CacheEntry& operator=(const CacheEntry&) = delete;
        
        // 允许移动构造和移动赋值
        CacheEntry(CacheEntry&& other) noexcept 
            : hash(other.hash), 
              result(std::move(other.result)), 
              timestamp(other.timestamp),
              access_count(other.access_count.load()) {}
        
        CacheEntry& operator=(CacheEntry&& other) noexcept {
            if (this != &other) {
                hash = other.hash;
                result = std::move(other.result);
                timestamp = other.timestamp;
                access_count.store(other.access_count.load());
            }
            return *this;
        }
    };
    
    static constexpr size_t CACHE_SIZE = 1 << 18;  // 256K entries
    static constexpr size_t CACHE_MASK = CACHE_SIZE - 1;
    std::array<CacheEntry, CACHE_SIZE> cache;
    mutable std::mutex cache_mutex;
    std::atomic<size_t> hit_count{0};
    std::atomic<size_t> miss_count{0};
    
public:
    std::shared_ptr<DNNFNode> get(size_t hash) {
        std::lock_guard<std::mutex> lock(cache_mutex);
        size_t index = hash & CACHE_MASK;
        auto& entry = cache[index];
        
        if (entry.hash == hash && entry.result) {
            entry.access_count.fetch_add(1, std::memory_order_relaxed);
            hit_count.fetch_add(1, std::memory_order_relaxed);
            return entry.result;
        }
        
        miss_count.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }
    
    void put(size_t hash, std::shared_ptr<DNNFNode> result) {
        std::lock_guard<std::mutex> lock(cache_mutex);
        size_t index = hash & CACHE_MASK;
        auto& entry = cache[index];
        entry.hash = hash;
        entry.result = result;
        entry.timestamp = std::chrono::steady_clock::now();
        entry.access_count.store(0);
    }
    
    double get_hit_rate() const {
        size_t hits = hit_count.load();
        size_t misses = miss_count.load();
        return (hits + misses) > 0 ? static_cast<double>(hits) / (hits + misses) : 0.0;
    }
    
    void clear_stats() {
        hit_count.store(0);
        miss_count.store(0);
    }
    
    std::pair<size_t, size_t> get_stats() const {
        return {hit_count.load(), miss_count.load()};
    }
};

// 优化5: DancingMatrix的优化扩展类
class OptimizedDancingMatrix : public DancingMatrix {
private:
    FastConnectivityDetector connectivity_detector;
    SmartCacheManager smart_cache;
    std::atomic<size_t> decomposition_count{0};
    std::atomic<size_t> parallel_search_count{0};
    
    // 性能阈值
    static constexpr size_t MIN_PARALLEL_BLOCKS = 3;
    static constexpr size_t MIN_BLOCK_COMPLEXITY = 20;
    static constexpr size_t MAX_SERIAL_DEPTH = 5;
    
public:
    OptimizedDancingMatrix(int rows, int cols, int** matrix) 
        : DancingMatrix(rows, cols, matrix) {
        smart_cache.clear_stats();
    }
    
    // 优化6: 快速连通性检测的initBlock重写
    void fastInitBlock(OptimizedBlock& block) {
        if (block.connectivity_cached && !block.needs_connectivity_update()) {
            return;
        }
        
        // 使用快速连通性检测器
        auto components = connectivity_detector.detect_components(block, this);
        
        block.connectedRows.clear();
        block.rowToRowsSet.clear();
        
        for (size_t i = 0; i < components.size(); ++i) {
            std::set<int> component_set(components[i].begin(), components[i].end());
            block.connectedRows.push_back(component_set);
            
            for (int row : components[i]) {
                block.rowToRowsSet[row].push_back(i);
            }
        }
        
        block.connectivity_cached = true;
    }
    
    // 优化7: 智能分块策略
    std::vector<OptimizedBlock> intelligentSplitBlock(const OptimizedBlock& block) {
        std::vector<OptimizedBlock> result;
        
        if (block.connectedRows.size() <= 1) {
            result.push_back(block);
            return result;
        }
        
        decomposition_count.fetch_add(1, std::memory_order_relaxed);
        
        for (const auto& component : block.connectedRows) {
            std::set<int> component_cols;
            
            // 找到这些行涉及的列
            for (int row : component) {
                if (!block.rows.count(row)) continue;
                
                if (RowIndex[row].right) {
                    Node* rowHead = RowIndex[row].right;
                    Node* cur = rowHead;
                    do {
                        if (block.cols.count(cur->col)) {
                            component_cols.insert(cur->col);
                        }
                        cur = cur->right;
                    } while (cur != rowHead);
                }
            }
            
            if (!component_cols.empty()) {
                OptimizedBlock sub_block(component, component_cols);
                sub_block.invalidate_connectivity(); // 标记需要重新计算
                result.push_back(std::move(sub_block));
            }
        }
        
        return result;
    }
    
    // 优化8: 自适应并行搜索策略
    std::shared_ptr<DNNFNode> adaptiveParallelSearch(std::vector<OptimizedBlock>& blocks) {
        // 评估是否值得并行
        size_t total_complexity = 0;
        for (const auto& block : blocks) {
            total_complexity += block.rows.size() * block.cols.size();
        }
        
        bool should_parallel = (blocks.size() >= MIN_PARALLEL_BLOCKS) && 
                              (total_complexity > MIN_BLOCK_COMPLEXITY * blocks.size()) &&
                              (parallel_search_count.load() < MAX_P_COUNT);
        
        if (!should_parallel) {
            return serialSearch(blocks);
        }
        
        parallel_search_count.fetch_add(1, std::memory_order_relaxed);
        
        // 使用父类的线程池
        std::vector<std::future<std::shared_ptr<DNNFNode>>> futures;
        
        for (auto& block : blocks) {
            futures.push_back(getThreadPool().enqueue([this, block]() mutable {
                return optimizedDXD(block);
            }));
        }
        
        // 收集结果
        auto and_node = std::make_shared<DNNFNode>(NodeType::Decomposed, -1, 0);
        uint64_t total_count = 1;
        
        for (auto& future : futures) {
            auto result = future.get();
            if (result->label == -2) {
                return F;  // 短路求值
            }
            and_node->children.push_back(result);
            total_count *= result->count;
        }
        
        and_node->count = total_count;
        return and_node;
    }
    
    // 优化9: 主搜索算法重写
    std::shared_ptr<DNNFNode> optimizedDXD(OptimizedBlock& block) {
        if (block.cols.empty()) {
            return T;
        }
        
        size_t state_hash = hashBlockState(block.cols);
        
        // 使用智能缓存
        auto cached = smart_cache.get(state_hash);
        if (cached) {
            return cached;
        }
        
        // 快速连通性检测
        fastInitBlock(block);
        
        // 检查是否可分解
        if (block.connectedRows.size() > 1) {
            auto sub_blocks = intelligentSplitBlock(block);
            auto result = adaptiveParallelSearch(sub_blocks);
            smart_cache.put(state_hash, result);
            return result;
        }
        
        // 单块搜索
        ColunmHeader* selected_col = selectColumnHeuristic(block.cols);
        if (selected_col->size <= 0) {
            smart_cache.put(state_hash, F);
            return F;
        }
        
        auto or_node = std::make_shared<DNNFNode>(NodeType::OR, selected_col->col, 0);
        
        Node* cur_node = selected_col->down;
        while (cur_node != selected_col) {
            // 使用父类的批量操作
            batchCoverInBlock(cur_node, block);
            
            auto sub_result = optimizedDXD(block);
            
            if (sub_result->label != -2) {
                // 使用父类的变量表
                auto var_it = V_Table.find(cur_node->row);
                std::shared_ptr<DNNFNode> var;
                if (var_it != V_Table.end()) {
                    var = var_it->second;
                } else {
                    var = std::make_shared<DNNFNode>(NodeType::Variable, cur_node->row + 1);
                    V_Table[cur_node->row] = var;
                }
                
                auto and_node = std::make_shared<DNNFNode>(NodeType::Decision, var, sub_result);
                and_node->count = sub_result->count;
                or_node->count += and_node->count;
                or_node->children.push_back(and_node);
            }
            
            batchUncoverInBlock(block);
            cur_node = cur_node->down;
        }
        
        if (or_node->children.empty()) {
            or_node = F;
        }
        
        smart_cache.put(state_hash, or_node);
        return or_node;
    }
    
    // 优化10: 启动优化搜索的公共接口
    void startOptimizedDXD() {
        std::cout << "开始优化版DXD搜索..." << std::endl;
        
        OptimizedBlock initial_block(rowsSet, colsSet);
        
        auto start = std::chrono::high_resolution_clock::now();
        rootDNNF = optimizedDXD(initial_block);
        auto end = std::chrono::high_resolution_clock::now();
        
        searchTimeSeconds = std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count();
        
        std::cout << "搜索到的解个数: " << rootDNNF->count << std::endl;
        std::cout << "优化版DXD搜索完成, 耗时: " << searchTimeSeconds << " 秒" << std::endl;
        
        print_optimization_stats();
        std::cout << std::endl;
    }
    
    // 性能统计
    void print_optimization_stats() const {
        auto [hits, misses] = smart_cache.get_stats();
        double hit_rate = smart_cache.get_hit_rate() * 100.0;
        
        std::cout << "=== 优化统计 ===" << std::endl;
        std::cout << "缓存命中率: " << std::fixed << std::setprecision(2) << hit_rate << "%" << std::endl;
        std::cout << "缓存命中: " << hits << ", 未命中: " << misses << std::endl;
        std::cout << "分解次数: " << decomposition_count.load() << std::endl;
        std::cout << "并行搜索次数: " << parallel_search_count.load() << std::endl;
    }

private:
    // 串行搜索辅助方法
    std::shared_ptr<DNNFNode> serialSearch(std::vector<OptimizedBlock>& blocks) {
        auto and_node = std::make_shared<DNNFNode>(NodeType::Decomposed, -1, 0);
        uint64_t total_count = 1;
        
        for (auto& block : blocks) {
            auto result = optimizedDXD(block);
            if (result->label == -2) {
                return F;
            }
            and_node->children.push_back(result);
            total_count *= result->count;
        }
        
        and_node->count = total_count;
        return and_node;
    }
    
    // 访问父类的线程池（通过公有静态方法）
    static ThreadPool& getThreadPool() {
        return DancingMatrix::getThreadPool();
    }
};

#endif // OPTIMIZED_DXD_H