#include "../include/HybridDXD.h"
#include "../include/ParallelTaskQueue.h"

#include <algorithm>
#include <ctime>
#include <stdexcept>
#include <thread>

class HybridDXD::TaskScheduler {
public:
    TaskScheduler(HybridDXD& owner, int threadCount)
        : owner_(owner),
          queue_(
              threadCount,
              [this](Task& task) { execute(task); }) {}
    ~TaskScheduler() = default;

    HybridDXD::SearchResult runGroup(
        vector<Block>& blocks,
        const vector<HybridSearchState>& states,
        int depth, bool nested) {
        const bool collectStats = owner_.statisticsEnabled();
        if (collectStats) {
            if (nested) {
                owner_.hybridStats_.innerParallelLaunches.fetch_add(
                    1, std::memory_order_relaxed);
            } else {
                owner_.hybridStats_.outerParallelLaunches.fetch_add(
                    1, std::memory_order_relaxed);
            }
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

        vector<uint64_t> blockRows;
        vector<uint64_t> blockCols;
        vector<HybridDXD::SearchMode> blockModes;
        vector<bool> blockSeeded;
        if (collectStats) {
            blockRows.resize(static_cast<size_t>(count));
            blockCols.resize(static_cast<size_t>(count));
            blockModes.resize(static_cast<size_t>(count));
            blockSeeded.resize(static_cast<size_t>(count), false);
            for (int i = 0; i < count; ++i) {
                blockRows[i] = blocks[i].rows.size();
                blockCols[i] = blocks[i].cols.size();
                blockModes[i] = states[i].mode;
                blockSeeded[i] = states[i].seed != nullptr;
            }
        }

        auto group = std::make_shared<Group>(count, collectStats);
        vector<int> taskOrder;
        taskOrder.reserve(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i) {
            if (states[i].seed) taskOrder.push_back(i);
        }
        for (int i = 0; i < count; ++i) {
            if (!states[i].seed) taskOrder.push_back(i);
        }
        for (int i : taskOrder) {
            HybridSearchState taskState = states[i];
            if (taskState.activeRows == 0) {
                taskState.activeRows = blocks[i].rows.size();
            }
            queue_.enqueue(
                Task{std::move(blocks[i]), std::move(taskState),
                     depth + 1, i, group});
        }

        while (group->pending.load(std::memory_order_acquire) > 0) {
            Task task;
            if (queue_.tryPop(task)) {
                execute(task);
            } else {
                std::this_thread::yield();
            }
        }

        if (collectStats) {
            std::lock_guard<std::mutex> logLock(logMutex_);
            const bool timedOut =
                group->timeout.load(std::memory_order_acquire);
            const bool failed =
                group->failure.load(std::memory_order_acquire);
            double total = 0.0;
            double maximum = 0.0;
            size_t executed = 0;
            for (int i = 0; i < count; ++i) {
                const double seconds = group->seconds[i];
                const bool ran = seconds >= 0.0;
                owner_.logger.logLine(
                    "[hybrid-task] group_depth=" +
                    std::to_string(depth) +
                    " index=" + std::to_string(i) +
                    " rows=" + std::to_string(
                        blockRows[i]) +
                    " cols=" + std::to_string(
                        blockCols[i]) +
                    " mode=" + searchModeName(blockModes[i]) +
                    " seed=" + (blockSeeded[i] ? "1" : "0") +
                    " ran=" + (ran ? "1" : "0") +
                    " seconds=" + std::to_string(ran ? seconds : 0.0));
                if (!ran) continue;
                ++executed;
                total += seconds;
                maximum = std::max(maximum, seconds);
                const uint64_t nanoseconds =
                    static_cast<uint64_t>(seconds * 1e9);
                owner_.recordBlockTaskTiming(
                    blockSeeded[i], nanoseconds);
            }
            owner_.logger.logLine(
                "[hybrid-task-group] depth=" + std::to_string(depth) +
                " tasks=" + std::to_string(count) +
                " executed=" + std::to_string(executed) +
                " cpu=" + std::to_string(total) +
                " max=" + std::to_string(maximum) +
                " timed_out=" + (timedOut ? "1" : "0") +
                " failed=" + (failed ? "1" : "0"));
        }

        if (group->timeout.load(std::memory_order_acquire)) {
            throw std::runtime_error("Time bound broken");
        }
        if (group->failure.load(std::memory_order_acquire)) {
            return {DNNFResult(0), owner_.F};
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
        Group(int n, bool collectStats)
            : pending(n), results(static_cast<size_t>(n)),
              nodes(static_cast<size_t>(n), nullptr),
              collectStats(collectStats),
              seconds(
                  collectStats ? static_cast<size_t>(n) : size_t{0},
                  -1.0) {}

        std::atomic<int> pending{0};
        std::atomic<bool> failure{false};
        std::atomic<bool> timeout{false};
        vector<DNNFResult> results;
        vector<shared_ptr<DNNFNode>> nodes;
        bool collectStats = false;
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

    static const char* searchModeName(HybridDXD::SearchMode mode) {
        switch (mode) {
            case HybridDXD::SearchMode::BfsProbe:
                return "BfsProbe";
            case HybridDXD::SearchMode::BfsNext:
                return "BfsNext";
            case HybridDXD::SearchMode::BfsThreshold:
                return "BfsThreshold";
            case HybridDXD::SearchMode::DynPath:
                return "DynPath";
            case HybridDXD::SearchMode::DynQuery:
                return "DynQuery";
            case HybridDXD::SearchMode::EttPath:
                return "EttPath";
            case HybridDXD::SearchMode::AdaptivePath:
                return "AdaptivePath";
            case HybridDXD::SearchMode::NoDecomposition:
                return "NoDecomposition";
        }
        return "Unknown";
    }

    void execute(Task& task) {
        const auto taskStart = task.group->collectStats
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        if (task.group->failure.load(std::memory_order_acquire) ||
            task.group->timeout.load(std::memory_order_acquire)) {
            task.group->pending.fetch_sub(
                1, std::memory_order_acq_rel);
            return;
        }

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
        if (task.group->collectStats) {
            task.group->seconds[task.index] =
                std::chrono::duration_cast<std::chrono::duration<double>>(
                    std::chrono::steady_clock::now() - taskStart)
                    .count();
        }
        task.group->pending.fetch_sub(1, std::memory_order_acq_rel);
    }

    HybridDXD& owner_;
    ParallelTaskQueue<Task> queue_;
    std::mutex logMutex_;
};

HybridDXD::HybridDXD(const string& file_path, Logger& logger,
                     int pool_size, bool debug)
    : DanceDNNF(file_path, logger, AlgorithmMode::HYBRID_DXD,
                pool_size, debug) {
}

HybridDXD::~HybridDXD() = default;

namespace {
uint64_t incidenceEdgeCode(int row, int column) {
    return (static_cast<uint64_t>(
                static_cast<uint32_t>(row))
            << 32) |
           static_cast<uint32_t>(column);
}

uint64_t subtractClamped(uint64_t value, uint64_t amount) {
    return value >= amount ? value - amount : 0;
}

set<int> mergedSet(const set<int>& first, const set<int>& second) {
    set<int> result = first;
    result.insert(second.begin(), second.end());
    return result;
}
}  // namespace

std::shared_ptr<HybridDXD::DynamicSeed>
HybridDXD::makeDynamicSeed(
    const Block& block, const IncidenceBfsResult& bfs) const {
    auto seed = std::make_shared<DynamicSeed>();
    seed->activeRows.insert(block.rows.begin(), block.rows.end());
    seed->activeCols.insert(block.cols.begin(), block.cols.end());
    seed->entries = block.activeEntriesValid
        ? block.activeEntries
        : bfs.metrics.graphEdges;
    seed->rowOffset = static_cast<uint64_t>(ROWS);
    seed->selectedBlockInitialArea =
        static_cast<uint64_t>(block.rows.size()) *
        static_cast<uint64_t>(block.cols.size());
    seed->thresholdRatio = config_.thresholdRatio;
    for (const IncidenceEdge& edge : bfs.treeEdges) {
        if (!block.rows.count(edge.row) ||
            !block.cols.count(edge.column)) {
            continue;
        }
        seed->treeEdges.push_back(edge);
        seed->treeEdgeSet.insert(
            incidenceEdgeCode(edge.row, edge.column));
    }
    for (const IncidenceEdge& edge : bfs.nonTreeEdges) {
        if (!block.rows.count(edge.row) ||
            !block.cols.count(edge.column)) {
            continue;
        }
        seed->nonTreeEdges.push_back(edge);
        seed->nonTreeEdgeSet.insert(
            incidenceEdgeCode(edge.row, edge.column));
    }
    seed->nonTreeEntries = seed->nonTreeEdges.size();
    return seed;
}

std::vector<HybridDXD::HybridSearchState>
HybridDXD::makePerBlockProbeStates(
    const vector<Block>& components,
    const IncidenceBfsResult* bfs) const {
    vector<HybridSearchState> states;
    states.reserve(components.size());
    for (const Block& component : components) {
        HybridSearchState state;
        state.mode = SearchMode::BfsProbe;
        state.probeContext = std::make_shared<ProbeContext>();
        state.probeContext->initialArea =
            static_cast<uint64_t>(component.rows.size()) *
            static_cast<uint64_t>(component.cols.size());
        state.probeContext->stopRatio = config_.thresholdRatio;
        if (bfs) {
            state.seed = makeDynamicSeed(component, *bfs);
            state.skipInitialDecomposition = true;
        }
        states.push_back(std::move(state));
    }
    return states;
}

std::vector<HybridDXD::HybridSearchState>
HybridDXD::makeNormalSplitStates(
    const vector<Block>& components) const {
    vector<HybridSearchState> states(
        components.size(),
        HybridSearchState{SearchMode::NoDecomposition});
    if (components.empty()) return states;

    const uint64_t stopThreshold =
        decompositionStopThresholdValue();
    const size_t minimum = selectSeedBlock(components);
    size_t maximum = selectMaximumBlock(components);
    if (maximum == minimum && components.size() > 1) {
        maximum = minimum == 0 ? 1 : 0;
    }
    const auto area = [](const Block& block) {
        return static_cast<uint64_t>(block.rows.size()) *
               static_cast<uint64_t>(block.cols.size());
    };
    if (area(components[minimum]) > stopThreshold) {
        states[minimum].mode = SearchMode::BfsProbe;
        states[minimum].probeContext =
            std::make_shared<ProbeContext>();
        states[minimum].probeContext->initialArea =
            area(components[minimum]);
        states[minimum].probeContext->stopRatio =
            config_.thresholdRatio;
    }
    if (area(components[maximum]) > stopThreshold) {
        states[maximum].mode = SearchMode::BfsThreshold;
        states[maximum].probeContext =
            std::make_shared<ProbeContext>();
        states[maximum].probeContext->initialArea =
            area(components[maximum]);
        states[maximum].probeContext->stopRatio =
            config_.thresholdRatio;
    }
    return states;
}

void HybridDXD::updateDecompositionStopThresholdForSplit(
    const vector<Block>& components) {
    if (components.empty()) return;
    const Block& minimum =
        components[selectSeedBlock(components)];
    initializeDecompositionStopThresholdFromSplit(
        static_cast<uint64_t>(minimum.rows.size()) *
            static_cast<uint64_t>(minimum.cols.size()),
        config_.thresholdRatio);
}

void HybridDXD::buildSeedEtt(
    const std::shared_ptr<DynamicSeed>& seed) {
    if (!seed || seed->ettBuilt || !tlsState) return;
    const bool collectStats = statisticsEnabled();
    const auto buildStart = collectStats
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    const std::clock_t cpuStart = collectStats
        ? std::clock()
        : std::clock_t{0};
    releaseThreadLocalDynamicCCStorage();

    auto context = std::make_shared<TreeGraphContext>();
    // Matrix column ids are not guaranteed to be zero-based in every input
    // format, so reserve one extra slot for the bipartite column namespace.
    context->graph = std::make_unique<Graph>(ROWS + COLS + 2);
    context->graph->setNeighborProvider(
        [this, seed](int vertex) {
            std::vector<int> neighbors;
            if (vertex < ROWS) {
                if (!seed->activeRows.count(vertex)) return neighbors;
                auto it = row_to_cols.find(vertex);
                if (it == row_to_cols.end()) return neighbors;
                for (int column : it->second) {
                    if (seed->activeCols.count(column)) {
                        neighbors.push_back(ROWS + column);
                    }
                }
                return neighbors;
            }
            const int column = vertex - ROWS;
            if (!seed->activeCols.count(column)) return neighbors;
            auto it = col_to_rows.find(column);
            if (it == col_to_rows.end()) return neighbors;
            for (int row : it->second) {
                if (seed->activeRows.count(row)) {
                    neighbors.push_back(row);
                }
            }
            return neighbors;
        });
    tlsState->graphContext = context;

    std::unordered_map<int, int> parent;
    std::unordered_map<int, unsigned char> rank;
    auto ensure = [&](int vertex) { parent.emplace(vertex, vertex); };
    std::function<int(int)> find = [&](int vertex) -> int {
        ensure(vertex);
        int root = vertex;
        while (parent[root] != root) root = parent[root];
        while (parent[vertex] != vertex) {
            const int next = parent[vertex];
            parent[vertex] = root;
            vertex = next;
        }
        return root;
    };
    auto unite = [&](int lhs, int rhs) {
        lhs = find(lhs);
        rhs = find(rhs);
        if (lhs == rhs) return false;
        if (rank[lhs] < rank[rhs]) std::swap(lhs, rhs);
        parent[rhs] = lhs;
        if (rank[lhs] == rank[rhs]) ++rank[lhs];
        return true;
    };

    for (int row : seed->activeRows) ensure(row);
    for (int column : seed->activeCols) {
        ensure(ROWS + column);
    }
    for (const IncidenceEdge& edge : seed->treeEdges) {
        unite(edge.row, ROWS + edge.column);
    }

    std::unordered_map<int, std::vector<
        std::pair<int, IncidenceEdge>>> adjacency;
    for (const IncidenceEdge& edge : seed->treeEdges) {
        const int row = edge.row;
        const int column = ROWS + edge.column;
        adjacency[row].push_back({column, edge});
        adjacency[column].push_back({row, edge});
    }

    std::unordered_set<int> visited;
    int nextTreeId = 0;
    for (int start : seed->activeRows) {
        if (!visited.insert(start).second) continue;
        auto tree = std::make_unique<splaytree::EulerTourTree>(
            nextTreeId++);
        tree->addVertex(start);
        std::queue<int> pending;
        pending.push(start);
        while (!pending.empty()) {
            const int vertex = pending.front();
            pending.pop();
            for (const auto& [neighbor, edge] : adjacency[vertex]) {
                (void)edge;
                if (!visited.insert(neighbor).second) continue;
                auto other =
                    std::make_unique<splaytree::EulerTourTree>(
                        nextTreeId++);
                other->addVertex(neighbor);
                tree->link(vertex, neighbor, other.get());
                pending.push(neighbor);
            }
        }
        tlsState->components.push_back(std::move(tree));
    }
    for (int column : seed->activeCols) {
        const int vertex = ROWS + column;
        if (visited.insert(vertex).second) {
            auto tree = std::make_unique<splaytree::EulerTourTree>(
                nextTreeId++);
            tree->addVertex(vertex);
            tlsState->components.push_back(std::move(tree));
        }
    }

    for (auto& tree : tlsState->components) {
        if (!tree) continue;
        tree->setReplacementNeighborProvider(
            [this, seed](int vertex,
                         std::unordered_set<int>& neighbors) {
                if (vertex < ROWS) {
                    auto it = row_to_cols.find(vertex);
                    if (it == row_to_cols.end()) return;
                    for (int column : it->second) {
                        if (seed->activeCols.count(column) &&
                            seed->nonTreeEdgeSet.count(
                                incidenceEdgeCode(vertex, column))) {
                            neighbors.insert(ROWS + column);
                        }
                    }
                } else {
                    const int column = vertex - ROWS;
                    auto it = col_to_rows.find(column);
                    if (it == col_to_rows.end()) return;
                    for (int row : it->second) {
                        if (seed->activeRows.count(row) &&
                            seed->nonTreeEdgeSet.count(
                                incidenceEdgeCode(row, column))) {
                            neighbors.insert(row);
                        }
                    }
                }
            });
    }
    for (const auto& tree : tlsState->components) {
        if (!tree || tree->isEmpty()) continue;
        context->graph->registerComponent(
            tree->getTreeId(), tree->getVertices());
    }
    if (!tlsState->components.empty()) {
        const int anyVertex =
            tlsState->components.front()->getAnyVertex();
        if (anyVertex >= 0) {
            tlsState->subgraph =
                context->graph->subgraphOf(anyVertex);
            tlsState->activeSubgraph = tlsState->subgraph;
        }
    }
    seed->ettBuilt = true;
    if (collectStats) {
        hybridStats_.ettBuildCalls.fetch_add(
            1, std::memory_order_relaxed);
        const uint64_t elapsed = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - buildStart).count());
        hybridStats_.ettBuildNanosecondsNew.fetch_add(
            elapsed, std::memory_order_relaxed);
        hybridStats_.dynCpuNanoseconds.fetch_add(
            cpuNanosecondsSince(cpuStart),
            std::memory_order_relaxed);
    }
}

void HybridDXD::rebuildSeedEttForest(
    const std::shared_ptr<DynamicSeed>& seed) {
    if (!seed || !tlsState) return;

    std::unordered_set<splaytree::Node*> ownedRoots;
    for (auto& tree : tlsState->components) {
        if (!tree || !tree->root) continue;
        if (!ownedRoots.insert(tree->root).second) {
            tree->root = nullptr;
        }
    }
    tlsState->components.clear();

    auto context = std::make_shared<TreeGraphContext>();
    context->graph = std::make_unique<Graph>(ROWS + COLS + 2);
    context->graph->setNeighborProvider(
        [this, seed](int vertex) {
            std::vector<int> neighbors;
            if (vertex < ROWS) {
                if (!seed->activeRows.count(vertex)) return neighbors;
                auto it = row_to_cols.find(vertex);
                if (it == row_to_cols.end()) return neighbors;
                for (int column : it->second) {
                    if (seed->activeCols.count(column)) {
                        neighbors.push_back(ROWS + column);
                    }
                }
                return neighbors;
            }
            const int column = vertex - ROWS;
            if (!seed->activeCols.count(column)) return neighbors;
            auto it = col_to_rows.find(column);
            if (it == col_to_rows.end()) return neighbors;
            for (int row : it->second) {
                if (seed->activeRows.count(row)) {
                    neighbors.push_back(row);
                }
            }
            return neighbors;
        });
    tlsState->graphContext = context;
    tlsState->nextTreeId = 0;
    tlsState->subgraph = nullptr;
    tlsState->activeSubgraph = nullptr;

    seed->treeEdges.clear();
    seed->treeEdgeSet.clear();
    std::unordered_set<int> visited;
    auto graphNeighbors = [&](int vertex) {
        return context->graph->getNeighbors(vertex);
    };
    auto visitComponent = [&](int start) {
        if (visited.count(start)) return;
        auto tree = std::make_unique<splaytree::EulerTourTree>(
            tlsState->nextTreeId++);
        tree->addVertex(start);
        visited.insert(start);
        std::queue<int> pending;
        pending.push(start);
        while (!pending.empty()) {
            const int vertex = pending.front();
            pending.pop();
            for (int neighbor : graphNeighbors(vertex)) {
                if (!visited.insert(neighbor).second) continue;
                auto other = std::make_unique<splaytree::EulerTourTree>(
                    tlsState->nextTreeId++);
                other->addVertex(neighbor);
                tree->link(vertex, neighbor, other.get());
                if (vertex < ROWS) {
                    seed->treeEdges.push_back({vertex, neighbor - ROWS});
                } else {
                    seed->treeEdges.push_back({neighbor, vertex - ROWS});
                }
                seed->treeEdgeSet.insert(
                    incidenceEdgeCode(
                        seed->treeEdges.back().row,
                        seed->treeEdges.back().column));
                pending.push(neighbor);
            }
        }
        tlsState->components.push_back(std::move(tree));
    };

    for (int row : seed->activeRows) visitComponent(row);
    for (int column : seed->activeCols) visitComponent(ROWS + column);

    for (auto& tree : tlsState->components) {
        if (!tree) continue;
        tree->setReplacementNeighborProvider(
            [this, seed](int vertex,
                         std::unordered_set<int>& neighbors) {
                if (vertex < ROWS) {
                    auto it = row_to_cols.find(vertex);
                    if (it == row_to_cols.end()) return;
                    for (int column : it->second) {
                        if (seed->activeCols.count(column) &&
                            seed->nonTreeEdgeSet.count(
                                incidenceEdgeCode(vertex, column))) {
                            neighbors.insert(ROWS + column);
                        }
                    }
                } else {
                    const int column = vertex - ROWS;
                    auto it = col_to_rows.find(column);
                    if (it == col_to_rows.end()) return;
                    for (int row : it->second) {
                        if (seed->activeRows.count(row) &&
                            seed->nonTreeEdgeSet.count(
                                incidenceEdgeCode(row, column))) {
                            neighbors.insert(row);
                        }
                    }
                }
            });
        context->graph->registerComponent(
            tree->getTreeId(), tree->getVertices());
    }
    if (!tlsState->components.empty()) {
        const int anyVertex =
            tlsState->components.front()->getAnyVertex();
        if (anyVertex >= 0) {
            tlsState->subgraph =
                context->graph->subgraphOf(anyVertex);
            tlsState->activeSubgraph = tlsState->subgraph;
        }
    }
    seed->nonTreeEdgeSet.clear();
    for (int row : seed->activeRows) {
        auto it = row_to_cols.find(row);
        if (it == row_to_cols.end()) continue;
        for (int column : it->second) {
            if (!seed->activeCols.count(column)) continue;
            const uint64_t code = incidenceEdgeCode(row, column);
            if (!seed->treeEdgeSet.count(code)) {
                seed->nonTreeEdgeSet.insert(code);
            }
        }
    }
    seed->nonTreeEntries = seed->nonTreeEdgeSet.size();
    seed->ettBuilt = true;
}

HybridDXD::EttForestSnapshot HybridDXD::cloneLocalEttForest() const {
    EttForestSnapshot snapshot;
    if (!tlsState) return snapshot;
    snapshot.reserve(tlsState->components.size());
    for (const auto& tree : tlsState->components) {
        if (tree) snapshot.push_back(tree->clone());
    }
    return snapshot;
}

void HybridDXD::restoreLocalEttForest(
    const std::shared_ptr<DynamicSeed>& seed,
    const EttForestSnapshot& snapshot) {
    if (!seed || !tlsState) return;

    releaseThreadLocalDynamicCCStorage();
    auto context = std::make_shared<TreeGraphContext>();
    context->graph = std::make_unique<Graph>(ROWS + COLS + 2);
    context->graph->setNeighborProvider(
        [this, seed](int vertex) {
            std::vector<int> neighbors;
            if (vertex < ROWS) {
                if (!seed->activeRows.count(vertex)) return neighbors;
                auto it = row_to_cols.find(vertex);
                if (it == row_to_cols.end()) return neighbors;
                for (int column : it->second) {
                    if (seed->activeCols.count(column)) {
                        neighbors.push_back(ROWS + column);
                    }
                }
                return neighbors;
            }
            const int column = vertex - ROWS;
            if (!seed->activeCols.count(column)) return neighbors;
            auto it = col_to_rows.find(column);
            if (it == col_to_rows.end()) return neighbors;
            for (int row : it->second) {
                if (seed->activeRows.count(row)) {
                    neighbors.push_back(row);
                }
            }
            return neighbors;
        });
    tlsState->graphContext = context;
    tlsState->nextTreeId = 0;

    for (const auto& source : snapshot) {
        if (!source) continue;
        auto tree = source->clone();
        tree->setReplacementNeighborProvider(
            [this, seed](int vertex,
                         std::unordered_set<int>& neighbors) {
                if (vertex < ROWS) {
                    auto it = row_to_cols.find(vertex);
                    if (it == row_to_cols.end()) return;
                    for (int column : it->second) {
                        if (seed->activeCols.count(column) &&
                            seed->nonTreeEdgeSet.count(
                                incidenceEdgeCode(vertex, column))) {
                            neighbors.insert(ROWS + column);
                        }
                    }
                } else {
                    const int column = vertex - ROWS;
                    auto it = col_to_rows.find(column);
                    if (it == col_to_rows.end()) return;
                    for (int row : it->second) {
                        if (seed->activeRows.count(row) &&
                            seed->nonTreeEdgeSet.count(
                                incidenceEdgeCode(row, column))) {
                            neighbors.insert(row);
                        }
                    }
                }
            });
        const auto vertices = tree->getVertices();
        context->graph->registerComponent(tree->getTreeId(), vertices);
        tlsState->nextTreeId =
            std::max(tlsState->nextTreeId, tree->getTreeId() + 1);
        tlsState->components.push_back(std::move(tree));
    }

    if (!tlsState->components.empty()) {
        const int anyVertex =
            tlsState->components.front()->getAnyVertex();
        if (anyVertex >= 0) {
            tlsState->subgraph =
                context->graph->subgraphOf(anyVertex);
            tlsState->activeSubgraph = tlsState->subgraph;
        }
    }
}

void HybridDXD::captureSeedTreeEdges(DynamicSeed& seed) const {
    seed.treeEdges.clear();
    seed.treeEdgeSet.clear();
    if (!tlsState) return;
    for (const auto& tree : tlsState->components) {
        if (!tree) continue;
        for (const auto& [u, edges] : tree->edgeNodes) {
            for (const auto& [v, node] : edges) {
                (void)node;
                if (u < v) continue;
                if (u < ROWS && v >= ROWS) {
                    const IncidenceEdge edge{u, v - ROWS};
                    seed.treeEdges.push_back(edge);
                    seed.treeEdgeSet.insert(
                        incidenceEdgeCode(edge.row, edge.column));
                } else if (v < ROWS && u >= ROWS) {
                    const IncidenceEdge edge{v, u - ROWS};
                    seed.treeEdges.push_back(edge);
                    seed.treeEdgeSet.insert(
                        incidenceEdgeCode(edge.row, edge.column));
                }
            }
        }
    }
}

std::vector<Block> HybridDXD::extractIncidenceComponents(
    const set<int>& targetCols) const {
    std::vector<Block> blocks;
    if (!tlsState) return blocks;
    for (const auto& tree : tlsState->components) {
        if (!tree || tree->isEmpty()) continue;
        Block block;
        for (int vertex : tree->vertices) {
            if (vertex < ROWS) {
                block.rows.insert(vertex);
            } else {
                const int column = vertex - ROWS;
                if (targetCols.count(column)) block.cols.insert(column);
            }
        }
        if (block.rows.empty() && block.cols.empty()) continue;
        block.activeEntries = 0;
        for (int row : block.rows) {
            auto it = row_to_cols.find(row);
            if (it == row_to_cols.end()) continue;
            for (int column : it->second) {
                if (block.cols.count(column)) ++block.activeEntries;
            }
        }
        block.activeEntriesValid = true;
        blocks.push_back(std::move(block));
    }
    std::sort(
        blocks.begin(), blocks.end(),
        [](const Block& lhs, const Block& rhs) {
            if (lhs.rows != rhs.rows) return lhs.rows < rhs.rows;
            return lhs.cols < rhs.cols;
        });
    return blocks;
}

bool HybridDXD::isTreeEdgeInForest(int row, int column) const {
    if (!tlsState) return false;
    for (const auto& tree : tlsState->components) {
        if (tree && tree->isTreeEdge(row, ROWS + column)) {
            return true;
        }
    }
    return false;
}

uint64_t HybridDXD::countTreeEdgesInForest() const {
    if (!tlsState) return 0;
    uint64_t directed = 0;
    for (const auto& tree : tlsState->components) {
        if (!tree) continue;
        for (const auto& [u, neighbors] : tree->edgeNodes) {
            for (const auto& [v, node] : neighbors) {
                (void)node;
                if (u < v) ++directed;
            }
        }
    }
    return directed;
}

uint64_t HybridDXD::countDeletedSeedEdges(
    const DynamicSeed& seed,
    const set<int>& deletedRows,
    const set<int>& deletedCols,
    uint64_t* deletedTreeEdges) const {
    std::unordered_set<uint64_t> deleted;
    for (int row : deletedRows) {
        auto it = row_to_cols.find(row);
        if (it == row_to_cols.end()) continue;
        for (int column : it->second) {
            if (seed.activeCols.count(column)) {
                deleted.insert(incidenceEdgeCode(row, column));
            }
        }
    }
    for (int column : deletedCols) {
        auto it = col_to_rows.find(column);
        if (it == col_to_rows.end()) continue;
        for (int row : it->second) {
            if (seed.activeRows.count(row)) {
                deleted.insert(incidenceEdgeCode(row, column));
            }
        }
    }
    uint64_t treeDeleted = 0;
    for (uint64_t code : deleted) {
        const int row = static_cast<int>(code >> 32);
        const int column = static_cast<int>(code & 0xffffffffULL);
        if (isTreeEdgeInForest(row, column) ||
            seed.treeEdgeSet.count(code)) {
            ++treeDeleted;
        }
    }
    if (deletedTreeEdges) *deletedTreeEdges = treeDeleted;
    return deleted.size();
}

HybridDXD::IncidenceBfsResult HybridDXD::runIncidenceBfs(Block& block) {
    const bool collectStats = statisticsEnabled();
    const auto start = collectStats
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    const std::clock_t cpuStart = collectStats
        ? std::clock()
        : std::clock_t{0};

    IncidenceBfsResult result = getComponentsByIncidenceBFS(block.cols);
    updateMaxBlockCount(result.components.size());

    if (!collectStats) return result;

    const uint64_t elapsed = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - start).count());
    const uint64_t cpuElapsed = static_cast<uint64_t>(
        static_cast<long double>(std::clock() - cpuStart) *
        1e9L / static_cast<long double>(CLOCKS_PER_SEC));
    hybridStats_.bfsCalls.fetch_add(1, std::memory_order_relaxed);
    hybridStats_.bfsQueries.fetch_add(1, std::memory_order_relaxed);
    hybridStats_.bfsNanosecondsNew.fetch_add(elapsed, std::memory_order_relaxed);
    hybridStats_.componentQueryCalls.fetch_add(1, std::memory_order_relaxed);
    hybridStats_.componentQueryNanoseconds.fetch_add(
        elapsed, std::memory_order_relaxed);
    hybridStats_.componentQueryCpuNanoseconds.fetch_add(
        cpuElapsed, std::memory_order_relaxed);
    hybridStats_.bfsCpuNanoseconds.fetch_add(
        cpuElapsed, std::memory_order_relaxed);
    return result;
}

