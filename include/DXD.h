#ifndef DXD_H
#define DXD_H

#include "../include/DancingMatrix.h"
#include "../include/DXDTime.h"
#include <ctime>

const int MIN_BLOCK_ROWS = 20;
const int MAX_BLOCK_ROWS = 200;
const int TIME_LIMIT_SECONDS = 1500; 
const int TIME_LIMIT_BUILDING_SECONDS = 1200;
const int MAX_DECOMPOSE_TIMES = 5;
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
        size_t MAX_B_COUNT;

        double searchTime = 0.0;
        string solutionCount; // 记录解的数量
        bool timeout = false; // 是否超时
        bool isParallelSearch = false; // 是否并行搜索
        double decomposeTime = 0.0;
        // 累计连通分量处理的 CPU 时间。实验使用单线程运行，因此
        // std::clock() 的进程 CPU 时间就是当前算法线程消耗的 CPU 时间。
        std::atomic<std::clock_t> ccCpuTicks{0};
        std::atomic<std::clock_t> ccBfsCpuTicks{0};
        std::atomic<std::clock_t> ccUpdateCpuTicks{0};
        std::atomic<std::clock_t> ccIncrementCpuTicks{0};
        std::atomic<std::clock_t> ccCoverCpuTicks{0};
        std::atomic<std::clock_t> ccUncoverCpuTicks{0};
        bool collectCCTime = false;
        double nextCCStatsSnapshotTime = 0.0;
        bool debug = false;

        // 构建Decision-ZDNNF
        shared_ptr<DNNFNode> buildDecisionNode(int r, shared_ptr<DNNFNode> lo, shared_ptr<DNNFNode> hi);

        shared_ptr<DNNFNode> buildDecomposableNode(vector<shared_ptr<DNNFNode>>& subDNNFs);

        std::pair<DNNFResult, std::shared_ptr<DNNFNode>> DXD(Block& block, int depth);
        std::pair<DNNFResult, std::shared_ptr<DNNFNode>> serialSearch(
            vector<Block>& blocks, int depth, bool startIndependentPolicies = false);
        std::pair<DNNFResult, std::shared_ptr<DNNFNode>> parallelSearchUseOmp(
            vector<Block>& blocks, int depth, bool disableDynamicUpdates = false,
            bool componentTreesAvailable = true);

        static constexpr int NO_SPLIT_LIMIT = 3;

        bool dynamic_ett_disabled = false;
        bool decomposition_disabled = false;
        bool bfs_fallback = false;
        bool bfs_probe_done = false;
        size_t bfs_area_threshold = 100000;
        int main_no_split_count = 0;
        size_t ccEttMaxCalls = 0;
        size_t ccEttCallsUsed = 0;
        size_t quickStatsUpdateLimit = 0;

        bool isCCETTCallBudgetExhausted() const {
            return (collectCCExperimentStats || collectCCTime) && ccEttMaxCalls > 0 &&
                   ccEttCallsUsed >= ccEttMaxCalls;
        }

        void stopCCForETTCallBudget() {
            if (isThreadLocal()) {
                tlsState->dynamic_ett_disabled = true;
                tlsState->decomposition_disabled = true;
                tlsState->bfs_probe_done = true;
            } else {
                dynamic_ett_disabled = true;
                decomposition_disabled = true;
                bfs_probe_done = true;
                turnOffGraphSync();
            }
        }

        bool isCurrentDynamicEttDisabled() const {
            return dynamic_ett_disabled ||
                   (isThreadLocal() && tlsState->dynamic_ett_disabled);
        }

        bool isCurrentDecompositionDisabled() const {
            return decomposition_disabled ||
                   (isThreadLocal() && tlsState->decomposition_disabled);
        }

        void disableDynamicEttForCurrentState() {
            if (isThreadLocal()) {
                tlsState->dynamic_ett_disabled = true;
            } else {
                dynamic_ett_disabled = true;
            }
        }

        bool shouldTryDecompose(const Block& block) {
            if (dxz_mode) return false;
            if (dxd_mode) return true;

            if (isCCETTCallBudgetExhausted()) {
                stopCCForETTCallBudget();
                return false;
            }

            if (block.rows.size() <= 20 || block.cols.empty()) return false;

            if (isCurrentDecompositionDisabled()) return false;

            // After three unsuccessful ETT queries we stop maintaining the
            // dynamic forest immediately.  One final BFS probe is deferred
            // until the remaining matrix is small enough.
            if (!isThreadLocal() && bfs_fallback) {
                return !bfs_probe_done &&
                       block.rows.size() * block.cols.size() <= bfs_area_threshold;
            }

            // 一旦进入并行子块，ETT 不再更新。每个第一层子块
            // 只在足够小时允许一次 BFS 探测；嵌套子块直接求解。
            if (isThreadLocal() && tlsState->dynamic_ett_disabled) {
                return tlsState->decompose_depth <= 1 &&
                       !tlsState->bfs_probe_done &&
                       block.rows.size() * block.cols.size() <= bfs_area_threshold;
            }

            return true;
        }

        bool shouldUseDynamicEtt(Block& block, int depth) {
            if (!useETT || dxd_mode) return false;
            if (isCurrentDynamicEttDisabled()) return false;
            (void)block;
            (void)depth;
            return isThreadLocal() ? !tlsState->bfs_fallback : !bfs_fallback;
        }

        bool shouldMaintainDynamicEtt(int depth) const {
            (void)depth;
            return useETT && !dxd_mode && !isCurrentDynamicEttDisabled() &&
                   isGraphSyncEnabled();
        }

        bool isEttStatisticsPeriod(const Block& block) const {
            return block.rows.size() > ccEttThreshold;
        }

        void resetAdaptiveDecompositionState() {
            dynamic_ett_disabled = false;
            decomposition_disabled = false;
            bfs_fallback = false;
            bfs_probe_done = false;
            main_no_split_count = 0;
            ccEttCallsUsed = 0;
        }

        void updateAdaptiveDecompositionState(const Block& block, size_t numBlocks) {
            if (dxd_mode) return;

            if (isThreadLocal() && tlsState->dynamic_ett_disabled) {
                tlsState->bfs_probe_done = true;
                // This is the component's only BFS probe.  Regardless of
                // whether it splits, descendants do not query CC again.
                tlsState->decomposition_disabled = true;
                return;
            }

            if (!isThreadLocal() && bfs_fallback) {
                bfs_probe_done = true;
                decomposition_disabled = true;
                return;
            }

            if (numBlocks > 1) {
                if (isThreadLocal()) {
                    tlsState->no_split_count = 0;
                } else {
                    main_no_split_count = 0;
                }
                return;
            }

            if (isThreadLocal()) {
                ++tlsState->no_split_count;
                if (!tlsState->bfs_fallback &&
                    tlsState->no_split_count >= NO_SPLIT_LIMIT) {
                    tlsState->bfs_fallback = true;
                    tlsState->dynamic_ett_disabled = true;
                    tlsState->no_split_count = 0;
                }
                return;
            }

            ++main_no_split_count;
            if (!bfs_fallback && main_no_split_count >= NO_SPLIT_LIMIT) {
                bfs_fallback = true;
                dynamic_ett_disabled = true;
                main_no_split_count = 0;
                turnOffGraphSync();
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

        static size_t automaticCCETTThreshold(size_t instanceRows) {
            if (instanceRows > 2000) return 200;
            if (instanceRows > 1000) return 100;
            if (instanceRows > 100) return 50;
            return 30;
        }

        void setCCETTThreshold(size_t rows) {
            ccEttThreshold = rows == 0 ? automaticCCETTThreshold(ROWS) : rows;
        }

        void setCCETTMaxCalls(size_t calls) {
            ccEttMaxCalls = calls;
        }

        void setBFSAreaThreshold(size_t area) {
            bfs_area_threshold = area == 0 ? size_t{100000} : area;
        }

        void setTimeLimit(long seconds) { timer.setTimeBound(seconds); }
        void logCCExperimentStats(bool complete);

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

        std::shared_ptr<DNNFNode> getCache(const size_t& key){
            std::shared_lock<std::shared_mutex> readLock(cacheMutex);
            if(C.find(key) != C.end()){
                return C[key];
            }
            return nullptr;
        }

        void setCache(const size_t& key, std::shared_ptr<DNNFNode> node){
            std::unique_lock<std::shared_mutex> writeLock(cacheMutex);
            if(C.find(key) == C.end()){
                C[key] = node;
            }
        }

        void setCacheCount(const size_t& key, DNNFResult count){
            std::unique_lock<std::shared_mutex> writeLock(cacheMutex);
            countCache[key] = count;
        }

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


        inline size_t gen_key(int r, const DNNFNode* lo, const DNNFNode* hi) const {
            auto mix = [](size_t x) -> size_t {
                x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
                x ^= x >> 27; x *= 0x94d049bb133111ebULL;
                return x ^ (x >> 31);
            };
            size_t h = mix(static_cast<size_t>(r));
            h ^= mix(lo->id + 1ULL);
            h ^= mix(hi->id + 0x9e3779b9ULL);
            return h;
        }

        void addTriedNumbers(int count) {
            std::lock_guard<std::mutex> lock(tried_numbers_mutex);
            tried_numbers += count;
        }

        void addCCCpuTicks(std::clock_t start, std::atomic<std::clock_t>& category) {
            const std::clock_t elapsed = std::clock() - start;
            ccCpuTicks.fetch_add(elapsed, std::memory_order_relaxed);
            category.fetch_add(elapsed, std::memory_order_relaxed);
        }

        double getCCCpuTime() const {
            return static_cast<double>(ccCpuTicks.load(std::memory_order_relaxed)) /
                   static_cast<double>(CLOCKS_PER_SEC);
        }

        static double ticksToSeconds(const std::atomic<std::clock_t>& ticks) {
            return static_cast<double>(ticks.load(std::memory_order_relaxed)) / CLOCKS_PER_SEC;
        }


    private:
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
        unordered_map<size_t, shared_ptr<DNNFNode>> C;

        // DNNF Nodes table
        mutable std::shared_mutex tableMutex;
        unordered_map<size_t, std::weak_ptr<DNNFNode>> node_table;

         // count cache (state → count)
        unordered_map<size_t, DNNFResult> countCache;

        ThreadPool& getThreadPool(int poolSize) {
            return ThreadPoolManager::get_instance(poolSize);
        }
};


#endif
