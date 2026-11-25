#ifndef DXD_H
#define DXD_H

#include "../include/DancingMatrix.h"
#include "../include/DXDTime.h"
#include <atomic>
#include <chrono>
#include <ctime>

const int TIME_LIMIT_SECONDS = 1500; 
using namespace std;

enum class NodeType { OR, Decision, Decomposed, Variable, Terminal };
enum class DecomMode { Dynamic, Static };
enum class DXDBfsPolicy { Always, Threshold };

class HybridDXD;

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

    // Search tasks reserve ID ranges and assign node IDs without touching the
    // global atomic counter for every node.
    DNNFNode(size_t assignedId, NodeType t, int l)
        : id(assignedId), type(t), label(l) {}

    DNNFNode(size_t assignedId, NodeType t,
             shared_ptr<DNNFNode> l, shared_ptr<DNNFNode> r)
        : id(assignedId), type(t), left(l), right(r) {}
 
    // Convenience: is this a terminal node?
    bool isTerminal() const { return type == NodeType::Terminal; }
};

class DanceDNNF : protected DancingMatrix {

    public:
        friend class HybridDXD;
        friend class LegacyHybridDXD;

        DanceDNNF(int rows, int cols, int** matrix, Logger& l,
                  bool verbose = false, int pool_size = 8)
            : DancingMatrix(rows, cols, matrix, verbose,
                            AlgorithmMode::DXD),
              logger(l) {
            
 
            timer.setTimeBound(TIME_LIMIT_SECONDS);

            std::cout<< "初始化DanceDNNF完成." << endl;
        }

        DanceDNNF(const string& file_path, Logger& l,
                  AlgorithmMode mode, int pool_size = 1,
                  bool debug = false)
            : DancingMatrix(file_path, mode),
            logger(l),
            max_threads(pool_size), 
            debug(debug),
            num_of_DNNFNodes(0),
            max_depth(1) {

            omp_set_num_threads(pool_size);
            omp_set_max_active_levels(1);
            std::cout << "设置并行线程数为: " << pool_size << std::endl;

            if (pool_size > 1) isParallelSearch = true;
            timer.setTimeBound(TIME_LIMIT_SECONDS);
        }

        ~DanceDNNF() = default;

        CStopWatch timer;

        const int MAX_P_COUNT = 1;
        atomic<int> p_count{0};
        int detect_record = 0;
        std::atomic<size_t> MAX_B_COUNT{1};

        double searchTime = 0.0;
        string solutionCount;
        bool timeout = false;
        bool isParallelSearch = false;
        double decomposeTime = 0.0;
        // CC CPU timing categories.  Timing is collected only when
        // collectCCTime is enabled and is restricted to one thread.
        std::atomic<std::clock_t> ccBfsCpuTicks{0};
        std::atomic<std::clock_t> ccDecrementCpuTicks{0};
        std::atomic<std::clock_t> ccIncrementCpuTicks{0};
        std::atomic<uint64_t> ccBfsWallNanoseconds{0};
        bool collectCCTime = false;
        bool debug = false;
        bool skipDNNFSize = false;
        std::atomic<size_t> createdDNNFNodes{0};

        // Decision-ZDNNF
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
        DXDBfsPolicy dxdBfsPolicy = DXDBfsPolicy::Always;
        long double dxdBfsThresholdRatio = 0.5L;
        size_t ccEttMaxCalls = 0;
        std::atomic<size_t> ccEttCallsUsed{0};
        size_t ccBfsMaxCalls = 10;
        std::atomic<size_t> ccBfsCallsUsed{0};
        std::atomic<uint64_t> dxdBfsCalls{0};
        std::atomic<uint64_t> dxdMultiSplits{0};
        std::atomic<uint64_t> dxdThresholdStops{0};
        std::atomic<uint64_t>* bfsCallCounter = nullptr;
        std::atomic<uint64_t>* componentQueryCallCounter = nullptr;
        size_t quickStatsUpdateLimit = 0;
        bool pureDynDXD = false;

