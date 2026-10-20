#ifndef DXD_H
#define DXD_H

#include "../include/DancingMatrix.h"
#include "../include/DXDTime.h"
#include <atomic>
#include <ctime>

const int TIME_LIMIT_SECONDS = 1500; 
using namespace std;

enum class NodeType { OR, Decision, Decomposed, Variable, Terminal };  // 节点类型 AND node 分为Decision和Decomposed两种
// 模式：ETT动态连通 vs BFS静态扫描
enum class DecomMode { Dynamic, Static };

class QuickStatsComplete final : public std::exception {
public:
    const char* what() const noexcept override { return "quick statistics complete"; }
};

struct DNNFNode {
    size_t id;
    inline static std::atomic<size_t> id_counter{0};

    NodeType type;
    int label{-3};   // -1 = T terminal, -2 = F terminal, -3 = Decomposed/OR
    uint64_t count{0};
    vector<std::shared_ptr<DNNFNode>> children;
    std::shared_ptr<DNNFNode> left;
    std::shared_ptr<DNNFNode> right;

    // Every constructor path assigns a fresh id.
    DNNFNode()
        : id(id_counter.fetch_add(1, std::memory_order_relaxed)) {}
 
    DNNFNode(NodeType t, int l)
        : id(id_counter.fetch_add(1, std::memory_order_relaxed)),
          type(t), label(l) {}
 
    DNNFNode(NodeType t, int l, uint64_t c)
        : id(id_counter.fetch_add(1, std::memory_order_relaxed)),
          type(t), label(l), count(c) {}
 
    // Decision node: Decision(var, lo, hi)
    DNNFNode(NodeType t, shared_ptr<DNNFNode> l, shared_ptr<DNNFNode> r)
        : id(id_counter.fetch_add(1, std::memory_order_relaxed)),
          type(t), left(l), right(r) {}
 
    // Simplified decision node (label + hi + count)
    DNNFNode(int l, shared_ptr<DNNFNode> r, uint64_t c)
        : id(id_counter.fetch_add(1, std::memory_order_relaxed)),
          type(NodeType::Decision), label(l), right(r), count(c) {}
 
    // Convenience: is this a terminal node?
    bool isTerminal() const { return type == NodeType::Terminal; }
};

class DanceDNNF : DancingMatrix { 

    public:
        DanceDNNF(int rows, int cols, int** matrix, Logger& l, bool verbose = false, int pool_size = 8) 
            : DancingMatrix(rows, cols, matrix, verbose), logger(l) {
            
 
            timer.setTimeBound(TIME_LIMIT_SECONDS);

            std::cout<< "初始化DanceDNNF完成." << endl;
        }

        DanceDNNF(const string& file_path, Logger& l, 
                       const bool useIG = false, const bool useETT = false, int pool_size = 1, bool debug = false)
            : DancingMatrix(file_path, useIG, useETT), 
            logger(l), 
            max_threads(pool_size), 
            debug(debug), 
            num_of_DNNFNodes(0),
            max_depth(1) {

            omp_set_num_threads(pool_size); // 设置并行线程数
            omp_set_max_active_levels(2);   // 初始分块 + 最多一层嵌套分块
            std::cout << "设置并行线程数为: " << pool_size << std::endl;

            if (pool_size > 1) isParallelSearch = true;
            timer.setTimeBound(TIME_LIMIT_SECONDS);
        }

        ~DanceDNNF() = default;

        CStopWatch timer;   // 计时器

        const int MAX_P_COUNT = 1; // 最大并行搜索次数   
        atomic<int> p_count{0}; // 记录当前并行的子进程数
        int detect_record = 0; // 记录第几次检测
        std::atomic<size_t> MAX_B_COUNT{1};

        double searchTime = 0.0;
        string solutionCount; // 记录解的数量
        bool timeout = false; // 是否超时
        bool isParallelSearch = false; // 是否并行搜索
        double decomposeTime = 0.0;
        // CC CPU timing categories.  Timing is collected only when
        // collectCCTime is enabled and is restricted to one thread.
        std::atomic<std::clock_t> ccBfsCpuTicks{0};
        std::atomic<std::clock_t> ccDecrementCpuTicks{0};
        std::atomic<std::clock_t> ccIncrementCpuTicks{0};
        bool collectCCTime = false;
        bool debug = false;

        // 构建Decision-ZDNNF
        shared_ptr<DNNFNode> buildDecisionNode(int r, shared_ptr<DNNFNode> lo, shared_ptr<DNNFNode> hi);

        shared_ptr<DNNFNode> buildDecomposableNode(vector<shared_ptr<DNNFNode>>& subDNNFs);

        std::pair<DNNFResult, std::shared_ptr<DNNFNode>> DXD(Block& block, int depth);
        std::pair<DNNFResult, std::shared_ptr<DNNFNode>> serialSearch(
            vector<Block>& blocks, int depth,
            vector<DecompositionPolicy> policies);
        std::pair<DNNFResult, std::shared_ptr<DNNFNode>> parallelSearchUseOmp(
            vector<Block>& blocks, int depth, bool componentTreesAvailable,
            vector<DecompositionPolicy> policies);

