#ifndef HYBRID_DXD_H
#define HYBRID_DXD_H

#include "DXD.h"

#include <atomic>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <memory>
#include <unordered_set>
#include <vector>

class HybridDXD final : public DanceDNNF {
public:
    enum class SearchMode {
        BfsProbe,
        BfsNext,
        BfsThreshold,
        DynPath,
        DynQuery,
        EttPath,
        AdaptivePath,
        NoDecomposition
    };

    struct DynamicSeed {
        std::vector<IncidenceEdge> treeEdges;
        std::vector<IncidenceEdge> nonTreeEdges;
        std::unordered_set<uint64_t> treeEdgeSet;
        std::unordered_set<uint64_t> nonTreeEdgeSet;
        set<int> activeRows;
        set<int> activeCols;
        uint64_t entries = 0;
        uint64_t nonTreeEntries = 0;
        uint64_t selectedBlockInitialArea = 0;
        long double thresholdRatio = 0.5L;
        bool ettBuilt = false;
        uint64_t rowOffset = 0;
    };

    struct ProbeContext {
        uint64_t initialArea = 0;
        long double stopRatio = 0.8L;
        bool triggered = false;
    };

    struct HybridSearchState {
        SearchMode mode = SearchMode::BfsProbe;
        uint64_t activeRows = 0;
        std::shared_ptr<ProbeContext> probeContext;
        std::shared_ptr<DynamicSeed> seed;
        bool skipInitialDecomposition = false;
        bool stopAfterQuery = false;
    };

    struct CostDecisionRecord {
        uint64_t decisionId = 0;
        std::string decisionKind = "cost";
        int depth = 0;
        uint64_t areaBeforeCover = 0;
        uint64_t areaAfterCover = 0;
        uint64_t stopArea = 0;
        uint64_t verticesBefore = 0;
        uint64_t edgesBefore = 0;
        uint64_t treeEdgesBefore = 0;
        uint64_t nonTreeEdgesBefore = 0;
        uint64_t verticesAfter = 0;
        uint64_t edgesAfter = 0;
        uint64_t treeEdgesAfter = 0;
        uint64_t nonTreeEdgesAfter = 0;
        uint64_t deletedVertices = 0;
        uint64_t deletedEdges = 0;
        uint64_t deletedTreeEdges = 0;
        uint64_t deletedNonTreeEdges = 0;
        uint64_t replacementScans = 0;
        uint64_t treeLinks = 0;
        long double logVerticesAfter = 0.0L;
        long double dynCost = 0.0L;
        long double bfsCost = 0.0L;
        bool dynAvailable = false;
        bool dynLocked = false;
        bool chooseDyn = false;
        bool thresholdStop = false;
        bool costEvaluated = true;
    };

    struct DynUpdateMetrics {
        uint64_t elapsedNanoseconds = 0;
        uint64_t verticesBefore = 0;
        uint64_t edgesBefore = 0;
        uint64_t treeEdgesBefore = 0;
        uint64_t nonTreeEdgesBefore = 0;
        uint64_t verticesAfter = 0;
        uint64_t edgesAfter = 0;
        uint64_t treeEdgesAfter = 0;
        uint64_t nonTreeEdgesAfter = 0;
        uint64_t deletedVertices = 0;
        uint64_t deletedEdges = 0;
        uint64_t deletedTreeEdges = 0;
        uint64_t deletedNonTreeEdges = 0;
        uint64_t replacementScans = 0;
        uint64_t treeLinks = 0;
    };

