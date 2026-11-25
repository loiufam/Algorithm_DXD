#ifndef LEGACY_HYBRID_DXD_H
#define LEGACY_HYBRID_DXD_H

#include "DXD.h"

#include <atomic>
#include <cstdint>
#include <fstream>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

class LegacyHybridDXD final : public DanceDNNF {
public:
    enum class SearchMode {
        BfsProbe,
        MinEttTrack,
        EttPath,
        MaxBfsTrack,
        NoDecomposition
    };

    struct ProbeContext {
        uint64_t initialArea = 0;
        bool triggered = false;
    };

    struct HybridSearchState {
        SearchMode mode = SearchMode::BfsProbe;
        uint64_t activeRows = 0;
        std::shared_ptr<ProbeContext> probeContext;
    };

    struct HybridStatistics {
        std::atomic<uint64_t> bfsQueries{0};
        std::atomic<uint64_t> bfsNanoseconds{0};
        std::atomic<uint64_t> ettBuilds{0};
        std::atomic<uint64_t> ettBuildNanoseconds{0};
        std::atomic<uint64_t> ettDecrementUpdates{0};
        std::atomic<uint64_t> ettDecrementNanoseconds{0};
        std::atomic<uint64_t> ettIncrementUpdates{0};
        std::atomic<uint64_t> ettIncrementNanoseconds{0};
        std::atomic<uint64_t> ettCalls{0};
        std::atomic<uint64_t> ettExtractionNanoseconds{0};
        std::atomic<uint64_t> ettReleases{0};
        std::atomic<uint64_t> ettReleaseNanoseconds{0};
        std::atomic<uint64_t> initialProbeSelections{0};
        std::atomic<uint64_t> ettEntryDecisions{0};
        std::atomic<uint64_t> outerParallelLaunches{0};
        std::atomic<uint64_t> innerParallelLaunches{0};
        std::atomic<uint64_t> activeVertexSum{0};
        std::atomic<uint64_t> activeEdgeSum{0};
        std::atomic<uint64_t> activeTreeSum{0};
        std::atomic<uint64_t> activeNonTreeSum{0};
        std::atomic<uint64_t> deletedVertexSum{0};
        std::atomic<uint64_t> deletedEdgeSum{0};
        std::atomic<uint64_t> deletedTreeSum{0};
        std::atomic<uint64_t> deletedNonTreeSum{0};
        std::atomic<uint64_t> replacementScanSum{0};
        std::atomic<uint64_t> treeEdgeLinkSum{0};

        void reset() {
            bfsQueries.store(0, std::memory_order_relaxed);
            bfsNanoseconds.store(0, std::memory_order_relaxed);
            ettBuilds.store(0, std::memory_order_relaxed);
            ettBuildNanoseconds.store(0, std::memory_order_relaxed);
            ettDecrementUpdates.store(0, std::memory_order_relaxed);
            ettDecrementNanoseconds.store(0, std::memory_order_relaxed);
            ettIncrementUpdates.store(0, std::memory_order_relaxed);
            ettIncrementNanoseconds.store(0, std::memory_order_relaxed);
            ettCalls.store(0, std::memory_order_relaxed);
            ettExtractionNanoseconds.store(0, std::memory_order_relaxed);
            ettReleases.store(0, std::memory_order_relaxed);
            ettReleaseNanoseconds.store(0, std::memory_order_relaxed);
            initialProbeSelections.store(0, std::memory_order_relaxed);
            ettEntryDecisions.store(0, std::memory_order_relaxed);
            outerParallelLaunches.store(0, std::memory_order_relaxed);
            innerParallelLaunches.store(0, std::memory_order_relaxed);
            activeVertexSum.store(0, std::memory_order_relaxed);
            activeEdgeSum.store(0, std::memory_order_relaxed);
            activeTreeSum.store(0, std::memory_order_relaxed);
            activeNonTreeSum.store(0, std::memory_order_relaxed);
            deletedVertexSum.store(0, std::memory_order_relaxed);
            deletedEdgeSum.store(0, std::memory_order_relaxed);
            deletedTreeSum.store(0, std::memory_order_relaxed);
            deletedNonTreeSum.store(0, std::memory_order_relaxed);
            replacementScanSum.store(0, std::memory_order_relaxed);
            treeEdgeLinkSum.store(0, std::memory_order_relaxed);
        }
    };

    struct HybridConfig {
        int outerParallelThreads = 8;
        int innerParallelThreads = 6;
        int ettAreaDivisor = 10;
        long double bfsAreaRatio = 0.5L;
    };

    LegacyHybridDXD(const string& file_path, Logger& logger,
              int pool_size = 8, bool debug = false);
    ~LegacyHybridDXD();

    void setEttAreaDivisor(int divisor) {
        config_.ettAreaDivisor = std::max(divisor, 1);
    }
    void setDecisionLog(const std::string& path) {
        decisionLogPath_ = path;
        if (!decisionLogPath_.empty() && !collectCCExperimentStats) {
            enableCCStatistics();
        }
    }
    void startHybridDXD();

    Logger& legacyLogger() { return logger; }
    const shared_ptr<DNNFNode>& legacyT() const { return T; }
    const shared_ptr<DNNFNode>& legacyF() const { return F; }
    bool legacyDebug() const { return debug; }

private:
    class TaskScheduler;
    using SearchResult = std::pair<DNNFResult, shared_ptr<DNNFNode>>;

    SearchResult hybridSearch(Block& block, int depth,
                              const HybridSearchState& state);
    SearchResult searchIndependentBlocks(
        vector<Block>& blocks, const vector<HybridSearchState>& states,
        int depth, bool nestedEttParallel);
    SearchResult searchOneIndependentBlock(
        Block& block, const HybridSearchState& state, int depth);

    size_t selectSmallestBlock(const vector<Block>& blocks) const;
    size_t selectLargestBlock(const vector<Block>& blocks) const;
    set<int> materializeActiveRows(const set<int>& cols) const;
    void buildProbeEtt(const Block& block);
    uint64_t currentTreeEdgeCount() const;
    uint64_t applyDecrement(
        const set<int>& deletedRows,
        int depth,
        uint64_t areaBeforeCover,
        uint64_t areaAfterCover,
        uint64_t stopArea);
    void applyIncrement(const set<int>& restoredRows);
    void recordDecrement(
        const CCUpdateStats& update,
        uint64_t treeEdgesBefore,
        int depth,
        uint64_t areaBeforeCover,
        uint64_t areaAfterCover,
        uint64_t stopArea,
        uint64_t elapsedNanoseconds);
    size_t currentEttComponentCount() const;
    void releaseLocalEtt();
    bool hasLocalEttStorage() const;
    bool statisticsEnabled() const {
        return debug || collectCCExperimentStats || decisionLog_.is_open();
    }
    static void addElapsedNanoseconds(
        std::atomic<uint64_t>& target,
        const std::chrono::steady_clock::time_point& start);
    void logSummary() const;

    HybridStatistics hybridStats_;
    HybridConfig config_;
    std::unique_ptr<TaskScheduler> taskScheduler_;
    uint64_t taskGrainArea_ = 0;
    std::string decisionLogPath_;
    std::ofstream decisionLog_;
    std::mutex decisionLogMutex_;
    std::atomic<uint64_t> decisionSequence_{0};
};

#endif
