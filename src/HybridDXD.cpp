#include "../include/HybridDXD.h"

#include <algorithm>
#include <condition_variable>
#include <stdexcept>
#include <thread>

class HybridDXD::TaskScheduler {
public:
    TaskScheduler(HybridDXD& owner, int threadCount)
        : owner_(owner),
          threadCount_(threadCount),
          capacity_(nextPowerOfTwo(1u << 14)),
          mask_(capacity_ - 1),
          buffer_(std::make_unique<Cell[]>(capacity_)) {
        for (size_t i = 0; i < capacity_; ++i) {
            buffer_[i].sequence.store(i, std::memory_order_relaxed);
        }
        const int workers = std::max(0, threadCount - 1);
        workers_.reserve(static_cast<size_t>(workers));
        for (int i = 0; i < workers; ++i) {
            workers_.emplace_back([this] { workerLoop(); });
        }
    }

    ~TaskScheduler() {
        stop_.store(true, std::memory_order_release);
        workCondition_.notify_all();
        for (std::thread& worker : workers_) {
            if (worker.joinable()) worker.join();
        }
    }

    int idleThreads() const {
        return std::max(
            0, threadCount_ -
                   activeTasks_.load(std::memory_order_relaxed));
    }

    HybridDXD::SearchResult runGroup(
        vector<Block>& blocks,
        const vector<HybridSearchState>& states,
        int depth, bool nested) {
        if (nested) {
            owner_.hybridStats_.innerParallelLaunches.fetch_add(
                1, std::memory_order_relaxed);
        } else {
            owner_.hybridStats_.outerParallelLaunches.fetch_add(
                1, std::memory_order_relaxed);
        }
        const int count = static_cast<int>(blocks.size());
        if (count <= 1) {
            if (count == 0) return {DNNFResult(1), owner_.T};
            HybridSearchState taskState = states.front();
            if (taskState.activeRows == 0) {
                taskState.activeRows = blocks.front().rows.size();
            }
            return owner_.searchOneIndependentBlock(
                blocks.front(), taskState, depth + 1);
        }

        auto group = std::make_shared<Group>(count);
        for (int i = 0; i < count; ++i) {
            HybridSearchState taskState = states[i];
            if (taskState.activeRows == 0) {
                taskState.activeRows = blocks[i].rows.size();
            }
            enqueue(Task{std::move(blocks[i]), std::move(taskState),
                         depth + 1, i, group});
        }

        while (group->pending.load(std::memory_order_acquire) > 0) {
            Task task;
            if (tryPop(task)) {
                execute(task);
            } else {
                std::this_thread::yield();
            }
        }

        if (group->timeout.load(std::memory_order_acquire)) {
            throw std::runtime_error("Time bound broken");
        }
        if (group->failure.load(std::memory_order_acquire)) {
            return {DNNFResult(0), owner_.F};
        }

        if (owner_.debug) {
            vector<double> times = group->seconds;
            std::sort(times.begin(), times.end(),
                      std::greater<double>());
            double total = 0.0;
            for (double value : times) total += value;
            owner_.logger.logLine(
                "[hybrid-task-group] tasks=" +
                std::to_string(count) +
                " cpu=" + std::to_string(total) +
                " max=" + std::to_string(times.front()) +
                " min=" + std::to_string(times.back()));
        }

        DNNFResult totalResult(1);
        vector<shared_ptr<DNNFNode>> subNodes;
        subNodes.reserve(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i) {
            totalResult = totalResult * group->results[i];
            if (group->nodes[i]) subNodes.push_back(group->nodes[i]);
        }
        return {totalResult, owner_.buildDecomposableNode(subNodes)};
    }

private:
    struct Group {
        explicit Group(int n)
            : pending(n), results(static_cast<size_t>(n)),
              nodes(static_cast<size_t>(n), nullptr),
              seconds(static_cast<size_t>(n), 0.0) {}

        std::atomic<int> pending{0};
        std::atomic<bool> failure{false};
        std::atomic<bool> timeout{false};
        vector<DNNFResult> results;
        vector<shared_ptr<DNNFNode>> nodes;
        vector<double> seconds;
    };

    struct Task {
        Task() = default;
        Task(Block b, HybridSearchState s, int d, int i,
             shared_ptr<Group> g)
            : block(std::move(b)), state(std::move(s)),
              depth(d), index(i), group(std::move(g)) {}

