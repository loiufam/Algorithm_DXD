#ifndef HYBRID_DXD_H
#define HYBRID_DXD_H

#include "DXD.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

class HybridDXD final : public DanceDNNF {
public:
    enum class SearchMode {
        BfsProbe,
        MinEttTrack,
        EttPath,
        MaxBfsTrack,
        BfsCheckpoint,
        NoDecomposition
    };

    struct ProbeContext {
        uint64_t initialArea = 0;
        bool triggered = false;
        int bfsCheckpointStartDepth = 0;
        int bfsCheckpointRemaining = 0;
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
        std::atomic<uint64_t> ettComponentChecks{0};
        std::atomic<uint64_t> ettExtractionNanoseconds{0};
        std::atomic<uint64_t> ettReleases{0};
        std::atomic<uint64_t> ettReleaseNanoseconds{0};
        std::atomic<uint64_t> initialProbeSelections{0};
        std::atomic<uint64_t> ettEntryDecisions{0};
        std::atomic<uint64_t> outerParallelLaunches{0};
        std::atomic<uint64_t> innerParallelLaunches{0};

        void reset() {
            bfsQueries.store(0, std::memory_order_relaxed);
            bfsNanoseconds.store(0, std::memory_order_relaxed);
            ettBuilds.store(0, std::memory_order_relaxed);
            ettBuildNanoseconds.store(0, std::memory_order_relaxed);
            ettDecrementUpdates.store(0, std::memory_order_relaxed);
            ettDecrementNanoseconds.store(0, std::memory_order_relaxed);
            ettComponentChecks.store(0, std::memory_order_relaxed);
            ettExtractionNanoseconds.store(0, std::memory_order_relaxed);
            ettReleases.store(0, std::memory_order_relaxed);
            ettReleaseNanoseconds.store(0, std::memory_order_relaxed);
            initialProbeSelections.store(0, std::memory_order_relaxed);
            ettEntryDecisions.store(0, std::memory_order_relaxed);
            outerParallelLaunches.store(0, std::memory_order_relaxed);
            innerParallelLaunches.store(0, std::memory_order_relaxed);
        }
    };

    struct HybridConfig {
        int outerParallelThreads = 8;
        int innerParallelThreads = 6;
        int ettAreaDivisor = 10;
    };

    HybridDXD(const string& file_path, Logger& logger,
              int pool_size = 8, bool debug = false);
    ~HybridDXD();

    void setEttAreaDivisor(int divisor) {
        config_.ettAreaDivisor = std::max(divisor, 1);
    }
    void startHybridDXD();

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
    uint64_t applyDecrement(const set<int>& deletedRows);
    size_t currentEttComponentCount() const;
    void releaseLocalEtt();
    bool hasLocalEttStorage() const;
    static void addElapsedNanoseconds(
        std::atomic<uint64_t>& target,
        const std::chrono::steady_clock::time_point& start);
    void logSummary() const;

    HybridStatistics hybridStats_;
    HybridConfig config_;
    std::unique_ptr<TaskScheduler> taskScheduler_;
    uint64_t taskGrainArea_ = 0;
};

#endif