    struct HybridStatistics {
        std::atomic<uint64_t> bfsQueries{0};
        std::atomic<uint64_t> bfsNanoseconds{0};
        std::atomic<uint64_t> ettDecrementNanoseconds{0};
        std::atomic<uint64_t> ettExtractionNanoseconds{0};
        std::atomic<uint64_t> ettReleases{0};
        std::atomic<uint64_t> ettReleaseNanoseconds{0};
        std::atomic<uint64_t> initialProbeSelections{0};
        std::atomic<uint64_t> ettEntryDecisions{0};
        std::atomic<uint64_t> outerParallelLaunches{0};
        std::atomic<uint64_t> innerParallelLaunches{0};
        std::atomic<uint64_t> adaptiveComparisons{0};
        std::atomic<uint64_t> bfsStrategySelections{0};
        std::atomic<uint64_t> dynStrategySelections{0};
        std::atomic<uint64_t> componentQueryCalls{0};
        std::atomic<uint64_t> componentQueryNanoseconds{0};
        std::atomic<uint64_t> componentQueryCpuNanoseconds{0};
        std::atomic<uint64_t> bfsCpuNanoseconds{0};
        std::atomic<uint64_t> dynCpuNanoseconds{0};
        std::atomic<uint64_t> dynQueryCalls{0};
        std::atomic<uint64_t> dynQueryNanoseconds{0};
        std::atomic<uint64_t> dynDecrementCalls{0};
        std::atomic<uint64_t> dynDecrementNanoseconds{0};
        std::atomic<uint64_t> dynIncrementCalls{0};
        std::atomic<uint64_t> dynIncrementNanoseconds{0};
        std::atomic<uint64_t> bfsCalls{0};
        std::atomic<uint64_t> bfsNanosecondsNew{0};
        std::atomic<uint64_t> ettBuildCalls{0};
        std::atomic<uint64_t> ettBuildNanosecondsNew{0};
        std::atomic<uint64_t> splitEvents{0};
        std::atomic<uint64_t> activeVertexSum{0};
        std::atomic<uint64_t> activeEdgeSum{0};
        std::atomic<uint64_t> activeTreeSum{0};
        std::atomic<uint64_t> activeNonTreeSum{0};
        std::atomic<uint64_t> deletedVertexSum{0};
        std::atomic<uint64_t> deletedEdgeSum{0};
        std::atomic<uint64_t> deletedTreeSum{0};
        std::atomic<uint64_t> deletedNonTreeSum{0};
        std::atomic<uint64_t> replacementScanSum{0};
        std::atomic<uint64_t> treeCutSum{0};
        std::atomic<uint64_t> treeLinkSum{0};
        std::atomic<uint64_t> initialBfsComponents{0};
        std::atomic<uint64_t> initialBfsMultiSplits{0};
        std::atomic<uint64_t> initialDynamicRows{0};
        std::atomic<uint64_t> initialDynamicCols{0};
        std::atomic<uint64_t> initialDynamicEntries{0};
        std::atomic<uint64_t> initialDynamicTreeEdges{0};
        std::atomic<uint64_t> initialDynamicNonTreeEdges{0};
        std::atomic<uint64_t> blockTaskCalls{0};
        std::atomic<uint64_t> seedBlockTaskCalls{0};
        std::atomic<uint64_t> seedBlockNanoseconds{0};
        std::atomic<uint64_t> ordinaryBlockTaskCalls{0};
        std::atomic<uint64_t> ordinaryBlockNanoseconds{0};