        Block block;
        HybridSearchState state;
        int depth = 0;
        int index = 0;
        shared_ptr<Group> group;
    };

    struct Cell {
        std::atomic<size_t> sequence{0};
        Task task;
    };

    static size_t nextPowerOfTwo(size_t value) {
        size_t result = 1;
        while (result < value) result <<= 1;
        return result;
    }

    void enqueue(Task task) {
        size_t position = tail_.load(std::memory_order_relaxed);
        for (;;) {
            Cell& cell = buffer_[position & mask_];
            const size_t sequence =
                cell.sequence.load(std::memory_order_acquire);
            const intptr_t difference =
                static_cast<intptr_t>(sequence) -
                static_cast<intptr_t>(position);
            if (difference == 0) {
                if (tail_.compare_exchange_weak(
                        position, position + 1,
                        std::memory_order_relaxed,
                        std::memory_order_relaxed)) {
                    cell.task = std::move(task);
                    cell.sequence.store(
                        position + 1, std::memory_order_release);
                    workCondition_.notify_one();
                    return;
                }
            } else if (difference < 0) {
                std::this_thread::yield();
                position = tail_.load(std::memory_order_relaxed);
            } else {
                position = tail_.load(std::memory_order_relaxed);
            }
        }
    }

    bool tryPop(Task& task) {
        size_t position = head_.load(std::memory_order_relaxed);
        for (;;) {
            Cell& cell = buffer_[position & mask_];
            const size_t sequence =
                cell.sequence.load(std::memory_order_acquire);
            const intptr_t difference =
                static_cast<intptr_t>(sequence) -
                static_cast<intptr_t>(position + 1);
            if (difference == 0) {
                if (head_.compare_exchange_weak(
                        position, position + 1,
                        std::memory_order_relaxed,
                        std::memory_order_relaxed)) {
                    task = std::move(cell.task);
                    cell.sequence.store(
                        position + capacity_,
                        std::memory_order_release);
                    return true;
                }
            } else if (difference < 0) {
                return false;
            } else {
                position = head_.load(std::memory_order_relaxed);
            }
        }
    }

    void workerLoop() {
        while (!stop_.load(std::memory_order_acquire)) {
            Task task;
            if (tryPop(task)) {
                execute(task);
                continue;
            }
            std::unique_lock<std::mutex> lock(waitMutex_);
            workCondition_.wait_for(
                lock, std::chrono::milliseconds(2), [this] {
                    return stop_.load(std::memory_order_acquire);
                });
        }
    }

    void execute(Task& task) {
        const auto taskStart = std::chrono::steady_clock::now();
        if (task.group->failure.load(std::memory_order_acquire) ||
            task.group->timeout.load(std::memory_order_acquire)) {
            task.group->pending.fetch_sub(
                1, std::memory_order_acq_rel);
            return;
        }
        activeTasks_.fetch_add(1, std::memory_order_relaxed);

        std::unique_ptr<DancingMatrix::ThreadLocalState> outerTls;
        if (owner_.isThreadLocal()) outerTls = std::move(owner_.tlsState);

        try {
            HybridDXD::SearchResult result =
                owner_.searchOneIndependentBlock(
                    task.block, task.state, task.depth);
            if (result.first.isZero()) {
                task.group->failure.store(
                    true, std::memory_order_release);
            } else {
                task.group->results[task.index] = result.first;
                task.group->nodes[task.index] = result.second;
            }
        } catch (const std::runtime_error& e) {
            if (std::string(e.what()).find("Time bound") !=
                std::string::npos) {
                task.group->timeout.store(
                    true, std::memory_order_release);
            } else {
                task.group->failure.store(
                    true, std::memory_order_release);
            }
        } catch (...) {
            task.group->failure.store(
                true, std::memory_order_release);
        }

        owner_.cleanupThreadLocalState();
        owner_.tlsState = std::move(outerTls);
        task.group->seconds[task.index] =
            std::chrono::duration_cast<std::chrono::duration<double>>(
                std::chrono::steady_clock::now() - taskStart)
                .count();
        activeTasks_.fetch_sub(1, std::memory_order_relaxed);
        task.group->pending.fetch_sub(1, std::memory_order_acq_rel);
    }

    HybridDXD& owner_;
    const int threadCount_;
    const size_t capacity_;
    const size_t mask_;
    std::unique_ptr<Cell[]> buffer_;
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
    std::atomic<bool> stop_{false};
    std::atomic<int> activeTasks_{0};
    std::mutex waitMutex_;
    std::condition_variable workCondition_;
    vector<std::thread> workers_;
};