        static constexpr uint8_t PROBE_LIMIT = 3;

        DecompositionPolicy decompositionPolicy;
        // Initial-split target selection: choose the smallest component when
        // the full instance area exceeds this value, otherwise the largest.
        size_t bfs_area_threshold = 100000;
        size_t ccEttMaxCalls = 0;
        std::atomic<size_t> ccEttCallsUsed{0};
        size_t quickStatsUpdateLimit = 0;

        bool isCCETTCallBudgetExhausted() const {
            return ccEttMaxCalls > 0 &&
                   ccEttCallsUsed.load(std::memory_order_relaxed) >= ccEttMaxCalls;
        }

        bool tryReserveETTCall() {
            if (ccEttMaxCalls == 0) {
                ccEttCallsUsed.fetch_add(1, std::memory_order_relaxed);
                return true;
            }

            size_t used = ccEttCallsUsed.load(std::memory_order_relaxed);
            while (used < ccEttMaxCalls) {
                if (ccEttCallsUsed.compare_exchange_weak(
                        used, used + 1,
                        std::memory_order_relaxed,
                        std::memory_order_relaxed)) {
                    return true;
                }
            }
            return false;
        }

        void updateMaxBlockCount(size_t count) {
            size_t current = MAX_B_COUNT.load(std::memory_order_relaxed);
            while (current < count &&
                   !MAX_B_COUNT.compare_exchange_weak(
                       current, count,
                       std::memory_order_relaxed,
                       std::memory_order_relaxed)) {
            }
        }

        void stopCCForETTCallBudget() {
            currentDecompositionPolicy().phase = CCProbePhase::Disabled;
            currentDecompositionPolicy().noSplitCount = 0;
        }

        DecompositionPolicy& currentDecompositionPolicy() {
            return isThreadLocal() ? tlsState->decompositionPolicy
                                   : decompositionPolicy;
        }

        const DecompositionPolicy& currentDecompositionPolicy() const {
            return isThreadLocal() ? tlsState->decompositionPolicy
                                   : decompositionPolicy;
        }

        bool shouldMaintainDynamicEtt() const {
            return useETT && !dxd_mode &&
                   currentDecompositionPolicy().phase == CCProbePhase::Ett;
        }

        void resetAdaptiveDecompositionState() {
            decompositionPolicy = DecompositionPolicy{
                CCProbePhase::Ett, EttFailureAction::FallbackToBfs, 0, false};
            ccEttCallsUsed.store(0, std::memory_order_relaxed);
        }

        static void updateDecompositionPolicy(DecompositionPolicy& policy,
                                              size_t numBlocks) {
            if (numBlocks > 1) {
                policy.phase = CCProbePhase::Disabled;
                policy.noSplitCount = 0;
                return;
            }
            if (numBlocks != 1 ||
                policy.phase == CCProbePhase::Disabled) {
                return;
            }

            ++policy.noSplitCount;
            if (policy.noSplitCount < PROBE_LIMIT) return;

            policy.noSplitCount = 0;
            if (policy.phase == CCProbePhase::Ett &&
                policy.ettFailureAction == EttFailureAction::FallbackToBfs) {
                policy.phase = CCProbePhase::Bfs;
            } else {
                policy.phase = CCProbePhase::Disabled;
            }
        }

        // 启动搜索函数
        void startDXD();
        void startMultiThreadDXD();

        void enableCCStatistics() {
            collectCCExperimentStats = true;
            ccExperimentStats.reset();
        }

        void enableQuickStatistics(size_t updates) {
            collectCCExperimentStats = true;
            quickStatsUpdateLimit = updates;
            ccExperimentStats.reset();
        }

        void enableCCTiming() { collectCCTime = true; }

        void setCCETTMaxCalls(size_t calls) {
            ccEttMaxCalls = calls;
        }

        void setBFSAreaThreshold(size_t area) {
            bfs_area_threshold = area == 0 ? size_t{100000} : area;
        }

        void setTimeLimit(long seconds) { timer.setTimeBound(seconds); }
        void logCCExperimentStats(bool complete, bool quick = false);

        // ── MDLX（统一接口，mode决定分块策略）
        DNNFResult parallelSearchMDLX(vector<Block>& blocks, DecomMode mode);
 
        // MDLX_impl: 统一递归搜索，根据mode选择分块方式和图更新策略
        DNNFResult MDLX(vector<int>& sols, Block& block, DecomMode mode);
 
        // start_MDLX: 统一入口，通过mode参数切换ETT/BFS
        void start_MDLX(DecomMode mode);

        /**
         * Counts unique nodes in the Decision-DNNF rooted at rootDNNF.
         * Terminal nodes (T, F) are excluded from the count.
         */
        size_t countDNNFNodes() const;

        size_t countZDDSize() const;

        void printAllSolutions(std::ostream& out = std::cout, size_t max_sols = 0) const;