std::vector<Block> HybridDXD::runPlainIncidenceBfs(Block& block) {
    const bool collectStats = statisticsEnabled();
    const auto start = collectStats
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    const std::clock_t cpuStart = collectStats
        ? std::clock()
        : std::clock_t{0};

    vector<Block> components = getComponentsByBFS(block.cols);
    updateMaxBlockCount(components.size());

    if (!collectStats) return components;

    const uint64_t elapsed = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - start).count());
    const uint64_t cpuElapsed = static_cast<uint64_t>(
        static_cast<long double>(std::clock() - cpuStart) *
        1e9L / static_cast<long double>(CLOCKS_PER_SEC));
    hybridStats_.bfsCalls.fetch_add(1, std::memory_order_relaxed);
    hybridStats_.bfsQueries.fetch_add(1, std::memory_order_relaxed);
    hybridStats_.bfsNanosecondsNew.fetch_add(elapsed, std::memory_order_relaxed);
    hybridStats_.componentQueryCalls.fetch_add(1, std::memory_order_relaxed);
    hybridStats_.componentQueryNanoseconds.fetch_add(
        elapsed, std::memory_order_relaxed);
    hybridStats_.componentQueryCpuNanoseconds.fetch_add(
        cpuElapsed, std::memory_order_relaxed);
    hybridStats_.bfsCpuNanoseconds.fetch_add(
        cpuElapsed, std::memory_order_relaxed);
    return components;
}