HybridDXD::HybridDXD(const string& file_path, Logger& logger,
                     int pool_size, bool debug)
    : DanceDNNF(file_path, logger, AlgorithmMode::HYBRID_DXD,
                pool_size, debug) {
}

HybridDXD::~HybridDXD() = default;

void HybridDXD::startHybridDXD() {
    if (!isHybridDXD()) {
        throw std::logic_error(
            "startHybridDXD requires AlgorithmMode::HYBRID_DXD");
    }

    MAX_B_COUNT.store(1, std::memory_order_relaxed);
    createdDNNFNodes.store(0, std::memory_order_relaxed);
    ccBfsCpuTicks.store(0, std::memory_order_relaxed);
    ccDecrementCpuTicks.store(0, std::memory_order_relaxed);
    ccIncrementCpuTicks.store(0, std::memory_order_relaxed);
    hybridStats_.reset();

    activeSearchState = nullptr;
    mainSearchState.reset(debug);

    try {
        const int hardwareThreads = std::max(
            1u, std::thread::hardware_concurrency());
        const int requestedThreads = max_threads > 0 ? max_threads : 8;
        config_.outerParallelThreads =
            std::max(1, std::min(requestedThreads, hardwareThreads));
        config_.innerParallelThreads =
            std::max(1, std::min(
                config_.innerParallelThreads,
                config_.outerParallelThreads));
        taskGrainArea_ = std::max<uint64_t>(
            4096,
            static_cast<uint64_t>(InitBlock.rows.size()) *
                InitBlock.cols.size() /
                static_cast<uint64_t>(
                    config_.outerParallelThreads * 16));
        sharedDNNCache =
            std::make_shared<LockFreeDNNCache>();
        taskScheduler_ = std::make_unique<TaskScheduler>(
            *this, config_.outerParallelThreads);
        initThreadLocalState(InitBlock, nullptr);

        timer.reset();
        timer.markStartTime();
        const auto start = std::chrono::high_resolution_clock::now();

        auto [result, compiledRoot] =
            hybridSearch(InitBlock, 1, HybridSearchState{});
        rootDNNF = compiledRoot;

        const auto end = std::chrono::high_resolution_clock::now();
        timer.markStopTime();
        searchTime =
            std::chrono::duration_cast<std::chrono::duration<double>>(
                end - start)
                .count();

        taskScheduler_.reset();
        sharedDNNCache.reset();
        releaseLocalEtt();
        cleanupThreadLocalState();

        timeout = false;
        solutionCount = result.toString();
        logger.logLine("Time: " + std::to_string(searchTime) + " s");
        if (debug) {
            logSearchStateStats("hybrid-main", mainSearchState);
        }
        if (solutionCount.size() > 12) {
            logger.logLine("Solutions (scientific): " +
                           result.toScientificString(3));
        } else {
            logger.logLine("Solutions: " + solutionCount);
        }
        logger.logLine(
            "Max Blocks: " +
            std::to_string(MAX_B_COUNT.load(std::memory_order_relaxed)));
        if (!skipDNNFSize) {
            logger.logLine(
                "DNNF Size: " + std::to_string(countDNNFNodes()));
        }
        if (debug) logSummary();
    } catch (const std::runtime_error& e) {
        taskScheduler_.reset();
        sharedDNNCache.reset();
        releaseLocalEtt();
        cleanupThreadLocalState();
        timer.markStopTime();

        const std::string message = e.what();
        if (message.find("Time bound") == std::string::npos) {
            logger.logLine("HybridDXD内部错误: " + message);
            throw;
        }

        timeout = true;
        logger.logLine("Time: " + std::to_string(timer.getElapsedTime()) + " s");
        logger.logLine("HybridDXD搜索超时: " + message);
        if (debug) logSummary();
    }
}