        void reset() {
            bfsQueries.store(0, std::memory_order_relaxed);
            bfsNanoseconds.store(0, std::memory_order_relaxed);
            ettDecrementNanoseconds.store(0, std::memory_order_relaxed);
            ettExtractionNanoseconds.store(0, std::memory_order_relaxed);
            ettReleases.store(0, std::memory_order_relaxed);
            ettReleaseNanoseconds.store(0, std::memory_order_relaxed);
            initialProbeSelections.store(0, std::memory_order_relaxed);
            ettEntryDecisions.store(0, std::memory_order_relaxed);
            outerParallelLaunches.store(0, std::memory_order_relaxed);
            innerParallelLaunches.store(0, std::memory_order_relaxed);
            adaptiveComparisons.store(0, std::memory_order_relaxed);
            bfsStrategySelections.store(0, std::memory_order_relaxed);
            dynStrategySelections.store(0, std::memory_order_relaxed);
            componentQueryCalls.store(0, std::memory_order_relaxed);
            componentQueryNanoseconds.store(0, std::memory_order_relaxed);
            componentQueryCpuNanoseconds.store(
                0, std::memory_order_relaxed);
            bfsCpuNanoseconds.store(0, std::memory_order_relaxed);
            dynCpuNanoseconds.store(0, std::memory_order_relaxed);
            dynQueryCalls.store(0, std::memory_order_relaxed);
            dynQueryNanoseconds.store(0, std::memory_order_relaxed);
            dynDecrementCalls.store(0, std::memory_order_relaxed);
            dynDecrementNanoseconds.store(0, std::memory_order_relaxed);
            dynIncrementCalls.store(0, std::memory_order_relaxed);
            dynIncrementNanoseconds.store(0, std::memory_order_relaxed);
            bfsCalls.store(0, std::memory_order_relaxed);
            bfsNanosecondsNew.store(0, std::memory_order_relaxed);
            ettBuildCalls.store(0, std::memory_order_relaxed);
            ettBuildNanosecondsNew.store(0, std::memory_order_relaxed);
            splitEvents.store(0, std::memory_order_relaxed);
            activeVertexSum.store(0, std::memory_order_relaxed);
            activeEdgeSum.store(0, std::memory_order_relaxed);
            activeTreeSum.store(0, std::memory_order_relaxed);
            activeNonTreeSum.store(0, std::memory_order_relaxed);
            deletedVertexSum.store(0, std::memory_order_relaxed);
            deletedEdgeSum.store(0, std::memory_order_relaxed);
            deletedTreeSum.store(0, std::memory_order_relaxed);
            deletedNonTreeSum.store(0, std::memory_order_relaxed);
            replacementScanSum.store(0, std::memory_order_relaxed);
            treeCutSum.store(0, std::memory_order_relaxed);
            treeLinkSum.store(0, std::memory_order_relaxed);
            initialBfsComponents.store(0, std::memory_order_relaxed);
            initialBfsMultiSplits.store(0, std::memory_order_relaxed);
            initialDynamicRows.store(0, std::memory_order_relaxed);
            initialDynamicCols.store(0, std::memory_order_relaxed);
            initialDynamicEntries.store(0, std::memory_order_relaxed);
            initialDynamicTreeEdges.store(0, std::memory_order_relaxed);
            initialDynamicNonTreeEdges.store(0, std::memory_order_relaxed);
            blockTaskCalls.store(0, std::memory_order_relaxed);
            seedBlockTaskCalls.store(0, std::memory_order_relaxed);
            seedBlockNanoseconds.store(0, std::memory_order_relaxed);
            ordinaryBlockTaskCalls.store(0, std::memory_order_relaxed);
            ordinaryBlockNanoseconds.store(0, std::memory_order_relaxed);
        }
    };

    struct HybridConfig {
        int outerParallelThreads = 8;
        int innerParallelThreads = 6;
        long double ettStopRatio = 0.8L;
        bool ettAlwaysOn = false;
        bool adaptiveDecomposition = false;
        long double treeOperationProbability = 1.0L;
        long double thresholdRatio = 0.5L;
        bool runtimePolicy = true;
        bool maxSeedBlock = false;
        std::string decisionLogPath;
    };

    HybridDXD(const string& file_path, Logger& logger,
              int pool_size = 8, bool debug = false);
    ~HybridDXD();

    void setEttStopRatio(long double ratio) {
        config_.ettStopRatio = ratio;
    }
    void setEttAlwaysOn(bool enabled) {
        config_.ettAlwaysOn = enabled;
    }
    void setAdaptiveDecomposition(bool enabled) {
        config_.adaptiveDecomposition = enabled;
    }
    void setTreeOperationProbability(long double probability) {
        config_.treeOperationProbability = probability;
    }
    void setThresholdRatio(long double ratio) {
        config_.thresholdRatio = ratio;
        setDXDBfsThresholdRatio(ratio);
    }
    void setRuntimePolicy(bool enabled) {
        config_.runtimePolicy = enabled;
    }
    void setMaxSeedBlock(bool enabled) {
        config_.maxSeedBlock = enabled;
    }
    void setDecisionLog(const std::string& path) {
        config_.decisionLogPath = path;
    }
    void startHybridDXD();

private:
    class TaskScheduler;
    using SearchResult = std::pair<DNNFResult, shared_ptr<DNNFNode>>;
    using EttForestSnapshot =
        std::vector<std::unique_ptr<splaytree::EulerTourTree>>;

    SearchResult hybridSearchRuntime(
        Block& block, int depth,
        const HybridSearchState& state);
    SearchResult searchIndependentBlocks(
        vector<Block>& blocks, const vector<HybridSearchState>& states,
        int depth, bool nestedEttParallel);
    SearchResult searchOneIndependentBlock(
        Block& block, const HybridSearchState& state, int depth);