std::vector<Block> HybridDXD::runDynComponentQuery(Block& block) {
    const bool collectStats = statisticsEnabled();
    const auto start = collectStats ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    const std::clock_t cpuStart = collectStats ? std::clock() : std::clock_t{0};

    vector<Block> components = extractIncidenceComponents(block.cols);
    updateMaxBlockCount(components.size());

    if (!collectStats) return components;

    const uint64_t elapsed = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count());
    const uint64_t cpuElapsed = 
        static_cast<uint64_t>(static_cast<long double>(std::clock() - cpuStart) * 1e9L / static_cast<long double>(CLOCKS_PER_SEC));
    hybridStats_.componentQueryCalls.fetch_add(1, std::memory_order_relaxed);
    hybridStats_.componentQueryNanoseconds.fetch_add(elapsed, std::memory_order_relaxed);
    hybridStats_.componentQueryCpuNanoseconds.fetch_add(cpuElapsed, std::memory_order_relaxed);
    hybridStats_.dynCpuNanoseconds.fetch_add(cpuElapsed, std::memory_order_relaxed);
    hybridStats_.dynQueryCalls.fetch_add(1, std::memory_order_relaxed);
    hybridStats_.dynQueryNanoseconds.fetch_add(elapsed, std::memory_order_relaxed);
    return components;
}