HybridDXD::SearchResult HybridDXD::hybridSearch(
    Block& block, int depth, const HybridSearchState& initialState) {
    if (timer.timeBoundBroken()) {
        throw std::runtime_error("Time bound broken");
    }

    HybridSearchState state = initialState;
    SearchMode mode = state.mode;

    if (block.cols.empty()) {
        return {DNNFResult(1), T};
    }

    vector<int> stateCols;
    const size_t stateHash = hashBlockState(block.cols, &stateCols);
    CacheKey stateKey{stateHash, std::move(stateCols), false};

    DNNFResult cachedCount(0);
    shared_ptr<DNNFNode> cachedNode;
    if (getCachedResult(stateKey, cachedCount, cachedNode)) {
        return {cachedCount, cachedNode};
    }

    if (mode == SearchMode::BfsProbe) {
        BFSScanMetrics bfsMetrics;
        const auto queryStart = std::chrono::steady_clock::now();
        vector<Block> components =
            getComponentsByBFS(block.cols, &bfsMetrics);
        addElapsedNanoseconds(hybridStats_.bfsNanoseconds, queryStart);
        hybridStats_.bfsQueries.fetch_add(1, std::memory_order_relaxed);
        updateMaxBlockCount(components.size());

        if (components.empty()) {
            setCachedResult(stateKey, DNNFResult(0), F);
            return {DNNFResult(0), F};
        }
        if (components.size() > 1) {
            vector<HybridSearchState> states(
                components.size(),
                HybridSearchState{SearchMode::NoDecomposition});
            const size_t smallest = selectSmallestBlock(components);
            const size_t largest = selectLargestBlock(components);
            states[smallest].mode = SearchMode::MinEttTrack;
            states[smallest].activeRows =
                components[smallest].rows.size();
            states[smallest].probeContext =
                std::make_shared<ProbeContext>();
            states[smallest].probeContext->initialArea =
                static_cast<uint64_t>(
                    components[smallest].rows.size()) *
                components[smallest].cols.size();
            if (largest != smallest) {
                states[largest].mode = SearchMode::MaxBfsTrack;
                states[largest].activeRows =
                    components[largest].rows.size();
                states[largest].probeContext =
                    std::make_shared<ProbeContext>();
                states[largest].probeContext->initialArea =
                    static_cast<uint64_t>(
                        components[largest].rows.size()) *
                    components[largest].cols.size();
            }
            hybridStats_.initialProbeSelections.fetch_add(
                largest != smallest ? 2 : 1,
                std::memory_order_relaxed);
            hybridStats_.ettEntryDecisions.fetch_add(
                1, std::memory_order_relaxed);

            auto result = searchIndependentBlocks(
                components, states, depth, false);
            setCachedResult(stateKey, result.first, result.second);
            return result;
        }
        mode = SearchMode::MinEttTrack;
        state.activeRows = block.rows.size();
        state.probeContext = std::make_shared<ProbeContext>();
        state.probeContext->initialArea =
            static_cast<uint64_t>(block.rows.size()) *
            block.cols.size();
        hybridStats_.ettEntryDecisions.fetch_add(
            1, std::memory_order_relaxed);
    }

    if (mode == SearchMode::BfsCheckpoint) {
        ProbeContext& probe = *state.probeContext;
        if (probe.bfsCheckpointRemaining <= 0) {
            mode = SearchMode::NoDecomposition;
        } else if (depth >= probe.bfsCheckpointStartDepth) {
            --probe.bfsCheckpointRemaining;
            BFSScanMetrics checkpointMetrics;
            const auto checkpointStart =
                std::chrono::steady_clock::now();
            vector<Block> components =
                getComponentsByBFS(block.cols, &checkpointMetrics);
            addElapsedNanoseconds(
                hybridStats_.bfsNanoseconds, checkpointStart);
            hybridStats_.bfsQueries.fetch_add(
                1, std::memory_order_relaxed);
            updateMaxBlockCount(components.size());
            if (components.size() > 1) {
                vector<HybridSearchState> states(
                    components.size(),
                    HybridSearchState{SearchMode::NoDecomposition});
                auto result = searchIndependentBlocks(
                    components, states, depth, true);
                setCachedResult(stateKey, result.first, result.second);
                return result;
            }
            if (probe.bfsCheckpointRemaining <= 0) {
                mode = SearchMode::NoDecomposition;
            }
        }
    }

    if (mode == SearchMode::NoDecomposition &&
        taskScheduler_ &&
        depth % 4 == 0 &&
        taskScheduler_->idleThreads() > 0 &&
        state.activeRows * block.cols.size() > taskGrainArea_) {
        BFSScanMetrics taskSplitMetrics;
        const auto splitStart = std::chrono::steady_clock::now();
        vector<Block> components =
            getComponentsByBFS(block.cols, &taskSplitMetrics);
        addElapsedNanoseconds(
            hybridStats_.bfsNanoseconds, splitStart);
        hybridStats_.bfsQueries.fetch_add(
            1, std::memory_order_relaxed);
        updateMaxBlockCount(components.size());
        if (components.size() > 1) {
            vector<HybridSearchState> states(
                components.size(),
                HybridSearchState{SearchMode::NoDecomposition});
            auto result = searchIndependentBlocks(
                components, states, depth, true);
            setCachedResult(stateKey, result.first, result.second);
            return result;
        }
    }

    ColumnHeader* choose = selectOptimalColumn(block.cols);
    if (!choose || choose->size <= 0) {
        setCachedResult(stateKey, DNNFResult(0), F);
        return {DNNFResult(0), F};
    }

    set<int> commonDeletedRows;
    bool ettActive = mode == SearchMode::EttPath;
    const bool commonMaintainRows = ettActive;
    const uint64_t rowsBeforeCover = state.activeRows;
    const uint64_t commonRemovedRows =
        rowsBeforeCover > 0
            ? static_cast<uint64_t>(getColSize(choose->col))
            : 0;
    coverInBlock(
        choose->col, block, commonDeletedRows, commonMaintainRows);

    const uint64_t rowsAfterCover =
        rowsBeforeCover > commonRemovedRows
            ? rowsBeforeCover - commonRemovedRows
            : 0;
    const uint64_t residualArea =
        rowsAfterCover * block.cols.size();

    if (mode == SearchMode::MinEttTrack &&
        residualArea <
            static_cast<long double>(state.probeContext->initialArea) /
                config_.ettAreaDivisor) {
        ProbeContext& probe = *state.probeContext;
        if (!probe.triggered) {
            probe.triggered = true;
            block.rows = materializeActiveRows(block.cols);
            state.activeRows = block.rows.size();
            buildProbeEtt(block);
            hybridStats_.ettComponentChecks.fetch_add(
                1, std::memory_order_relaxed);
            mode = SearchMode::EttPath;
            ettActive = true;
            if (currentEttComponentCount() > 1) {
                const auto extractionStart =
                    std::chrono::steady_clock::now();
                vector<Block> components =
                    getComponentsByETT(block.cols);
                addElapsedNanoseconds(
                    hybridStats_.ettExtractionNanoseconds,
                    extractionStart);
                updateMaxBlockCount(components.size());
                releaseLocalEtt();
                vector<HybridSearchState> states(
                    components.size(),
                    HybridSearchState{SearchMode::NoDecomposition});
                auto result = searchIndependentBlocks(
                    components, states, depth, true);
                uncoverInBlock(
                    choose->col, block, commonMaintainRows);
                setCachedResult(stateKey, result.first, result.second);
                return result;
            }
        } else {
            mode = SearchMode::NoDecomposition;
        }
    } else if (mode == SearchMode::MaxBfsTrack &&
               residualArea <
                   static_cast<long double>(
                       state.probeContext->initialArea) *
                       0.5) {
        ProbeContext& probe = *state.probeContext;
        if (!probe.triggered) {
            probe.triggered = true;
            mode = SearchMode::BfsCheckpoint;
            probe.bfsCheckpointStartDepth = depth;
            probe.bfsCheckpointRemaining = 5;
            --probe.bfsCheckpointRemaining;
            BFSScanMetrics maxBfsMetrics;
            const auto maxBfsStart = std::chrono::steady_clock::now();
            vector<Block> components =
                getComponentsByBFS(block.cols, &maxBfsMetrics);
            addElapsedNanoseconds(
                hybridStats_.bfsNanoseconds, maxBfsStart);
            hybridStats_.bfsQueries.fetch_add(
                1, std::memory_order_relaxed);
            updateMaxBlockCount(components.size());
            if (components.size() > 1) {
                vector<HybridSearchState> states(
                    components.size(),
                    HybridSearchState{SearchMode::NoDecomposition});
                auto result = searchIndependentBlocks(
                    components, states, depth, true);
                uncoverInBlock(
                    choose->col, block, commonMaintainRows);
                setCachedResult(stateKey, result.first, result.second);
                return result;
            }
        } else {
            mode = SearchMode::BfsCheckpoint;
        }
    } else if (ettActive) {
        const uint64_t decrementNanoseconds =
            applyDecrement(commonDeletedRows);
        (void)decrementNanoseconds;
        hybridStats_.ettComponentChecks.fetch_add(
            1, std::memory_order_relaxed);
        if (currentEttComponentCount() > 1) {
            const auto extractionStart = std::chrono::steady_clock::now();
            vector<Block> components = getComponentsByETT(block.cols);
            addElapsedNanoseconds(
                hybridStats_.ettExtractionNanoseconds, extractionStart);
            updateMaxBlockCount(components.size());
            releaseLocalEtt();
            vector<HybridSearchState> states(
                components.size(),
                HybridSearchState{SearchMode::NoDecomposition});
            auto result = searchIndependentBlocks(
                components, states, depth, true);
            uncoverInBlock(
                choose->col, block, commonMaintainRows);
            setCachedResult(stateKey, result.first, result.second);
            return result;
        }

    }

    DNNFResult totalResult(0);
    shared_ptr<DNNFNode> x = F;
    Node* curC = choose->down;
    while (curC != choose) {
        set<int> branchDeletedRows;
        vector<int> branchCoveredCols;
        const bool branchMaintainRows = ettActive;
        uint64_t branchRemovedRows = 0;
        try {
            for (Node* curR = curC->right; curR != curC;
                 curR = curR->right) {
                const int col = curR->col;
                if (state.activeRows > 0) {
                    branchRemovedRows +=
                        static_cast<uint64_t>(getColSize(col));
                }
                coverInBlock(
                    col, block, branchDeletedRows,
                    branchMaintainRows);
                branchCoveredCols.push_back(col);
            }

            SearchResult branchResult;
            if (ettActive) {
                const uint64_t decrementNanoseconds =
                    applyDecrement(branchDeletedRows);
                (void)decrementNanoseconds;
                hybridStats_.ettComponentChecks.fetch_add(
                    1, std::memory_order_relaxed);
                if (currentEttComponentCount() > 1) {
                    const auto extractionStart =
                        std::chrono::steady_clock::now();
                    vector<Block> components =
                        getComponentsByETT(block.cols);
                    addElapsedNanoseconds(
                        hybridStats_.ettExtractionNanoseconds,
                        extractionStart);
                    updateMaxBlockCount(components.size());
                    releaseLocalEtt();
                    ettActive = false;
                    mode = SearchMode::NoDecomposition;
                    vector<HybridSearchState> states(
                        components.size(),
                        HybridSearchState{SearchMode::NoDecomposition});
                    branchResult = searchIndependentBlocks(
                        components, states, depth, true);
                } else {
                    HybridSearchState childState = state;
                    childState.mode = SearchMode::EttPath;
                    branchResult = hybridSearch(
                        block, depth + 1, childState);
                }
            } else {
                HybridSearchState childState = state;
                childState.mode = mode;
                if (state.activeRows > 0) {
                    childState.activeRows =
                        rowsAfterCover > branchRemovedRows
                            ? rowsAfterCover - branchRemovedRows
                            : 0;
                }
                branchResult = hybridSearch(
                    block, depth + 1, childState);
            }
            if (ettActive) {
                // The ETT probe follows one branch only.  Do not pay
                // incremental rollback cost for sibling branches.
                releaseLocalEtt();
                ettActive = false;
                mode = SearchMode::NoDecomposition;
            }
            auto& [result, subNode] = branchResult;
            if (!result.isZero()) {
                x = buildDecisionNode(curC->row, x, subNode);
                totalResult = totalResult + result;
            }
        } catch (...) {
            releaseLocalEtt();
            for (auto it = branchCoveredCols.rbegin();
                 it != branchCoveredCols.rend(); ++it) {
                uncoverInBlock(
                    *it, block, branchMaintainRows);
            }
            uncoverInBlock(
                choose->col, block, commonMaintainRows);
            throw;
        }

        for (auto it = branchCoveredCols.rbegin();
             it != branchCoveredCols.rend(); ++it) {
            uncoverInBlock(
                *it, block, branchMaintainRows);
        }
        curC = curC->down;
    }

    uncoverInBlock(
        choose->col, block, commonMaintainRows);
    if (ettActive) {
        releaseLocalEtt();
    }

    setCachedResult(stateKey, totalResult, x);
    return {totalResult, x};
}