        bool isCCETTCallBudgetExhausted() const {
            return !pureDynDXD && ccEttMaxCalls > 0 &&
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

        bool isCCBFSCallBudgetExhausted() const {
            return ccBfsMaxCalls > 0 &&
                   ccBfsCallsUsed.load(std::memory_order_relaxed) >=
                       ccBfsMaxCalls;
        }

        bool tryReserveBFSCall() {
            if (ccBfsMaxCalls == 0) {
                ccBfsCallsUsed.fetch_add(1, std::memory_order_relaxed);
                return true;
            }

            size_t used = ccBfsCallsUsed.load(std::memory_order_relaxed);
            while (used < ccBfsMaxCalls) {
                if (ccBfsCallsUsed.compare_exchange_weak(
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
            if (pureDynDXD) return;

            currentDecompositionPolicy().phase =
                isCCBFSCallBudgetExhausted()
                    ? CCProbePhase::Disabled
                    : CCProbePhase::Bfs;
            currentDecompositionPolicy().noSplitCount = 0;
            currentDecompositionPolicy().skipNextProbe = false;
        }

        void stopCCForBFSCallBudget() {
            currentDecompositionPolicy().phase = CCProbePhase::Disabled;
            currentDecompositionPolicy().noSplitCount = 0;
            currentDecompositionPolicy().skipNextProbe = false;
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
            return algorithmMode == AlgorithmMode::DYNDXD &&
                   currentDecompositionPolicy().phase == CCProbePhase::Ett;
        }

        void resetAdaptiveDecompositionState() {
            decompositionPolicy = pureDynDXD
                ? DecompositionPolicy{
                      CCProbePhase::Ett, EttFailureAction::Disable, 0, false}
                : DecompositionPolicy{
                      CCProbePhase::Ett, EttFailureAction::FallbackToBfs, 0, false};
            ccEttCallsUsed.store(0, std::memory_order_relaxed);
            ccBfsCallsUsed.store(0, std::memory_order_relaxed);
        }

        void updateDecompositionPolicy(DecompositionPolicy& policy,
                                       size_t numBlocks) {
            if (pureDynDXD) {
                policy.phase = CCProbePhase::Ett;
                policy.ettFailureAction = EttFailureAction::Disable;
                policy.noSplitCount = 0;
                policy.skipNextProbe = false;
                return;
            }

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

        void setCCBFSMaxCalls(size_t calls) {
            ccBfsMaxCalls = calls;
        }

        void setPureDynDXD(bool pure) {
            pureDynDXD = pure;
            if (pureDynDXD) {
                ccEttMaxCalls = 0;
                ccBfsMaxCalls = 0;
            }
        }

        void setSkipDNNFSize(bool skip) {
            skipDNNFSize = skip;
        }

        void setBFSAreaThreshold(size_t area) {
            bfs_area_threshold = area == 0 ? size_t{100000} : area;
        }
        void setDXDBfsPolicy(DXDBfsPolicy policy) {
            dxdBfsPolicy = policy;
        }
        void setDXDBfsThresholdRatio(long double ratio) {
            dxdBfsThresholdRatio = ratio;
        }

        void setTimeLimit(long seconds) { timer.setTimeBound(seconds); }
        void logCCExperimentStats(bool complete, bool quick = false);

        DNNFResult parallelSearchMDLX(vector<Block>& blocks, DecomMode mode);
 
        DNNFResult MDLX(vector<int>& sols, Block& block, DecomMode mode);
 
        void start_MDLX(DecomMode mode);

        /**
         * Counts unique nodes in the Decision-DNNF rooted at rootDNNF.
         * Terminal nodes (T, F) are excluded from the count.
         */
        size_t countDNNFNodes() const {
            return createdDNNFNodes.load(std::memory_order_relaxed);
        }

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
            if (!isDXZMode()) {
                throw std::logic_error(
                    "runDXZ requires AlgorithmMode::DXZ");
            }
            turnOffGraphSync();
            controlOUTPUT = true;
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

        double ccBfsWallSeconds() const {
            return static_cast<double>(
                       ccBfsWallNanoseconds.load(
                           std::memory_order_relaxed)) /
                   1e9;
        }

    private:
        void beginCCBfsWallTime() {
            std::lock_guard<std::mutex> lock(ccBfsWallMutex_);
            if (ccBfsActiveCalls_++ == 0) {
                ccBfsWallStart_ = std::chrono::steady_clock::now();
            }
        }

        void endCCBfsWallTime() {
            std::lock_guard<std::mutex> lock(ccBfsWallMutex_);
            if (ccBfsActiveCalls_ == 0) return;
            if (--ccBfsActiveCalls_ != 0) return;
            const uint64_t elapsed =
                static_cast<uint64_t>(
                    std::chrono::duration_cast<
                        std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now() -
                        ccBfsWallStart_)
                        .count());
            ccBfsWallNanoseconds.fetch_add(
                elapsed, std::memory_order_relaxed);
        }

        void resetCCBfsWallTime() {
            std::lock_guard<std::mutex> lock(ccBfsWallMutex_);
            ccBfsActiveCalls_ = 0;
            ccBfsWallNanoseconds.store(0, std::memory_order_relaxed);
        }

        class CCBfsWallTimeScope {
        public:
            CCBfsWallTimeScope(DanceDNNF& owner, bool enabled)
                : owner_(owner), enabled_(enabled) {
                if (enabled_) owner_.beginCCBfsWallTime();
            }

            ~CCBfsWallTimeScope() {
                if (enabled_) owner_.endCCBfsWallTime();
            }

        private:
            DanceDNNF& owner_;
            bool enabled_;
        };

        std::mutex ccBfsWallMutex_;
        int ccBfsActiveCalls_ = 0;
        std::chrono::steady_clock::time_point ccBfsWallStart_{};

    public:


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

        struct SharedCacheEntry {
            CacheKey key;
            CacheEntry value;
        };

        class LockFreeDNNCache {
        public:
            explicit LockFreeDNNCache(
                size_t capacity = size_t{1} << 22)
                : capacity_(nextPowerOfTwo(capacity)),
                  mask_(capacity_ - 1),
                  slots_(std::make_unique<
                         std::atomic<SharedCacheEntry*>[]>(capacity_)) {
                for (size_t i = 0; i < capacity_; ++i) {
                    slots_[i].store(nullptr, std::memory_order_relaxed);
                }
            }

            ~LockFreeDNNCache() {
                for (size_t i = 0; i < capacity_; ++i) {
                    delete slots_[i].load(std::memory_order_relaxed);
                }
            }

            bool get(const CacheKey& key, CacheEntry& result) const {
                size_t index = key.hash & mask_;
                for (size_t probes = 0; probes < capacity_; ++probes) {
                    SharedCacheEntry* entry =
                        slots_[index].load(std::memory_order_acquire);
                    if (!entry) return false;
                    if (entry->key == key) {
                        result = entry->value;
                        return true;
                    }
                    index = (index + 1) & mask_;
                }
                return false;
            }

            void put(const CacheKey& key, const CacheEntry& value) {
                size_t index = key.hash & mask_;
                auto candidate =
                    std::make_unique<SharedCacheEntry>(
                        SharedCacheEntry{key, value});
                for (size_t probes = 0; probes < capacity_; ++probes) {
                    SharedCacheEntry* current =
                        slots_[index].load(std::memory_order_acquire);
                    if (!current) {
                        SharedCacheEntry* raw = candidate.get();
                        if (slots_[index].compare_exchange_strong(
                                current, raw,
                                std::memory_order_release,
                                std::memory_order_acquire)) {
                            candidate.release();
                            return;
                        }
                        if (current && current->key == key) return;
                    }
                    if (current->key == key) return;
                    index = (index + 1) & mask_;
                }
            }

        private:
            static size_t nextPowerOfTwo(size_t value) {
                size_t result = 1;
                while (result < value) result <<= 1;
                return result;
            }

            size_t capacity_;
            size_t mask_;
            std::unique_ptr<std::atomic<SharedCacheEntry*>[]> slots_;
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

        struct SearchState {
            static constexpr size_t NODE_ID_BLOCK_SIZE = size_t{1} << 20;

            bool collectStats = false;
            uint64_t cacheReads = 0;
            uint64_t cacheHits = 0;
            uint64_t cacheWrites = 0;
            uint64_t nodeLookups = 0;
            uint64_t nodeLookupHits = 0;
            uint64_t nodeInserts = 0;
            uint64_t nodesAllocated = 0;
            double cacheReadSeconds = 0.0;
            double cacheWriteSeconds = 0.0;
            double stateHashSeconds = 0.0;
            double chooseColumnSeconds = 0.0;
            double decompositionProbeSeconds = 0.0;
            double nodeLookupSeconds = 0.0;
            double nodeInsertSeconds = 0.0;
            double lockWaitSeconds = 0.0;
            uint64_t stateHashCalls = 0;
            uint64_t chooseColumnCalls = 0;
            uint64_t decompositionProbeCalls = 0;
            uint64_t cacheKeyColumns = 0;
            BlockMutationMetrics mutation;

            size_t nextNodeId = 0;
            size_t nodeIdLimit = 0;
            unordered_map<CacheKey, CacheEntry, CacheKeyHash> cache;
            unordered_map<DecisionNodeKey, std::weak_ptr<DNNFNode>,
                          DecisionNodeKeyHash> nodeTable;

            void reset(bool enableStats) {
                collectStats = enableStats;
                cacheReads = 0;
                cacheHits = 0;
                cacheWrites = 0;
                nodeLookups = 0;
                nodeLookupHits = 0;
                nodeInserts = 0;
                nodesAllocated = 0;
                cacheReadSeconds = 0.0;
                cacheWriteSeconds = 0.0;
                stateHashSeconds = 0.0;
                chooseColumnSeconds = 0.0;
                decompositionProbeSeconds = 0.0;
                nodeLookupSeconds = 0.0;
                nodeInsertSeconds = 0.0;
                lockWaitSeconds = 0.0;
                stateHashCalls = 0;
                chooseColumnCalls = 0;
                decompositionProbeCalls = 0;
                cacheKeyColumns = 0;
                mutation = BlockMutationMetrics{};
                mutation.enabled = enableStats;
                nextNodeId = 0;
                nodeIdLimit = 0;
                cache.clear();
                nodeTable.clear();
            }

            size_t allocateNodeId() {
                if (nextNodeId == nodeIdLimit) {
                    nextNodeId = DNNFNode::id_counter.fetch_add(
                        NODE_ID_BLOCK_SIZE, std::memory_order_relaxed);
                    nodeIdLimit = nextNodeId + NODE_ID_BLOCK_SIZE;
                }
                ++nodesAllocated;
                return nextNodeId++;
            }

            void mergeStatsFrom(const SearchState& other) {
                cacheReads += other.cacheReads;
                cacheHits += other.cacheHits;
                cacheWrites += other.cacheWrites;
                nodeLookups += other.nodeLookups;
                nodeLookupHits += other.nodeLookupHits;
                nodeInserts += other.nodeInserts;
                nodesAllocated += other.nodesAllocated;
                cacheReadSeconds += other.cacheReadSeconds;
                cacheWriteSeconds += other.cacheWriteSeconds;
                stateHashSeconds += other.stateHashSeconds;
                chooseColumnSeconds += other.chooseColumnSeconds;
                decompositionProbeSeconds += other.decompositionProbeSeconds;
                nodeLookupSeconds += other.nodeLookupSeconds;
                nodeInsertSeconds += other.nodeInsertSeconds;
                lockWaitSeconds += other.lockWaitSeconds;
                stateHashCalls += other.stateHashCalls;
                chooseColumnCalls += other.chooseColumnCalls;
                decompositionProbeCalls += other.decompositionProbeCalls;
                cacheKeyColumns += other.cacheKeyColumns;

                mutation.coverSeconds += other.mutation.coverSeconds;
                mutation.uncoverSeconds += other.mutation.uncoverSeconds;
                mutation.coverSetSeconds += other.mutation.coverSetSeconds;
                mutation.uncoverSetSeconds += other.mutation.uncoverSetSeconds;
                mutation.removedRowsClearSeconds +=
                    other.mutation.removedRowsClearSeconds;
                mutation.coverCalls += other.mutation.coverCalls;
                mutation.uncoverCalls += other.mutation.uncoverCalls;
                mutation.coverSetOps += other.mutation.coverSetOps;
                mutation.uncoverSetOps += other.mutation.uncoverSetOps;
                mutation.removedRowsClearCalls +=
                    other.mutation.removedRowsClearCalls;
            }
        };

        struct ThreadRunStats {
            int taskIndex = -1;
            int ompThread = -1;
            size_t rows = 0;
            size_t cols = 0;
            double solveSeconds = 0.0;
            uint64_t cacheReads = 0;
            uint64_t cacheHits = 0;
            uint64_t cacheWrites = 0;
            uint64_t nodeLookups = 0;
            uint64_t nodeLookupHits = 0;
            uint64_t nodeInserts = 0;
            uint64_t nodesAllocated = 0;
            double cacheReadSeconds = 0.0;
            double cacheWriteSeconds = 0.0;
            double stateHashSeconds = 0.0;
            double chooseColumnSeconds = 0.0;
            double decompositionProbeSeconds = 0.0;
            double nodeLookupSeconds = 0.0;
            double nodeInsertSeconds = 0.0;
            double lockWaitSeconds = 0.0;
            uint64_t stateHashCalls = 0;
            uint64_t chooseColumnCalls = 0;
            uint64_t decompositionProbeCalls = 0;
            uint64_t cacheKeyColumns = 0;
            double coverSeconds = 0.0;
            double uncoverSeconds = 0.0;
            double coverSetSeconds = 0.0;
            double uncoverSetSeconds = 0.0;
            double removedRowsClearSeconds = 0.0;
            uint64_t coverCalls = 0;
            uint64_t uncoverCalls = 0;
            uint64_t coverSetOps = 0;
            uint64_t uncoverSetOps = 0;
            uint64_t removedRowsClearCalls = 0;
        };

        static double elapsedSeconds(
            const std::chrono::steady_clock::time_point& start) {
            return std::chrono::duration_cast<std::chrono::duration<double>>(
                       std::chrono::steady_clock::now() - start)
                .count();
        }

        SearchState& currentSearchState() {
            return activeSearchState ? *activeSearchState : mainSearchState;
        }

        void logSearchStateStats(const string& label,
                                 const SearchState& state);

        bool getCachedResult(const CacheKey& key, DNNFResult& count,
                             shared_ptr<DNNFNode>& node) {
            SearchState& state = currentSearchState();
            const auto start = state.collectStats
                                   ? std::chrono::steady_clock::now()
                                   : std::chrono::steady_clock::time_point{};
            auto it = state.cache.find(key);
            if (state.collectStats) {
                state.cacheReadSeconds += elapsedSeconds(start);
                ++state.cacheReads;
            }
            if (it != state.cache.end()) {
                if (state.collectStats) ++state.cacheHits;
                count = it->second.count;
                node = it->second.node;
                return true;
            }

            if (!sharedDNNCache) return false;
            CacheEntry sharedEntry;
            if (!sharedDNNCache->get(key, sharedEntry)) return false;
            state.cache.emplace(
                key, CacheEntry{sharedEntry.count, sharedEntry.node});
            if (state.collectStats) ++state.cacheHits;
            count = sharedEntry.count;
            node = sharedEntry.node;
            return true;
        }

        void setCachedResult(const CacheKey& key, const DNNFResult& count,
                             const shared_ptr<DNNFNode>& node) {
            SearchState& state = currentSearchState();
            const auto start = state.collectStats
                                   ? std::chrono::steady_clock::now()
                                   : std::chrono::steady_clock::time_point{};
            state.cache.emplace(key, CacheEntry{count, node});
            if (sharedDNNCache) {
                sharedDNNCache->put(key, CacheEntry{count, node});
            }
            if (state.collectStats) {
                state.cacheWriteSeconds += elapsedSeconds(start);
                ++state.cacheWrites;
                state.cacheKeyColumns += key.cols.size();
            }
        }

        // ThreadPool& pool;
        Logger& logger;
        bool controlOUTPUT = false;

        int max_threads = 0;

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

        mutable std::shared_mutex recordMutex; // 记录互斥锁
        SearchState mainSearchState;
        std::shared_ptr<LockFreeDNNCache> sharedDNNCache;
        inline static thread_local SearchState* activeSearchState = nullptr;

        ThreadPool& getThreadPool(int poolSize) {
            return ThreadPoolManager::get_instance(poolSize);
        }
};


#endif