HybridDXD::SearchResult HybridDXD::searchWithoutHybridDecomposition(
    Block& block, int depth) {
    if (tlsState) {
        tlsState->decompositionPolicy.phase = CCProbePhase::Disabled;
    }
    return DXD(block, depth);
}

void HybridDXD::recordInitialDynamicSeed(
    size_t componentCount,
    const std::shared_ptr<DynamicSeed>& seed) {
    if (!statisticsEnabled() || !seed) return;

    hybridStats_.initialBfsComponents.store(
        componentCount, std::memory_order_relaxed);
    hybridStats_.initialDynamicRows.store(
        seed->activeRows.size(), std::memory_order_relaxed);
    hybridStats_.initialDynamicCols.store(
        seed->activeCols.size(), std::memory_order_relaxed);
    hybridStats_.initialDynamicEntries.store(
        seed->entries, std::memory_order_relaxed);
    hybridStats_.initialDynamicTreeEdges.store(
        seed->treeEdges.size(), std::memory_order_relaxed);
    hybridStats_.initialDynamicNonTreeEdges.store(
        seed->nonTreeEdges.size(), std::memory_order_relaxed);
}

HybridDXD::CostDecisionRecord HybridDXD::evaluateCostDecision(
    int depth,
    uint64_t areaBeforeCover,
    uint64_t areaAfterCover,
    long double stopArea,
    const DynamicSeed& seed,
    const set<int>& deletedRows,
    const set<int>& deletedCols) {
    CostDecisionRecord decision;
    if (statisticsEnabled()) {
        decision.decisionId =
            decisionSequence_.fetch_add(1, std::memory_order_relaxed);
    }
    decision.depth = depth;
    decision.areaBeforeCover = areaBeforeCover;
    decision.areaAfterCover = areaAfterCover;
    decision.stopArea = static_cast<uint64_t>(stopArea);
    decision.verticesBefore = seed.activeRows.size() + seed.activeCols.size();
    decision.edgesBefore = seed.entries;
    decision.treeEdgesBefore =
        seed.entries >= seed.nonTreeEntries
            ? seed.entries - seed.nonTreeEntries
            : 0;
    decision.nonTreeEdgesBefore = seed.nonTreeEntries;

    for (int row : deletedRows) {
        if (seed.activeRows.count(row)) ++decision.deletedVertices;
    }
    for (int column : deletedCols) {
        if (seed.activeCols.count(column)) ++decision.deletedVertices;
    }

    decision.deletedEdges = countDeletedSeedEdges(
        seed, deletedRows, deletedCols, &decision.deletedTreeEdges);
    decision.deletedNonTreeEdges = subtractClamped(
        decision.deletedEdges, decision.deletedTreeEdges);
    decision.verticesAfter = subtractClamped(
        decision.verticesBefore, decision.deletedVertices);
    decision.edgesAfter = subtractClamped(
        seed.entries, decision.deletedEdges);
    decision.treeEdgesAfter = subtractClamped(
        decision.treeEdgesBefore, decision.deletedTreeEdges);
    decision.nonTreeEdgesAfter = subtractClamped(
        seed.nonTreeEntries, decision.deletedNonTreeEdges);

    decision.logVerticesAfter = std::log2(
        static_cast<long double>(std::max<uint64_t>(
            decision.verticesAfter, 2)));
    decision.dynCost =
        static_cast<long double>(decision.deletedTreeEdges) *
            decision.logVerticesAfter +
        static_cast<long double>(decision.deletedNonTreeEdges) +
        static_cast<long double>(decision.deletedVertices);
    decision.bfsCost =
        static_cast<long double>(decision.verticesAfter) +
        static_cast<long double>(decision.edgesAfter);
    decision.dynAvailable = !seed.treeEdges.empty();
    decision.dynLocked = false;
    decision.chooseDyn =
        decision.dynAvailable &&
        decision.dynCost < decision.bfsCost;
    decision.thresholdStop =
        static_cast<long double>(areaAfterCover) <= stopArea;
    return decision;
}