HybridDXD::SearchResult HybridDXD::searchIndependentBlocks(
    vector<Block>& blocks,
    const vector<HybridSearchState>& states, int depth,
    bool nestedEttParallel) {
    const int count = static_cast<int>(blocks.size());
    if (states.size() != blocks.size()) {
        throw std::logic_error("HybridDXD state count mismatch");
    }
    if (taskScheduler_ && count > 1) {
        return taskScheduler_->runGroup(
            blocks, states, depth, nestedEttParallel);
    }

    if (count <= 1) {
        vector<DNNFResult> results(count);
        vector<shared_ptr<DNNFNode>> nodes(count);
        for (int i = 0; i < count; ++i) {
            auto [result, node] =
                searchOneIndependentBlock(blocks[i], states[i], depth + 1);
            releaseLocalEtt();
            results[i] = result;
            nodes[i] = node;
            if (result.isZero()) break;
        }

        DNNFResult totalResult(1);
        vector<shared_ptr<DNNFNode>> subNodes;
        subNodes.reserve(count);
        for (int i = 0; i < count; ++i) {
            if (results[i].isZero()) return {DNNFResult(0), F};
            totalResult = totalResult * results[i];
            if (nodes[i]) subNodes.push_back(nodes[i]);
        }
        return {totalResult, buildDecomposableNode(subNodes)};
    }

    throw std::runtime_error("Hybrid task scheduler unavailable");
}