    IncidenceBfsResult runIncidenceBfs(Block& block);
    std::vector<Block> runPlainIncidenceBfs(Block& block);
    std::vector<Block> runDynComponentQuery(Block& block);
    SearchResult searchWithoutHybridDecomposition(Block& block, int depth);
    std::shared_ptr<DynamicSeed> makeDynamicSeed(
        const Block& block, const IncidenceBfsResult& bfs) const;
    bool usesPerBlockDecomposition() const {
        return config_.outerParallelThreads <= 1 ||
               collectCCExperimentStats;
    }
    std::vector<HybridSearchState> makePerBlockProbeStates(
        const vector<Block>& components,
        const IncidenceBfsResult* bfs) const;
    void recordInitialDynamicSeed(
        size_t componentCount,
        const std::shared_ptr<DynamicSeed>& seed);
    void buildSeedEtt(const std::shared_ptr<DynamicSeed>& seed);
    void rebuildSeedEttForest(const std::shared_ptr<DynamicSeed>& seed);
    EttForestSnapshot cloneLocalEttForest() const;
    void restoreLocalEttForest(
        const std::shared_ptr<DynamicSeed>& seed,
        const EttForestSnapshot& snapshot);
    void captureSeedTreeEdges(DynamicSeed& seed) const;
    std::vector<Block> extractIncidenceComponents(
        const set<int>& targetCols) const;
    bool isTreeEdgeInForest(int row, int column) const;
    uint64_t countTreeEdgesInForest() const;
    void markSeedDeleted(
        const std::shared_ptr<DynamicSeed>& seed,
        const set<int>& deletedRows,
        const set<int>& deletedCols);
    DynUpdateMetrics updateSeedEtt(
        const std::shared_ptr<DynamicSeed>& seed,
        const set<int>& deletedRows,
        const std::set<int>& deletedCols);
    uint64_t countDeletedSeedEdges(
        const DynamicSeed& seed,
        const set<int>& deletedRows,
        const set<int>& deletedCols,
        uint64_t* deletedTreeEdges) const;
    CostDecisionRecord evaluateCostDecision(
        int depth,
        uint64_t areaBeforeCover,
        uint64_t areaAfterCover,
        long double stopArea,
        const DynamicSeed& seed,
        const set<int>& deletedRows,
        const set<int>& deletedCols);
    bool statisticsEnabled() const {
        return debug || collectCCExperimentStats ||
               decisionLog_.is_open();
    }
    void recordDecrementStatistics(
        uint64_t elapsedNanoseconds,
        uint64_t verticesBefore,
        uint64_t edgesBefore,
        uint64_t treeEdgesBefore,
        uint64_t nonTreeEdgesBefore,
        uint64_t deletedVertices,
        uint64_t deletedEdges,
        uint64_t deletedTreeEdges,
        uint64_t replacementScans,
        uint64_t treeLinks);
    void recordCostDecision(
        const CostDecisionRecord& record,
        const DynUpdateMetrics* update = nullptr);
    void recordBlockTaskTiming(bool seeded, uint64_t nanoseconds);
    void configureBfsCounting(vector<Block>& blocks) const;

    size_t selectSeedBlock(const vector<Block>& blocks) const;
    size_t selectMaximumBlock(const vector<Block>& blocks) const;
    void releaseLocalEtt();
    bool hasLocalEttStorage() const;
    static void addElapsedNanoseconds(
        std::atomic<uint64_t>& target,
        const std::chrono::steady_clock::time_point& start);
    static uint64_t cpuNanosecondsSince(std::clock_t start);
    template <typename T>
    void logMetric(const string& name, const T& value) const {
        logger.logLine("Hybrid " + name + ": " + std::to_string(value));
    }
    void logSeconds(const string& name, double seconds) const {
        logger.logLine(
            "Hybrid " + name + ": " + std::to_string(seconds) + " s");
    }
    void logSummary() const;

    HybridStatistics hybridStats_;
    HybridConfig config_;
    std::unique_ptr<TaskScheduler> taskScheduler_;
    std::ofstream decisionLog_;
    std::mutex decisionLogMutex_;
    std::atomic<uint64_t> decisionSequence_{0};
};

#endif