void HybridDXD::recordDecrementStatistics(
    uint64_t elapsedNanoseconds,
    uint64_t verticesBefore,
    uint64_t edgesBefore,
    uint64_t treeEdgesBefore,
    uint64_t nonTreeEdgesBefore,
    uint64_t deletedVertices,
    uint64_t deletedEdges,
    uint64_t deletedTreeEdges,
    uint64_t replacementScans,
    uint64_t treeLinks) {
    hybridStats_.dynDecrementCalls.fetch_add(1, std::memory_order_relaxed);
    hybridStats_.dynDecrementNanoseconds.fetch_add(
        elapsedNanoseconds, std::memory_order_relaxed);
    hybridStats_.activeVertexSum.fetch_add(
        verticesBefore, std::memory_order_relaxed);
    hybridStats_.activeEdgeSum.fetch_add(
        edgesBefore, std::memory_order_relaxed);
    hybridStats_.activeTreeSum.fetch_add(
        treeEdgesBefore, std::memory_order_relaxed);
    hybridStats_.activeNonTreeSum.fetch_add(
        nonTreeEdgesBefore, std::memory_order_relaxed);
    hybridStats_.deletedVertexSum.fetch_add(
        deletedVertices, std::memory_order_relaxed);
    hybridStats_.deletedEdgeSum.fetch_add(
        deletedEdges, std::memory_order_relaxed);
    hybridStats_.deletedTreeSum.fetch_add(
        deletedTreeEdges, std::memory_order_relaxed);
    hybridStats_.deletedNonTreeSum.fetch_add(
        deletedEdges - deletedTreeEdges, std::memory_order_relaxed);
    hybridStats_.replacementScanSum.fetch_add(
        replacementScans, std::memory_order_relaxed);
    hybridStats_.treeCutSum.fetch_add(
        deletedTreeEdges, std::memory_order_relaxed);
    hybridStats_.treeLinkSum.fetch_add(
        treeLinks, std::memory_order_relaxed);
}

void HybridDXD::recordCostDecision(
    const CostDecisionRecord& record,
    const DynUpdateMetrics* update) {
    std::lock_guard<std::mutex> lock(decisionLogMutex_);
    if (!decisionLog_.is_open()) return;
    const uint64_t replacementScans = update
        ? update->replacementScans
        : record.replacementScans;
    const uint64_t treeLinks = update
        ? update->treeLinks
        : record.treeLinks;
    decisionLog_
        << record.decisionId
        << ',' << record.decisionKind
        << ',' << record.depth
        << ',' << record.areaBeforeCover
        << ',' << record.areaAfterCover
        << ',' << record.stopArea
        << ',' << record.verticesBefore
        << ',' << record.edgesBefore
        << ',' << record.treeEdgesBefore
        << ',' << record.nonTreeEdgesBefore
        << ',' << record.verticesAfter
        << ',' << record.edgesAfter
        << ',' << record.treeEdgesAfter
        << ',' << record.nonTreeEdgesAfter
        << ',' << record.deletedVertices
        << ',' << record.deletedEdges
        << ',' << record.deletedTreeEdges
        << ',' << record.deletedNonTreeEdges
        << ',' << static_cast<double>(record.logVerticesAfter)
        << ',';
    if (record.costEvaluated) {
        decisionLog_ << static_cast<double>(record.dynCost);
    }
    decisionLog_ << ',';
    if (record.costEvaluated) {
        decisionLog_ << static_cast<double>(record.bfsCost);
    }
    decisionLog_
        << ',' << replacementScans
        << ',' << treeLinks
        << ',' << (record.dynAvailable ? 1 : 0)
        << ',' << (record.dynLocked ? 1 : 0)
        << ',' << (record.chooseDyn ? "Dyn" : "BFS")
        << ',' << (record.chooseDyn ? "Dyn" : "BFS")
        << ',' << (record.thresholdStop ? 1 : 0)
        << '\n';
    decisionLog_.flush();
}

void HybridDXD::recordBlockTaskTiming(
    bool seeded, uint64_t nanoseconds) {
    if (!statisticsEnabled()) return;
    hybridStats_.blockTaskCalls.fetch_add(
        1, std::memory_order_relaxed);
    if (seeded) {
        hybridStats_.seedBlockTaskCalls.fetch_add(
            1, std::memory_order_relaxed);
        hybridStats_.seedBlockNanoseconds.fetch_add(
            nanoseconds, std::memory_order_relaxed);
    } else {
        hybridStats_.ordinaryBlockTaskCalls.fetch_add(
            1, std::memory_order_relaxed);
        hybridStats_.ordinaryBlockNanoseconds.fetch_add(
            nanoseconds, std::memory_order_relaxed);
    }
}

void HybridDXD::markSeedDeleted(
    const std::shared_ptr<DynamicSeed>& seed,
    const set<int>& deletedRows,
    const set<int>& deletedCols) {
    if (!seed) return;
    for (int row : deletedRows) seed->activeRows.erase(row);
    for (int column : deletedCols) seed->activeCols.erase(column);
}

HybridDXD::DynUpdateMetrics HybridDXD::updateSeedEtt(
    const std::shared_ptr<DynamicSeed>& seed,
    const set<int>& deletedRows,
    const set<int>& deletedCols) {
    DynUpdateMetrics metrics;
    if (!seed || !seed->ettBuilt ||
        (deletedRows.empty() && deletedCols.empty())) {
        return metrics;
    }
    const bool collectStats = statisticsEnabled();
    const auto started = collectStats
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    const std::clock_t cpuStart = collectStats
        ? std::clock()
        : std::clock_t{0};
    const uint64_t vBefore =
        seed->activeRows.size() + seed->activeCols.size();
    const uint64_t eBefore = seed->entries;
    const uint64_t etBefore = countTreeEdgesInForest();
    const uint64_t enBefore = seed->nonTreeEntries;
    std::unordered_set<uint64_t> deletedEdges;
    for (int row : deletedRows) {
        auto it = row_to_cols.find(row);
        if (it == row_to_cols.end()) continue;
        for (int column : it->second) {
            if (seed->activeCols.count(column)) {
                deletedEdges.insert(incidenceEdgeCode(row, column));
            }
        }
    }
    for (int column : deletedCols) {
        auto it = col_to_rows.find(column);
        if (it == col_to_rows.end()) continue;
        for (int row : it->second) {
            if (seed->activeRows.count(row)) {
                deletedEdges.insert(incidenceEdgeCode(row, column));
            }
        }
    }
    uint64_t deletedTree = 0;
    uint64_t replacementScans = 0;
    uint64_t treeLinks = 0;
    for (uint64_t code : deletedEdges) {
        seed->nonTreeEdgeSet.erase(code);
    }
    for (uint64_t code : deletedEdges) {
        const int row = static_cast<int>(code >> 32);
        const int column = static_cast<int>(code & 0xffffffffULL);
        if (!isTreeEdgeInForest(row, column)) continue;
        ++deletedTree;
        seed->treeEdgeSet.erase(code);
        for (auto& tree : tlsState->components) {
            if (!tree || !tree->containsVertex(row)) continue;
            splaytree::ReplacementSearchMetrics searchMetrics;
            auto splitTree = tree->cutWithReplacement(
                row, ROWS + column, &searchMetrics);
            if (searchMetrics.searched) {
                replacementScans += searchMetrics.scanSteps;
            }
            if (searchMetrics.found) {
                ++treeLinks;
                int replacementRow = searchMetrics.replacementU;
                int replacementColumn = searchMetrics.replacementV;
                if (replacementRow >= ROWS) {
                    replacementRow -= ROWS;
                    std::swap(replacementRow, replacementColumn);
                } else {
                    replacementColumn -= ROWS;
                }
                const uint64_t replacementCode =
                    incidenceEdgeCode(
                        replacementRow, replacementColumn);
                seed->treeEdgeSet.insert(replacementCode);
                seed->nonTreeEdgeSet.erase(replacementCode);
            }
            if (splitTree) {
                bool duplicateRoot = false;
                for (const auto& existing : tlsState->components) {
                    if (existing &&
                        existing->getRoot() == splitTree->getRoot()) {
                        duplicateRoot = true;
                        break;
                    }
                }
                if (duplicateRoot) splitTree->root = nullptr;
                splitTree->treeId = static_cast<int>(
                    tlsState->nextTreeId++);
                tlsState->components.push_back(std::move(splitTree));
            }
            break;
        }
    }
    uint64_t removedVertices = 0;
    for (int row : deletedRows) {
        if (seed->activeRows.count(row)) ++removedVertices;
    }
    for (int column : deletedCols) {
        if (seed->activeCols.count(column)) ++removedVertices;
    }
    markSeedDeleted(seed, deletedRows, deletedCols);
    seed->entries -= std::min(seed->entries, deletedEdges.size());
    for (int row : deletedRows) {
        for (auto& tree : tlsState->components) {
            if (tree && tree->containsVertex(row)) {
                tree->removeVertex(row);
                break;
            }
        }
    }
    for (int column : deletedCols) {
        const int vertex = ROWS + column;
        for (auto& tree : tlsState->components) {
            if (tree && tree->containsVertex(vertex)) {
                tree->removeVertex(vertex);
                break;
            }
        }
    }
    tlsState->components.erase(
        std::remove_if(
            tlsState->components.begin(),
            tlsState->components.end(),
            [](const auto& tree) {
                return !tree || tree->isEmpty();
            }),
        tlsState->components.end());
    if (!tlsState->components.empty()) {
        const int anyVertex =
            tlsState->components.front()->getAnyVertex();
        if (anyVertex >= 0 && tlsState->graphContext &&
            tlsState->graphContext->graph) {
            tlsState->subgraph =
                tlsState->graphContext->graph->subgraphOf(anyVertex);
            tlsState->activeSubgraph = tlsState->subgraph;
        }
    }
    captureSeedTreeEdges(*seed);
    seed->nonTreeEntries = seed->nonTreeEdgeSet.size();
    uint64_t elapsed = 0;
    if (collectStats) {
        elapsed = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - started).count());
        recordDecrementStatistics(
            elapsed,
            vBefore,
            eBefore,
            etBefore,
            enBefore,
            removedVertices,
            deletedEdges.size(),
            deletedTree,
            replacementScans,
            treeLinks);
        hybridStats_.dynCpuNanoseconds.fetch_add(
            cpuNanosecondsSince(cpuStart),
            std::memory_order_relaxed);
    }
    metrics.elapsedNanoseconds = elapsed;
    metrics.verticesBefore = vBefore;
    metrics.edgesBefore = eBefore;
    metrics.treeEdgesBefore = etBefore;
    metrics.nonTreeEdgesBefore = enBefore;
    metrics.deletedVertices = removedVertices;
    metrics.deletedEdges = deletedEdges.size();
    metrics.deletedTreeEdges = deletedTree;
    metrics.deletedNonTreeEdges =
        metrics.deletedEdges >= metrics.deletedTreeEdges
            ? metrics.deletedEdges - metrics.deletedTreeEdges
            : 0;
    metrics.verticesAfter = seed->activeRows.size() +
                            seed->activeCols.size();
    metrics.edgesAfter = seed->entries;
    metrics.treeEdgesAfter = countTreeEdgesInForest();
    metrics.nonTreeEdgesAfter = seed->nonTreeEntries;
    metrics.replacementScans = replacementScans;
    metrics.treeLinks = treeLinks;
    return metrics;
}