HybridDXD::SearchResult HybridDXD::searchOneIndependentBlock(
    Block& block, const HybridSearchState& state, int depth) {
    initThreadLocalState(block, nullptr);

    DanceDNNF::SearchState* previousState = activeSearchState;
    DanceDNNF::SearchState childState;
    childState.collectStats = debug;
    childState.mutation.enabled = debug;
    activeSearchState = &childState;

    try {
        SearchResult result = hybridSearch(block, depth, state);
        releaseLocalEtt();
        if (debug) {
            DanceDNNF::SearchState& aggregate =
                previousState ? *previousState : mainSearchState;
            aggregate.mergeStatsFrom(childState);
        }
        activeSearchState = previousState;
        return result;
    } catch (...) {
        releaseLocalEtt();
        activeSearchState = previousState;
        throw;
    }
}

size_t HybridDXD::selectSmallestBlock(
    const vector<Block>& blocks) const {
    size_t best = 0;
    for (size_t i = 1; i < blocks.size(); ++i) {
        const uint64_t area =
            static_cast<uint64_t>(blocks[i].rows.size()) *
            blocks[i].cols.size();
        const uint64_t bestArea =
            static_cast<uint64_t>(blocks[best].rows.size()) *
            blocks[best].cols.size();
        const bool smallerArea = area < bestArea;
        const bool tieBreak =
            area == bestArea &&
            (blocks[i].rows.size() < blocks[best].rows.size() ||
             (blocks[i].rows.size() == blocks[best].rows.size() &&
              blocks[i].cols.size() < blocks[best].cols.size()));
        if (smallerArea || tieBreak) {
            best = i;
        }
    }
    return best;
}