        bool queryRecord (size_t key) {
            std::shared_lock<std::shared_mutex> readLock(recordMutex);
            return records.find(key) != records.end();
        }

        void insertRecord (size_t key) {
            std::unique_lock<std::shared_mutex> writeLock(recordMutex);
            records.insert(key);
        }

        Block getBlock() {
            Block fullBlock(rowsSet, colsSet);
            return fullBlock;
        };

        int getRecordCount(){
            std::shared_lock<std::shared_mutex> readLock(recordMutex);
            return detect_record;
        }

        void addRecordCount(){
            std::unique_lock<std::shared_mutex> writeLock(recordMutex);
            detect_record++;
        }

        void runDXZ() {
            turnOffGraphSync();
            controlOUTPUT = true;
            dxz_mode = true;
            startDXD();
        }


        void addTriedNumbers(int count) {
            std::lock_guard<std::mutex> lock(tried_numbers_mutex);
            tried_numbers += count;
        }

        void addCCCpuTicks(std::clock_t start, std::atomic<std::clock_t>& category) {
            if (!collectCCTime && !collectCCExperimentStats) return;
            const std::clock_t elapsed = std::clock() - start;
            category.fetch_add(elapsed, std::memory_order_relaxed);
        }

        static double ticksToSeconds(const std::atomic<std::clock_t>& ticks) {
            return static_cast<double>(ticks.load(std::memory_order_relaxed)) / CLOCKS_PER_SEC;
        }


    private:
        struct CacheKey {
            size_t hash;
            vector<int> cols;
            bool dxz;

            bool operator==(const CacheKey& other) const noexcept {
                return hash == other.hash && dxz == other.dxz &&
                       cols == other.cols;
            }
        };

        struct CacheKeyHash {
            size_t operator()(const CacheKey& key) const noexcept {
                return key.hash;
            }
        };

        struct CacheEntry {
            DNNFResult count;
            shared_ptr<DNNFNode> node;
        };

        struct DecisionNodeKey {
            int row;
            size_t loId;
            size_t hiId;

            bool operator==(const DecisionNodeKey& other) const noexcept {
                return row == other.row && loId == other.loId &&
                       hiId == other.hiId;
            }
        };

        struct DecisionNodeKeyHash {
            static uint64_t mix(uint64_t x) noexcept {
                x += 0x9e3779b97f4a7c15ULL;
                x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
                x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
                return x ^ (x >> 31);
            }

            size_t operator()(const DecisionNodeKey& key) const noexcept {
                uint64_t h = mix(static_cast<uint32_t>(key.row));
                h ^= mix(key.loId + 0x9e3779b97f4a7c15ULL);
                h ^= mix(key.hiId + 0xd6e8feb86659fd93ULL);
                return static_cast<size_t>(h);
            }
        };

        bool getCachedResult(const CacheKey& key, DNNFResult& count,
                             shared_ptr<DNNFNode>& node) const {
            std::shared_lock<std::shared_mutex> readLock(cacheMutex);
            auto it = cache.find(key);
            if (it == cache.end()) return false;

            count = it->second.count;
            node = it->second.node;
            return true;
        }

        void setCachedResult(const CacheKey& key, const DNNFResult& count,
                             const shared_ptr<DNNFNode>& node) {
            std::unique_lock<std::shared_mutex> writeLock(cacheMutex);
            cache.emplace(key, CacheEntry{count, node});
        }

        // ThreadPool& pool;
        Logger& logger;
        bool controlOUTPUT = false;

        // 并行阈值控制
        int max_threads = 0;               // 最大线程数（0 = 使用 hardware_concurrency）
        double sync_off_ratio = 0.75;      // 超过此比例即关闭图同步


        int tried_numbers = 0; // 已尝试的次数
        std::mutex tried_numbers_mutex;
        
        int num_of_zddNodes = 0; // 记录生成的ZDD节点数量
        // DNNF相关
        int num_of_DNNFNodes;
        int max_depth;
        vector<string> cache_input_order; // 记录缓存的输入顺序，便于输出
        std::shared_ptr<DNNFNode> rootDNNF;
        std::shared_ptr<DNNFNode> T = std::make_shared<DNNFNode>(NodeType::Terminal, -1, 1);
        std::shared_ptr<DNNFNode> F = std::make_shared<DNNFNode>(NodeType::Terminal, -2, 0);
        
        // DNNF缓存
        std::unordered_set<size_t> records; // 用于记录无法分解的矩阵状态

        mutable std::shared_mutex cacheMutex;
        mutable std::shared_mutex recordMutex; // 记录互斥锁
        unordered_map<CacheKey, CacheEntry, CacheKeyHash> cache;

        // DNNF Nodes table
        mutable std::shared_mutex tableMutex;
        unordered_map<DecisionNodeKey, std::weak_ptr<DNNFNode>,
                      DecisionNodeKeyHash> node_table;

        ThreadPool& getThreadPool(int poolSize) {
            return ThreadPoolManager::get_instance(poolSize);
        }
};


#endif