HybridDXD::SearchResult HybridDXD::hybridSearchRuntime(
    Block& block, int depth,
    const HybridSearchState& initialState) {
    if (timer.timeBoundBroken()) {
        throw std::runtime_error("Time bound broken");
    }
    if (block.cols.empty()) return {DNNFResult(1), T};
    if (block.rows.empty()) return {DNNFResult(0), F};

    HybridSearchState state = initialState;
    vector<int> stateCols;
    const size_t stateHash = hashBlockState(block.cols, &stateCols);
    CacheKey stateKey{stateHash, std::move(stateCols), false};
    DNNFResult cachedCount(0);
    shared_ptr<DNNFNode> cachedNode;
    if (getCachedResult(stateKey, cachedCount, cachedNode)) {
        return {cachedCount, cachedNode};
    }

    const uint64_t currentArea =
        static_cast<uint64_t>(block.rows.size()) *
        static_cast<uint64_t>(block.cols.size());
    if (isBelowDecompositionStopThreshold(currentArea)) {
        return searchWithoutHybridDecomposition(block, depth);
    }

    if (state.mode == SearchMode::NoDecomposition) {
        return searchWithoutHybridDecomposition(block, depth);
    }
    if (state.mode == SearchMode::BfsNext) {
        IncidenceBfsResult bfs = runIncidenceBfs(block);
        if (bfs.components.empty()) {
            setCachedResult(stateKey, DNNFResult(0), F);
            return {DNNFResult(0), F};
        }
        if (bfs.components.size() > 1) {
            if (statisticsEnabled()) {
                hybridStats_.splitEvents.fetch_add(1, std::memory_order_relaxed);
            }
            updateDecompositionStopThresholdForSplit(bfs.components);
            configureBfsCounting(bfs.components);
            vector<HybridSearchState> states;
            if (usesPerBlockDecomposition()) {
                states = makePerBlockProbeStates(
                    bfs.components, &bfs);
            } else {
                states = makeNormalSplitStates(bfs.components);
                const size_t minimum =
                    selectSeedBlock(bfs.components);
                if (states[minimum].mode ==
                    SearchMode::BfsProbe) {
                    states[minimum].seed = makeDynamicSeed(
                        bfs.components[minimum], bfs);
                    states[minimum].skipInitialDecomposition =
                        true;
                }
            }
            auto result = searchIndependentBlocks(
                bfs.components, states, depth, false);
            setCachedResult(stateKey, result.first, result.second);
            return result;
        }
        if (state.stopAfterQuery) {
            return searchWithoutHybridDecomposition(block, depth);
        }
        state.seed = makeDynamicSeed(bfs.components.front(), bfs);
        state.skipInitialDecomposition = true;
        state.mode = SearchMode::BfsProbe;
    }

    if (state.mode == SearchMode::BfsThreshold) {
        vector<Block> components = runPlainIncidenceBfs(block);
        if (components.empty()) {
            setCachedResult(stateKey, DNNFResult(0), F);
            return {DNNFResult(0), F};
        }
        if (components.size() > 1) {
            if (statisticsEnabled()) {
                hybridStats_.splitEvents.fetch_add(
                    1, std::memory_order_relaxed);
            }
            updateDecompositionStopThresholdForSplit(components);
            configureBfsCounting(components);
            vector<HybridSearchState> states;
            if (usesPerBlockDecomposition()) {
                states = makePerBlockProbeStates(components, nullptr);
            } else {
                states = makeNormalSplitStates(components);
            }
            auto result = searchIndependentBlocks(
                components, states, depth, false);
            setCachedResult(stateKey, result.first, result.second);
            return result;
        }
    }

    if (state.mode == SearchMode::DynQuery) {
        vector<Block> components = runDynComponentQuery(block);
        if (components.size() > 1) {
            if (statisticsEnabled()) {
                hybridStats_.splitEvents.fetch_add(1, std::memory_order_relaxed);
            }
            updateDecompositionStopThresholdForSplit(components);
            vector<HybridSearchState> states;
            if (usesPerBlockDecomposition()) {
                states = makePerBlockProbeStates(components, nullptr);
            } else {
                states = makeNormalSplitStates(components);
            }
            auto result = searchIndependentBlocks(components, states, depth, true);
            setCachedResult(stateKey, result.first, result.second);
            return result;
        }
        if (state.stopAfterQuery) {
            return searchWithoutHybridDecomposition(block, depth);
        }
        state.mode = SearchMode::DynPath;
        state.skipInitialDecomposition = false;
    }

    if (state.mode == SearchMode::BfsProbe) {
        if (state.skipInitialDecomposition) {
            state.skipInitialDecomposition = false;
        } else {
            IncidenceBfsResult bfs = runIncidenceBfs(block);
            if (bfs.components.empty()) {
                setCachedResult(stateKey, DNNFResult(0), F);
                return {DNNFResult(0), F};
            }
            if (depth == 1) {
                recordInitialDecompositionBlockCount(
                    bfs.components.size());
                if (bfs.components.size() == 1 &&
                    !decompositionStopThresholdReady.load(
                        std::memory_order_acquire)) {
                    initializeSingleBlockStopThreshold(
                        currentArea);
                }
            }
            if (bfs.components.size() > 1) {
                if (statisticsEnabled()) {
                    hybridStats_.splitEvents.fetch_add(1, std::memory_order_relaxed);
                    hybridStats_.initialBfsMultiSplits.fetch_add(1, std::memory_order_relaxed);
                }
                updateDecompositionStopThresholdForSplit(
                    bfs.components);
                configureBfsCounting(bfs.components);
                if (usesPerBlockDecomposition()) {
                    vector<HybridSearchState> states =
                        makePerBlockProbeStates(
                            bfs.components, &bfs);
                    if (depth == 1 && !states.empty()) {
                        recordInitialDynamicSeed(
                            bfs.components.size(), states.front().seed);
                    }
                    auto result = searchIndependentBlocks(
                        bfs.components, states, depth, false);
                    setCachedResult(
                        stateKey, result.first, result.second);
                    return result;
                }
                vector<HybridSearchState> states =
                    makeNormalSplitStates(bfs.components);
                const size_t selected =
                    selectSeedBlock(bfs.components);
                if (states[selected].mode ==
                    SearchMode::BfsProbe) {
                    auto selectedSeed =
                        makeDynamicSeed(
                            bfs.components[selected], bfs);
                    if (depth == 1) {
                        recordInitialDynamicSeed(
                            bfs.components.size(),
                            selectedSeed);
                    }
                    states[selected].seed =
                        std::move(selectedSeed);
                    states[selected].skipInitialDecomposition =
                        true;
                }
                auto result = searchIndependentBlocks(
                    bfs.components, states, depth, false);
                setCachedResult(stateKey, result.first, result.second);
                return result;
            }
            state.seed = makeDynamicSeed(bfs.components.front(), bfs);
            if (depth == 1) {
                recordInitialDynamicSeed(1, state.seed);
            }
            state.skipInitialDecomposition = true;
        }
    }

    const bool boundedBfs = state.mode == SearchMode::BfsThreshold;
    if (!state.seed && !boundedBfs) {
        return searchWithoutHybridDecomposition(block, depth);
    }

    ColumnHeader* choose = selectOptimalColumn(block.cols);
    if (!choose || choose->size <= 0) {
        setCachedResult(stateKey, DNNFResult(0), F);
        return {DNNFResult(0), F};
    }

    const uint64_t decisionAreaBeforeCover =
        static_cast<uint64_t>(block.rows.size()) *
        static_cast<uint64_t>(block.cols.size());
    set<int> commonDeletedRows;
    set<int> commonDeletedCols{choose->col};
    const bool commonMaintainRows = true;
    coverInBlock(choose->col, block, commonDeletedRows, commonMaintainRows);

    const bool enteredWithDyn =
        state.seed && state.seed->ettBuilt &&
        hasLocalEttStorage();
    if (!boundedBfs && state.seed && !state.probeContext) {
        state.probeContext = std::make_shared<ProbeContext>();
        state.probeContext->initialArea =
            state.seed->selectedBlockInitialArea;
        state.probeContext->stopRatio =
            state.seed->thresholdRatio;
    }
    const long double frameStopArea =
        static_cast<long double>(decompositionStopThresholdValue());
    CostDecisionRecord frameDecision;
    bool frameDecisionEvaluated = false;
    if (!boundedBfs && state.seed) {
        const uint64_t areaAfterCommonCover =
            static_cast<uint64_t>(block.rows.size()) *
            static_cast<uint64_t>(block.cols.size());
        frameDecision = evaluateCostDecision(
            depth,
            decisionAreaBeforeCover,
            areaAfterCommonCover,
            frameStopArea,
            *state.seed,
            commonDeletedRows,
            commonDeletedCols);
        frameDecisionEvaluated = true;
        recordCostDecision(frameDecision);
    }

    DNNFResult totalResult(0);
    shared_ptr<DNNFNode> rootNode = F;
    EttForestSnapshot dynamicForestBaseline;
    bool dynamicBranchStarted = false;
    Node* curC = choose->down;
    while (curC != choose) {
        set<int> branchDeletedRows;
        set<int> branchDeletedCols;
        vector<int> coveredCols;
        const bool branchMaintainRows = true;
        try {
            for (Node* curR = curC->right; curR != curC;
                 curR = curR->right) {
                branchDeletedCols.insert(curR->col);
                coverInBlock(
                    curR->col, block, branchDeletedRows,
                    branchMaintainRows);
                coveredCols.push_back(curR->col);
            }

            SearchResult branchResult;
            const set<int> deletedRows =
                mergedSet(commonDeletedRows, branchDeletedRows);
            const set<int> deletedCols =
                mergedSet(commonDeletedCols, branchDeletedCols);
            const uint64_t areaAfterCover =
                static_cast<uint64_t>(block.rows.size()) *
                static_cast<uint64_t>(block.cols.size());
            const bool thresholdStop =
                !boundedBfs &&
                isBelowDecompositionStopThreshold(areaAfterCover);

            HybridSearchState childState = state;
            const bool chooseDyn =
                frameDecisionEvaluated && frameDecision.chooseDyn;

            if (thresholdStop) {
                releaseLocalEtt();
                childState.seed.reset();
                childState.mode = SearchMode::NoDecomposition;
                childState.stopAfterQuery = false;
                childState.skipInitialDecomposition = false;
            } else if (chooseDyn) {
                auto branchSeed =
                    std::make_shared<DynamicSeed>(*state.seed);
                if (statisticsEnabled()) {
                    hybridStats_.dynStrategySelections.fetch_add(
                        1, std::memory_order_relaxed);
                }
                if (dynamicBranchStarted) {
                    restoreLocalEttForest(
                        state.seed, dynamicForestBaseline);
                    branchSeed->ettBuilt = true;
                } else if (enteredWithDyn) {
                    dynamicForestBaseline =
                        cloneLocalEttForest();
                } else {
                    branchSeed->ettBuilt = false;
                    buildSeedEtt(branchSeed);
                    dynamicForestBaseline =
                        cloneLocalEttForest();
                }
                dynamicBranchStarted = true;
                updateSeedEtt(branchSeed, deletedRows, deletedCols);
                childState.seed = std::move(branchSeed);
                childState.mode = SearchMode::DynQuery;
                childState.stopAfterQuery = false;
                childState.skipInitialDecomposition = false;
            } else {
                releaseLocalEtt();
                childState.seed.reset();
                childState.mode = boundedBfs
                    ? SearchMode::BfsThreshold
                    : SearchMode::BfsNext;
                childState.stopAfterQuery = false;
                childState.skipInitialDecomposition = false;
            }
            branchResult = hybridSearchRuntime(block, depth + 1, childState);
            releaseLocalEtt();
            auto& [branchCount, branchNode] = branchResult;
            if (!branchCount.isZero()) {
                rootNode = buildDecisionNode(
                    curC->row, rootNode, branchNode);
                totalResult = totalResult + branchCount;
            }
        } catch (...) {
            releaseLocalEtt();
            for (auto it = coveredCols.rbegin();
                 it != coveredCols.rend(); ++it) {
                uncoverInBlock(*it, block, branchMaintainRows);
            }
            uncoverInBlock(choose->col, block, commonMaintainRows);
            throw;
        }

        for (auto it = coveredCols.rbegin();
             it != coveredCols.rend(); ++it) {
            uncoverInBlock(*it, block, branchMaintainRows);
        }
        curC = curC->down;
    }
    uncoverInBlock(choose->col, block, commonMaintainRows);
    setCachedResult(stateKey, totalResult, rootNode);
    return {totalResult, rootNode};
}

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
    bfsCallCounter = &hybridStats_.bfsCalls;
    componentQueryCallCounter = &hybridStats_.componentQueryCalls;
    if (collectCCExperimentStats) ccExperimentStats.reset();
    resetCCCacheStatistics();
    resetDecompositionStopThreshold();

    activeSearchState = nullptr;
    mainSearchState.reset(debug);
    decisionSequence_.store(0, std::memory_order_relaxed);
    decisionLog_.close();
    if (!config_.decisionLogPath.empty()) {
        const fs::path path(config_.decisionLogPath);
        if (!path.parent_path().empty()) {
            fs::create_directories(path.parent_path());
        }
        decisionLog_.open(
            config_.decisionLogPath, std::ios::out | std::ios::trunc);
        if (!decisionLog_) {
            throw std::runtime_error(
                "cannot open Hybrid decision log: " +
                config_.decisionLogPath);
        }
        decisionLog_
            << "decision_id,decision_kind,depth,area_before_cover,area_after_cover,"
               "stop_area,V_before,E_before,ET_before,EN_before,"
               "V_after,E_after,ET_after,EN_after,Vd,Ed,ETd,ENd,"
               "log2_V_after,C_dyn,C_bfs,Er,tree_links,can_use_dyn,"
               "dyn_locked,choice,operative_mode,threshold_stop\n";
        decisionLog_.flush();
        logger.logLine(
            "Hybrid Decision Log: " + config_.decisionLogPath);
    }

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
        sharedDNNCache =
            std::make_shared<LockFreeDNNCache>();
        taskScheduler_ = std::make_unique<TaskScheduler>(
            *this, config_.outerParallelThreads);
        initThreadLocalState(InitBlock, nullptr);

        timer.reset();
        timer.markStartTime();
        const auto start = std::chrono::high_resolution_clock::now();

        auto [result, compiledRoot] =
            hybridSearchRuntime(InitBlock, 1, HybridSearchState{});
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
        decisionLog_.close();

        timeout = false;
        solutionCount = result.toString();
        logger.logLine("Time: " + std::to_string(searchTime) + " s");
        if (debug) {
            logSearchStateStats("hybrid", mainSearchState);
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
        logCCExperimentStats(true);
        if (!skipDNNFSize) {
            logger.logLine(
                "DNNF Size: " + std::to_string(countDNNFNodes()));
        }
        if (debug || collectCCExperimentStats) logSummary();
    } catch (const std::runtime_error& e) {
        taskScheduler_.reset();
        sharedDNNCache.reset();
        releaseLocalEtt();
        cleanupThreadLocalState();
        decisionLog_.close();
        timer.markStopTime();

        const std::string message = e.what();
        if (message.find("Time bound") == std::string::npos) {
            logger.logLine("HybridDXD内部错误: " + message);
            throw;
        }

        timeout = true;
        logger.logLine("Time: " + std::to_string(timer.getElapsedTime()) + " s");
        logger.logLine(
            "Max Blocks: " +
            std::to_string(MAX_B_COUNT.load(std::memory_order_relaxed)));
        logger.logLine("HybridDXD搜索超时: " + message);
        logCCExperimentStats(false);
        if (debug || collectCCExperimentStats) logSummary();
    }
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
        const bool collectStats = statisticsEnabled();
        for (int i = 0; i < count; ++i) {
            const auto taskStart = collectStats
                ? std::chrono::steady_clock::now()
                : std::chrono::steady_clock::time_point{};
            auto [result, node] =
                searchOneIndependentBlock(blocks[i], states[i], depth + 1);
            if (collectStats) {
                const uint64_t elapsed =
                    static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now() - taskStart)
                            .count());
                recordBlockTaskTiming(
                    states[i].seed != nullptr, elapsed);
                logger.logLine(
                    "[hybrid-task] group_depth=" +
                    std::to_string(depth) +
                    " index=" + std::to_string(i) +
                    " rows=" + std::to_string(blocks[i].rows.size()) +
                    " cols=" + std::to_string(blocks[i].cols.size()) +
                    " mode=serial seed=" +
                    (states[i].seed ? "1" : "0") +
                    " ran=1 seconds=" +
                    std::to_string(
                        static_cast<double>(elapsed) / 1e9));
            }
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
        SearchResult result = hybridSearchRuntime(block, depth, state);
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