size_t HybridDXD::selectLargestBlock(
    const vector<Block>& blocks) const {
    size_t best = 0;
    for (size_t i = 1; i < blocks.size(); ++i) {
        const uint64_t area =
            static_cast<uint64_t>(blocks[i].rows.size()) *
            blocks[i].cols.size();
        const uint64_t bestArea =
            static_cast<uint64_t>(blocks[best].rows.size()) *
            blocks[best].cols.size();
        if (area > bestArea) best = i;
    }
    return best;
}

set<int> HybridDXD::materializeActiveRows(const set<int>& cols) const {
    set<int> rows;
    for (int col : cols) {
        ColumnHeader* header = getColumnHeader(col);
        for (Node* node = header->down; node != header;
             node = node->down) {
            rows.insert(node->row);
        }
    }
    return rows;
}

void HybridDXD::buildProbeEtt(const Block& block) {
    const auto buildStart = std::chrono::steady_clock::now();
    prepareThreadLocalDynamicCCForBlock(block, false);
    addElapsedNanoseconds(
        hybridStats_.ettBuildNanoseconds, buildStart);
    hybridStats_.ettBuilds.fetch_add(1, std::memory_order_relaxed);
}

uint64_t HybridDXD::applyDecrement(const set<int>& deletedRows) {
    if (deletedRows.empty()) return 0;
    const auto updateStart = std::chrono::steady_clock::now();
    DecUpdateCC(deletedRows);
    const uint64_t elapsedNanoseconds =
        static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - updateStart)
                .count());
    hybridStats_.ettDecrementNanoseconds.fetch_add(
        elapsedNanoseconds, std::memory_order_relaxed);
    hybridStats_.ettDecrementUpdates.fetch_add(
        1, std::memory_order_relaxed);
    return elapsedNanoseconds;
}

size_t HybridDXD::currentEttComponentCount() const {
    if (!tlsState) return 0;
    return static_cast<size_t>(std::count_if(
        tlsState->components.begin(), tlsState->components.end(),
        [](const auto& tree) {
            return tree && !tree->isEmpty();
        }));
}

void HybridDXD::releaseLocalEtt() {
    if (!hasLocalEttStorage()) return;
    const auto releaseStart = std::chrono::steady_clock::now();
    releaseThreadLocalDynamicCCStorage();
    addElapsedNanoseconds(
        hybridStats_.ettReleaseNanoseconds, releaseStart);
    hybridStats_.ettReleases.fetch_add(1, std::memory_order_relaxed);
}

bool HybridDXD::hasLocalEttStorage() const {
    return tlsState && tlsState->initialized &&
           (tlsState->graphContext || !tlsState->components.empty());
}

void HybridDXD::addElapsedNanoseconds(
    std::atomic<uint64_t>& target,
    const std::chrono::steady_clock::time_point& start) {
    target.fetch_add(
        static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - start)
                .count()),
        std::memory_order_relaxed);
}

void HybridDXD::logSummary() const {
    logger.logLine("Hybrid Strategy: min-block-ETT/max-block-BFS");
    logger.logLine(
        "Hybrid ETT Area Divisor: " +
        std::to_string(config_.ettAreaDivisor));
    logger.logLine(
        "Hybrid BFS Queries: " +
        std::to_string(
            hybridStats_.bfsQueries.load(std::memory_order_relaxed)));
    logger.logLine(
        "Hybrid BFS Time: " +
        std::to_string(
            static_cast<double>(hybridStats_.bfsNanoseconds.load(
                std::memory_order_relaxed)) /
            1e9) +
        " s");
    logger.logLine(
        "Hybrid Initial Probe Selections: " +
        std::to_string(hybridStats_.initialProbeSelections.load(
            std::memory_order_relaxed)));
    logger.logLine(
        "Hybrid ETT Entry Decisions: " +
        std::to_string(hybridStats_.ettEntryDecisions.load(
            std::memory_order_relaxed)));
    logger.logLine(
        "Hybrid Outer Parallel Launches: " +
        std::to_string(hybridStats_.outerParallelLaunches.load(
            std::memory_order_relaxed)));
    logger.logLine(
        "Hybrid Inner Parallel Launches: " +
        std::to_string(hybridStats_.innerParallelLaunches.load(
            std::memory_order_relaxed)));
    logger.logLine(
        "Hybrid ETT Builds: " +
        std::to_string(hybridStats_.ettBuilds.load(
            std::memory_order_relaxed)));
    logger.logLine(
        "Hybrid ETT Build Time: " +
        std::to_string(
            static_cast<double>(hybridStats_.ettBuildNanoseconds.load(
                std::memory_order_relaxed)) /
            1e9) +
        " s");
    logger.logLine(
        "Hybrid ETT Decrement Updates: " +
        std::to_string(hybridStats_.ettDecrementUpdates.load(
            std::memory_order_relaxed)));
    logger.logLine(
        "Hybrid ETT Decrement Time: " +
        std::to_string(
            static_cast<double>(hybridStats_.ettDecrementNanoseconds.load(
                std::memory_order_relaxed)) /
            1e9) +
        " s");
    logger.logLine(
        "Hybrid ETT Component Checks: " +
        std::to_string(hybridStats_.ettComponentChecks.load(
            std::memory_order_relaxed)));
    logger.logLine(
        "Hybrid ETT Extraction Time: " +
        std::to_string(
            static_cast<double>(hybridStats_.ettExtractionNanoseconds.load(
                std::memory_order_relaxed)) /
            1e9) +
        " s");
    logger.logLine(
        "Hybrid ETT Releases: " +
        std::to_string(hybridStats_.ettReleases.load(
            std::memory_order_relaxed)));
    logger.logLine(
        "Hybrid ETT Release Time: " +
        std::to_string(
            static_cast<double>(hybridStats_.ettReleaseNanoseconds.load(
                std::memory_order_relaxed)) /
            1e9) +
        " s");
}