void HybridDXD::configureBfsCounting(vector<Block>& blocks) const {
    const uint64_t stopThreshold =
        decompositionStopThresholdValue();
    for (Block& block : blocks) {
        block.bfsStopArea = stopThreshold;
        block.activeRowCount =
            static_cast<uint64_t>(block.rows.size());
        block.trackBfsRows = true;
        block.rowCoverCounts.clear();
    }
}

size_t HybridDXD::selectSeedBlock(const vector<Block>& blocks) const {
    size_t best = 0;
    for (size_t i = 1; i < blocks.size(); ++i) {
        const uint64_t area =
            static_cast<uint64_t>(blocks[i].rows.size()) *
            blocks[i].cols.size();
        const uint64_t bestArea =
            static_cast<uint64_t>(blocks[best].rows.size()) *
            blocks[best].cols.size();
        const bool tieBreak =
            area == bestArea &&
            (blocks[i].rows.size() < blocks[best].rows.size() ||
             (blocks[i].rows.size() == blocks[best].rows.size() &&
              blocks[i].cols.size() < blocks[best].cols.size()));
        if (area < bestArea || tieBreak) {
            best = i;
        }
    }
    return best;
}

size_t HybridDXD::selectMaximumBlock(const vector<Block>& blocks) const {
    size_t best = 0;
    for (size_t i = 1; i < blocks.size(); ++i) {
        const uint64_t area =
            static_cast<uint64_t>(blocks[i].rows.size()) *
            blocks[i].cols.size();
        const uint64_t bestArea =
            static_cast<uint64_t>(blocks[best].rows.size()) *
            blocks[best].cols.size();
        const bool tieBreak =
            area == bestArea &&
            (blocks[i].rows.size() > blocks[best].rows.size() ||
             (blocks[i].rows.size() == blocks[best].rows.size() &&
              blocks[i].cols.size() > blocks[best].cols.size()));
        if (area > bestArea || tieBreak) {
            best = i;
        }
    }
    return best;
}

void HybridDXD::releaseLocalEtt() {
    if (!hasLocalEttStorage()) return;
    const bool collectStats = statisticsEnabled();
    const auto releaseStart = collectStats
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    releaseThreadLocalDynamicCCStorage();
    if (collectStats) {
        addElapsedNanoseconds(
            hybridStats_.ettReleaseNanoseconds, releaseStart);
        hybridStats_.ettReleases.fetch_add(
            1, std::memory_order_relaxed);
    }
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

uint64_t HybridDXD::cpuNanosecondsSince(std::clock_t start) {
    return static_cast<uint64_t>(
        static_cast<long double>(std::clock() - start) *
        1e9L / static_cast<long double>(CLOCKS_PER_SEC));
}

void HybridDXD::logSummary() const {
    const double componentQuerySeconds =
        static_cast<double>(hybridStats_.componentQueryNanoseconds.load(
            std::memory_order_relaxed)) / 1e9;
    const double dynSeconds =
        static_cast<double>(
            hybridStats_.dynQueryNanoseconds.load(
                std::memory_order_relaxed) +
            hybridStats_.dynDecrementNanoseconds.load(
                std::memory_order_relaxed) +
            hybridStats_.dynIncrementNanoseconds.load(
                std::memory_order_relaxed)) / 1e9;
    const double bfsSeconds =
        static_cast<double>(hybridStats_.bfsNanosecondsNew.load(
            std::memory_order_relaxed)) / 1e9;
    const double bfsCpuSeconds =
        static_cast<double>(hybridStats_.bfsCpuNanoseconds.load(
            std::memory_order_relaxed)) / 1e9;
    const double dynCpuSeconds =
        static_cast<double>(hybridStats_.dynCpuNanoseconds.load(
            std::memory_order_relaxed)) / 1e9;
    logMetric("Runtime Policy", config_.runtimePolicy ? 1 : 0);
    logger.logLine(
        usesPerBlockDecomposition()
            ? "Hybrid Decomposition Strategy: per-block-incidence-bfs-dyn-cost"
            : "Hybrid Decomposition Strategy: min-area-dyn/max-area-bfs");
    logger.logLine(
        usesPerBlockDecomposition()
            ? "Hybrid Seed Policy: per-block-dynamic-seed"
            : "Hybrid Seed Policy: min-area-dyn/max-area-bfs");
    logMetric(
        "Threshold Ratio",
        static_cast<double>(config_.thresholdRatio));
    logMetric(
        "Initial Blocks",
        initialDecompositionBlockCount.load(
            std::memory_order_relaxed));
    logMetric(
        "Minimum Block Area",
        minimumDecompositionBlockArea.load(
            std::memory_order_relaxed));
    logMetric(
        "Stop Threshold",
        decompositionStopThresholdValue());
    logMetric(
        "Initial BFS Components",
        hybridStats_.initialBfsComponents.load(std::memory_order_relaxed));
    logMetric(
        "Initial BFS Multi Splits",
        hybridStats_.initialBfsMultiSplits.load(std::memory_order_relaxed));
    logMetric(
        "Initial Dynamic Rows",
        hybridStats_.initialDynamicRows.load(std::memory_order_relaxed));
    logMetric(
        "Initial Dynamic Cols",
        hybridStats_.initialDynamicCols.load(std::memory_order_relaxed));
    logMetric(
        "Initial Dynamic Entries",
        hybridStats_.initialDynamicEntries.load(std::memory_order_relaxed));
    logMetric(
        "Initial Dynamic Tree Edges",
        hybridStats_.initialDynamicTreeEdges.load(std::memory_order_relaxed));
    logMetric(
        "Initial Dynamic Non Tree Edges",
        hybridStats_.initialDynamicNonTreeEdges.load(
            std::memory_order_relaxed));
    logMetric(
        "Block Task Calls",
        hybridStats_.blockTaskCalls.load(std::memory_order_relaxed));
    logMetric(
        "Seed Block Task Calls",
        hybridStats_.seedBlockTaskCalls.load(
            std::memory_order_relaxed));
    logSeconds(
        "Seed Block Task Time",
        static_cast<double>(
            hybridStats_.seedBlockNanoseconds.load(
                std::memory_order_relaxed)) / 1e9);
    logMetric(
        "Ordinary Block Task Calls",
        hybridStats_.ordinaryBlockTaskCalls.load(
            std::memory_order_relaxed));
    logSeconds(
        "Ordinary Block Task Time",
        static_cast<double>(
            hybridStats_.ordinaryBlockNanoseconds.load(
                std::memory_order_relaxed)) / 1e9);

    logMetric(
        "Component Query Calls",
        hybridStats_.componentQueryCalls.load(std::memory_order_relaxed));
    logSeconds("Component Query Time", componentQuerySeconds);
    logSeconds(
        "Component Query CPU",
        static_cast<double>(
            hybridStats_.componentQueryCpuNanoseconds.load(
                std::memory_order_relaxed)) / 1e9);
    logMetric(
        "Dyn Decrement Calls",
        hybridStats_.dynDecrementCalls.load(std::memory_order_relaxed));
    logMetric(
        "Dyn Increment Calls",
        hybridStats_.dynIncrementCalls.load(std::memory_order_relaxed));
    logMetric(
        "Dyn Query Calls",
        hybridStats_.dynQueryCalls.load(std::memory_order_relaxed));
    logMetric(
        "ETT Calls",
        hybridStats_.dynQueryCalls.load(std::memory_order_relaxed));
    logSeconds("Dyn Time", dynSeconds);
    logMetric(
        "BFS Calls",
        hybridStats_.bfsCalls.load(std::memory_order_relaxed));
    logSeconds("BFS Time", bfsSeconds);
    logSeconds("BFS CPU", bfsCpuSeconds);
    logSeconds("Dyn CPU", dynCpuSeconds);
    logSeconds("BFS+Dyn CPU", bfsCpuSeconds + dynCpuSeconds);
    logMetric(
        "ETT Build Calls",
        hybridStats_.ettBuildCalls.load(std::memory_order_relaxed));
    logSeconds(
        "ETT Build Time",
        static_cast<double>(hybridStats_.ettBuildNanosecondsNew.load(
            std::memory_order_relaxed)) / 1e9);
    logMetric(
        "V Before Sum",
        hybridStats_.activeVertexSum.load(std::memory_order_relaxed));
    logMetric(
        "E Before Sum",
        hybridStats_.activeEdgeSum.load(std::memory_order_relaxed));
    logMetric(
        "ET Before Sum",
        hybridStats_.activeTreeSum.load(std::memory_order_relaxed));
    logMetric(
        "EN Before Sum",
        hybridStats_.activeNonTreeSum.load(std::memory_order_relaxed));
    logMetric(
        "Vd Sum",
        hybridStats_.deletedVertexSum.load(std::memory_order_relaxed));
    logMetric(
        "Ed Sum",
        hybridStats_.deletedEdgeSum.load(std::memory_order_relaxed));
    logMetric(
        "ETd Sum",
        hybridStats_.deletedTreeSum.load(std::memory_order_relaxed));
    logMetric(
        "ENd Sum",
        hybridStats_.deletedNonTreeSum.load(std::memory_order_relaxed));
    logMetric(
        "Er Sum",
        hybridStats_.replacementScanSum.load(std::memory_order_relaxed));
    logMetric(
        "Tree Edge Cuts",
        hybridStats_.treeCutSum.load(std::memory_order_relaxed));
    logMetric(
        "Tree Edge Links",
        hybridStats_.treeLinkSum.load(std::memory_order_relaxed));
    logger.logLine("Hybrid Strategy: runtime-bipartite-dyn-bfs");
    logMetric(
        "Max BFS Stop Ratio",
        config_.thresholdRatio);
    logMetric(
        "Outer Parallel Launches",
        hybridStats_.outerParallelLaunches.load(std::memory_order_relaxed));
    logMetric(
        "Inner Parallel Launches",
        hybridStats_.innerParallelLaunches.load(std::memory_order_relaxed));
    logMetric(
        "ETT Releases",
        hybridStats_.ettReleases.load(std::memory_order_relaxed));
    logMetric(
        "Adaptive Comparisons",
        hybridStats_.adaptiveComparisons.load(std::memory_order_relaxed));
    logMetric(
        "BFS Strategy Selections",
        hybridStats_.bfsStrategySelections.load(std::memory_order_relaxed));
    logMetric(
        "Dyn Strategy Selections",
        hybridStats_.dynStrategySelections.load(std::memory_order_relaxed));
    logSeconds(
        "ETT Release Time",
        static_cast<double>(hybridStats_.ettReleaseNanoseconds.load(
            std::memory_order_relaxed)) / 1e9);
}
