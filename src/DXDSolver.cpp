#include "../include/DXD.h"

namespace {

size_t blockArea(const Block& block) {
    return block.rows.size() * block.cols.size();
}

size_t selectInitialProbeBlock(const vector<Block>& blocks,
                               size_t initialArea,
                               size_t areaThreshold) {
    if (blocks.empty()) return 0;

    const bool chooseSmallest = initialArea > areaThreshold;
    size_t best = 0;
    for (size_t i = 1; i < blocks.size(); ++i) {
        const size_t area = blockArea(blocks[i]);
        const size_t bestArea = blockArea(blocks[best]);
        const bool betterArea = chooseSmallest ? area < bestArea
                                               : area > bestArea;
        const bool betterTieBreak =
            area == bestArea &&
            (blocks[i].rows.size() < blocks[best].rows.size() ||
             (blocks[i].rows.size() == blocks[best].rows.size() &&
              (blocks[i].cols.size() < blocks[best].cols.size() ||
               (blocks[i].cols.size() == blocks[best].cols.size() &&
                !blocks[i].rows.empty() && !blocks[best].rows.empty() &&
                *blocks[i].rows.begin() < *blocks[best].rows.begin()))));
        if (betterArea || betterTieBreak) {
            best = i;
        }
    }
    return best;
}

}  // namespace

shared_ptr<DNNFNode> DanceDNNF::buildDecisionNode(int r, shared_ptr<DNNFNode> lo, shared_ptr<DNNFNode> hi) {
    if (hi == F) {
        return lo;
    }

    DecisionNodeKey key{r, lo->id, hi->id};
    SearchState& state = currentSearchState();
    const auto lookupStart = state.collectStats
                                 ? std::chrono::steady_clock::now()
                                 : std::chrono::steady_clock::time_point{};
    auto it = state.nodeTable.find(key);
    if (state.collectStats) {
        state.nodeLookupSeconds += elapsedSeconds(lookupStart);
        ++state.nodeLookups;
    }
    if (it != state.nodeTable.end()) {
        if (auto cached = it->second.lock()) {
            if (state.collectStats) ++state.nodeLookupHits;
            return cached;
        }
        state.nodeTable.erase(it);
    }

    auto decision_node = make_shared<DNNFNode>(
        state.allocateNodeId(), NodeType::Decision, lo, hi);
    createdDNNFNodes.fetch_add(1, std::memory_order_relaxed);
    decision_node->label = r;

    const auto insertStart = state.collectStats
                                 ? std::chrono::steady_clock::now()
                                 : std::chrono::steady_clock::time_point{};
    state.nodeTable[key] = decision_node;
    if (state.collectStats) {
        state.nodeInsertSeconds += elapsedSeconds(insertStart);
        ++state.nodeInserts;
    }

    return decision_node;
}

shared_ptr<DNNFNode> DanceDNNF::buildDecomposableNode(vector<shared_ptr<DNNFNode>>& subDNNFs) {
    SearchState& state = currentSearchState();
    auto decompose_node = make_shared<DNNFNode>(
        state.allocateNodeId(), NodeType::Decomposed, -3);
    createdDNNFNodes.fetch_add(1, std::memory_order_relaxed);
    decompose_node->children = subDNNFs;
    return decompose_node;
}

//   Process each independent block sequentially.
//   Returns the product of solution counts AND the Decomposed-AND node that
//   connects all sub-DNNF roots (enabling independent AND decomposition).
std::pair<DNNFResult, shared_ptr<DNNFNode>> DanceDNNF::serialSearch(
    vector<Block>& blocks, int parent_depth,
    vector<DecompositionPolicy> policies) {

    DNNFResult totalResult(1);
    vector<shared_ptr<DNNFNode>> subNodes;
    subNodes.reserve(blocks.size());

    if (policies.size() != blocks.size()) {
        policies.assign(blocks.size(), DecompositionPolicy{});
    }

    const bool needsComponentTrees =
        isDynDXD() &&
        std::any_of(policies.begin(), policies.end(),
                    [](const DecompositionPolicy& policy) {
                        return policy.phase == CCProbePhase::Ett;
                    });
    DecompositionPolicy& policy = currentDecompositionPolicy();
    const DecompositionPolicy outerPolicy = policy;
    auto searchIndependentBlock =
        [&](Block& childBlock)
        -> std::pair<DNNFResult, shared_ptr<DNNFNode>> {
        SearchState* previousState = activeSearchState;
        SearchState childState;
        childState.collectStats = debug;
        childState.mutation.enabled = debug;
        activeSearchState = &childState;
        try {
            auto result = DXD(childBlock, parent_depth + 1);
            if (debug) {
                SearchState& aggregate =
                    previousState ? *previousState : mainSearchState;
                aggregate.mergeStatsFrom(childState);
            }
            activeSearchState = previousState;
            return result;
        } catch (...) {
            activeSearchState = previousState;
            throw;
        }
    };

    if (!needsComponentTrees || getComponents().empty()) {
        try {
            for (size_t i = 0; i < blocks.size(); ++i) {
                policy = policies[i];
                auto [result, node] = searchIndependentBlock(blocks[i]);
                if (result.isZero()) {
                    policy = outerPolicy;
                    return {DNNFResult(0), F};
                }
                totalResult = totalResult * result;
                subNodes.push_back(node);
            }
        } catch (...) {
            policy = outerPolicy;
            throw;
        }
        policy = outerPolicy;
        auto decompNode = buildDecomposableNode(subNodes);
        return {totalResult, decompNode};
    }

    SubGraph*& subgraphRoute = currentSubgraphRoute();
    SubGraph* outerSubgraph = subgraphRoute;

    std::vector<std::vector<std::unique_ptr<splaytree::EulerTourTree>>> stash;

    auto& comps = getComponents();

    std::vector<std::unique_ptr<splaytree::EulerTourTree>> forest;
    forest.swap(comps);
    stash.resize(blocks.size());
    std::vector<bool> used(forest.size(), false);
    for (size_t i = 0; i < blocks.size(); ++i) {
        if (policies[i].phase != CCProbePhase::Ett) continue;
        for (size_t j = 0; j < forest.size(); ++j) {
            if (used[j] || !forest[j]) continue;
            const int representative = forest[j]->getAnyVertex();
            if (representative >= 0 && blocks[i].rows.count(representative)) {
                stash[i].push_back(std::move(forest[j]));
                used[j] = true;
                break;
            }
        }
        if (stash[i].empty()) {
            for (auto& tree : stash)
                for (auto& component : tree)
                    if (component) comps.push_back(std::move(component));
            for (auto& tree : forest)
                if (tree) comps.push_back(std::move(tree));
            subgraphRoute = outerSubgraph;
            return {DNNFResult(0), F};
        }
    }
    for (size_t j = 0; j < forest.size(); ++j) {
        if (!used[j] && forest[j]) {
            // The forest must describe exactly the blocks being searched.
            for (auto& tree : stash)
                for (auto& component : tree)
                    if (component) comps.push_back(std::move(component));
            for (auto& tree : forest)
                if (tree) comps.push_back(std::move(tree));
            subgraphRoute = outerSubgraph;
            return {DNNFResult(0), F};
        }
    }

    auto restoreStash = [&]() {
        comps.clear();
        for (auto& t : stash)
            for (auto& component : t)
                if (component) comps.push_back(std::move(component));
        subgraphRoute = outerSubgraph;
    };

    try {
        for (size_t i = 0; i < blocks.size(); ++i) {
        const bool usesComponentTree =
            policies[i].phase == CCProbePhase::Ett;

        if (usesComponentTree && stash[i].empty()) {
            std::cerr << "serialSearch: component " << i << " missing\n";
            restoreStash();
            policy = outerPolicy;
            return {DNNFResult(0), F};
        }

        if (usesComponentTree) {
            int anyV = stash[i].front()->getAnyVertex();
            subgraphRoute =
                (anyV >= 0) ? currentGraph()->subgraphOf(anyV) : nullptr;
            for (auto& component : stash[i]) {
                comps.push_back(std::move(component));
            }
        } else {
            subgraphRoute = nullptr;
        }

        policy = policies[i];

        std::pair<DNNFResult, shared_ptr<DNNFNode>> childResult;
        try {
            childResult = searchIndependentBlock(blocks[i]);
        } catch (...) {
            policy = outerPolicy;
            throw;
        }
        auto& [result, node] = childResult;

        if (usesComponentTree) {
            stash[i] = std::move(comps);
            comps.clear();
        }

        if (result.isZero()) {
            restoreStash();
            policy = outerPolicy;
            return {DNNFResult(0), F};
        }
        totalResult = totalResult * result;
        subNodes.push_back(node);
        }
    } catch (...) {
        size_t target = 0;
        while (target < stash.size() && !stash[target].empty()) {
            ++target;
        }
        if (target == stash.size() && !stash.empty()) target = 0;
        if (target < stash.size()) {
            for (auto& component : comps) {
                if (component) stash[target].push_back(std::move(component));
            }
            comps.clear();
        }
        restoreStash();
        policy = outerPolicy;
        throw;
    }
    restoreStash();
    policy = outerPolicy;

    auto decompNode = buildDecomposableNode(subNodes);
    return {totalResult, decompNode};
}

//   Launch one OpenMP thread per independent block.
//   Returns the product of counts AND the Decomposed-AND node.
std::pair<DNNFResult, shared_ptr<DNNFNode>> DanceDNNF::parallelSearchUseOmp(
    vector<Block>& blocks, int parent_depth, bool componentTreesAvailable,
    vector<DecompositionPolicy> policies) {

    const int n = blocks.size();
    if (policies.size() != blocks.size()) {
        policies.assign(blocks.size(), DecompositionPolicy{});
    }
    std::atomic<bool> has_failure(false);
    std::atomic<bool> has_timeout(false);

    std::vector<std::unique_ptr<splaytree::EulerTourTree>> extracted(n);
    std::vector<std::vector<std::unique_ptr<splaytree::EulerTourTree>>>
        returned(n);
    
    auto& comps = getComponents();
    bool manageTrees = isDynDXD() && componentTreesAvailable;
    const bool manageThreadState = isDynDXD();
    if (manageTrees) {
        std::vector<int> treeForBlock(n, -1);
        std::vector<bool> treeUsed(comps.size(), false);
        for (int i = 0; i < n && manageTrees; ++i) {
            if (policies[i].phase != CCProbePhase::Ett) continue;
            for (size_t j = 0; j < comps.size(); ++j) {
                if (treeUsed[j] || !comps[j]) continue;
                const int representative = comps[j]->getAnyVertex();
                if (representative >= 0 && blocks[i].rows.count(representative)) {
                    treeForBlock[i] = static_cast<int>(j);
                    treeUsed[j] = true;
                    break;
                }
            }
            if (treeForBlock[i] < 0) manageTrees = false;
        }
        if (manageTrees) {
            for (int i = 0; i < n; ++i) {
                if (treeForBlock[i] >= 0) {
                    extracted[i] =
                        std::move(comps[treeForBlock[i]]);
                }
            }
            comps.clear();
        }
    }

    std::vector<DNNFResult> results(n);
    std::vector<shared_ptr<DNNFNode>>  nodes(n, nullptr);
    std::vector<ThreadRunStats> taskStats;
    std::mutex taskStatsMutex;
    if (debug) taskStats.reserve(n);

    #pragma omp parallel for schedule(dynamic)
    for (int i = 0; i < n; i++) {
        if (has_failure.load(std::memory_order_acquire) || 
            has_timeout.load(std::memory_order_acquire)) {
            if (manageTrees && extracted[i]) {
                returned[i].push_back(std::move(extracted[i]));
            }
            continue;
        }

        std::unique_ptr<ThreadLocalState> outerTls;
        if (manageThreadState) {
            outerTls = std::move(tlsState);
        }

        SearchState* previousState = activeSearchState;
        SearchState taskState;
        taskState.collectStats = debug;
        taskState.mutation.enabled = debug;
        activeSearchState = &taskState;
        const auto taskStart = std::chrono::steady_clock::now();
        const int ompThread = omp_get_thread_num();
        auto returnTaskComponents = [&]() {
            if (!manageTrees || !tlsState) return;
            for (auto& tree : tlsState->components) {
                if (tree) returned[i].push_back(std::move(tree));
            }
            tlsState->components.clear();
        };

        try {
            // === 初始化线程局部状态 ===
            if(manageThreadState) {
                const int outerDepth = outerTls ? outerTls->decompose_depth : 0;
                initThreadLocalState(blocks[i],
                    manageTrees ? std::move(extracted[i]) : nullptr);
                if (tlsState) {
                    tlsState->decompose_depth = outerDepth + 1;
                    tlsState->decompositionPolicy = policies[i];
                    if (!manageTrees &&
                        tlsState->decompositionPolicy.phase == CCProbePhase::Ett) {
                        tlsState->decompositionPolicy.phase =
                            tlsState->decompositionPolicy.ettFailureAction ==
                                    EttFailureAction::FallbackToBfs
                                ? CCProbePhase::Bfs
                                : CCProbePhase::Disabled;
                    }
                }
            }

            // === 执行搜索 ===
            auto [result, node] = DXD(blocks[i], parent_depth + 1);

            if (result.isZero()) {
                has_failure.store(true, std::memory_order_release);
            } else {
                results[i] = result;
                nodes[i] = node;
            }

            returnTaskComponents();
        } catch (const std::runtime_error& e) {
            std::string msg = e.what();
            if (msg.find("Time bound") != std::string::npos) {
                has_timeout.store(true, std::memory_order_release);
            } else {
                has_failure.store(true, std::memory_order_release);
                std::cerr << "Thread " << omp_get_thread_num() 
                         << " error: " << msg << "\n";
            }
            returnTaskComponents();
        } catch (const std::exception& e) {
            has_failure.store(true, std::memory_order_release);
            std::cerr << "Thread " << omp_get_thread_num() 
                     << " exception: " << e.what() << "\n";
            returnTaskComponents();
        } catch (...) {
            has_failure.store(true, std::memory_order_release);
            std::cerr << "Thread " << omp_get_thread_num() 
                     << " unknown error\n";
            returnTaskComponents();
        }

        if (manageThreadState) {
            for (auto& tree : returned[i]) {
                rebindTreeReplacementProvider(tree.get(), outerTls.get());
            }
            cleanupThreadLocalState();
            tlsState = std::move(outerTls);
        }

        const double taskSeconds = elapsedSeconds(taskStart);
        if (debug) {
            ThreadRunStats stats;
            stats.taskIndex = i;
            stats.ompThread = ompThread;
            stats.rows = blocks[i].rows.size();
            stats.cols = blocks[i].cols.size();
            stats.solveSeconds = taskSeconds;
            stats.cacheReads = taskState.cacheReads;
            stats.cacheHits = taskState.cacheHits;
            stats.cacheWrites = taskState.cacheWrites;
            stats.nodeLookups = taskState.nodeLookups;
            stats.nodeLookupHits = taskState.nodeLookupHits;
            stats.nodeInserts = taskState.nodeInserts;
            stats.nodesAllocated = taskState.nodesAllocated;
            stats.cacheReadSeconds = taskState.cacheReadSeconds;
            stats.cacheWriteSeconds = taskState.cacheWriteSeconds;
            stats.stateHashSeconds = taskState.stateHashSeconds;
            stats.chooseColumnSeconds = taskState.chooseColumnSeconds;
            stats.decompositionProbeSeconds =
                taskState.decompositionProbeSeconds;
            stats.nodeLookupSeconds = taskState.nodeLookupSeconds;
            stats.nodeInsertSeconds = taskState.nodeInsertSeconds;
            stats.stateHashCalls = taskState.stateHashCalls;
            stats.chooseColumnCalls = taskState.chooseColumnCalls;
            stats.decompositionProbeCalls =
                taskState.decompositionProbeCalls;
            stats.cacheKeyColumns = taskState.cacheKeyColumns;
            stats.lockWaitSeconds = taskState.lockWaitSeconds;
            stats.coverSeconds = taskState.mutation.coverSeconds;
            stats.uncoverSeconds = taskState.mutation.uncoverSeconds;
            stats.coverSetSeconds = taskState.mutation.coverSetSeconds;
            stats.uncoverSetSeconds = taskState.mutation.uncoverSetSeconds;
            stats.removedRowsClearSeconds =
                taskState.mutation.removedRowsClearSeconds;
            stats.coverCalls = taskState.mutation.coverCalls;
            stats.uncoverCalls = taskState.mutation.uncoverCalls;
            stats.coverSetOps = taskState.mutation.coverSetOps;
            stats.uncoverSetOps = taskState.mutation.uncoverSetOps;
            stats.removedRowsClearCalls =
                taskState.mutation.removedRowsClearCalls;

            std::lock_guard<std::mutex> statsLock(taskStatsMutex);
            taskStats.push_back(stats);
        }
        activeSearchState = previousState;
    }

    if (manageTrees) {
        comps.clear();
        for (auto& forest : returned) {
            for (auto& tree : forest) {
                if (tree) comps.push_back(std::move(tree));
            }
        }
    }

    if (debug && !taskStats.empty()) {
        std::sort(taskStats.begin(), taskStats.end(),
                  [](const ThreadRunStats& left, const ThreadRunStats& right) {
                      return left.taskIndex < right.taskIndex;
                  });

        for (const ThreadRunStats& stats : taskStats) {
            const double hitRate =
                stats.cacheReads == 0
                    ? 0.0
                    : static_cast<double>(stats.cacheHits) / stats.cacheReads;
            logger.logLine(
                "[thread-task] task=" + std::to_string(stats.taskIndex) +
                " omp_thread=" + std::to_string(stats.ompThread) +
                " rows=" + std::to_string(stats.rows) +
                " cols=" + std::to_string(stats.cols) +
                " area=" + std::to_string(stats.rows * stats.cols) +
                " solve_sec=" + std::to_string(stats.solveSeconds) +
                " cache_reads=" + std::to_string(stats.cacheReads) +
                " cache_hits=" + std::to_string(stats.cacheHits) +
                " cache_hit_rate=" + std::to_string(hitRate) +
                " cache_writes=" + std::to_string(stats.cacheWrites) +
                " nodes_allocated=" + std::to_string(stats.nodesAllocated) +
                " cache_read_sec=" + std::to_string(stats.cacheReadSeconds) +
                " cache_write_sec=" + std::to_string(stats.cacheWriteSeconds) +
                " state_hash_sec=" + std::to_string(stats.stateHashSeconds) +
                " choose_col_sec=" +
                std::to_string(stats.chooseColumnSeconds) +
                " probe_sec=" +
                std::to_string(stats.decompositionProbeSeconds) +
                " node_lookup_sec=" + std::to_string(stats.nodeLookupSeconds) +
                " node_insert_sec=" + std::to_string(stats.nodeInsertSeconds) +
                " state_hash_calls=" + std::to_string(stats.stateHashCalls) +
                " choose_col_calls=" +
                std::to_string(stats.chooseColumnCalls) +
                " probe_calls=" +
                std::to_string(stats.decompositionProbeCalls) +
                " cache_key_columns=" +
                std::to_string(stats.cacheKeyColumns) +
                " cover_sec=" + std::to_string(stats.coverSeconds) +
                " uncover_sec=" + std::to_string(stats.uncoverSeconds) +
                " cover_set_sec=" + std::to_string(stats.coverSetSeconds) +
                " uncover_set_sec=" + std::to_string(stats.uncoverSetSeconds) +
                " removed_rows_clear_sec=" +
                std::to_string(stats.removedRowsClearSeconds) +
                " cover_calls=" + std::to_string(stats.coverCalls) +
                " uncover_calls=" + std::to_string(stats.uncoverCalls) +
                " cover_set_ops=" + std::to_string(stats.coverSetOps) +
                " uncover_set_ops=" + std::to_string(stats.uncoverSetOps) +
                " removed_rows_clear_calls=" +
                std::to_string(stats.removedRowsClearCalls) +
                " lock_wait_sec=" + std::to_string(stats.lockWaitSeconds));
        }

        std::map<int, ThreadRunStats> perThread;
        for (const ThreadRunStats& stats : taskStats) {
            ThreadRunStats& aggregate = perThread[stats.ompThread];
            aggregate.ompThread = stats.ompThread;
            aggregate.rows += stats.rows;
            aggregate.cols += stats.cols;
            aggregate.solveSeconds += stats.solveSeconds;
            aggregate.cacheReads += stats.cacheReads;
            aggregate.cacheHits += stats.cacheHits;
            aggregate.cacheWrites += stats.cacheWrites;
            aggregate.nodeLookups += stats.nodeLookups;
            aggregate.nodeLookupHits += stats.nodeLookupHits;
            aggregate.nodeInserts += stats.nodeInserts;
            aggregate.nodesAllocated += stats.nodesAllocated;
            aggregate.cacheReadSeconds += stats.cacheReadSeconds;
            aggregate.cacheWriteSeconds += stats.cacheWriteSeconds;
            aggregate.stateHashSeconds += stats.stateHashSeconds;
            aggregate.chooseColumnSeconds += stats.chooseColumnSeconds;
            aggregate.decompositionProbeSeconds +=
                stats.decompositionProbeSeconds;
            aggregate.nodeLookupSeconds += stats.nodeLookupSeconds;
            aggregate.nodeInsertSeconds += stats.nodeInsertSeconds;
            aggregate.stateHashCalls += stats.stateHashCalls;
            aggregate.chooseColumnCalls += stats.chooseColumnCalls;
            aggregate.decompositionProbeCalls +=
                stats.decompositionProbeCalls;
            aggregate.cacheKeyColumns += stats.cacheKeyColumns;
            aggregate.lockWaitSeconds += stats.lockWaitSeconds;
            aggregate.coverSeconds += stats.coverSeconds;
            aggregate.uncoverSeconds += stats.uncoverSeconds;
            aggregate.coverSetSeconds += stats.coverSetSeconds;
            aggregate.uncoverSetSeconds += stats.uncoverSetSeconds;
            aggregate.removedRowsClearSeconds +=
                stats.removedRowsClearSeconds;
            aggregate.coverCalls += stats.coverCalls;
            aggregate.uncoverCalls += stats.uncoverCalls;
            aggregate.coverSetOps += stats.coverSetOps;
            aggregate.uncoverSetOps += stats.uncoverSetOps;
            aggregate.removedRowsClearCalls +=
                stats.removedRowsClearCalls;
        }
        for (const auto& [threadId, stats] : perThread) {
            const double hitRate =
                stats.cacheReads == 0
                    ? 0.0
                    : static_cast<double>(stats.cacheHits) / stats.cacheReads;
            logger.logLine(
                "[thread-summary] omp_thread=" + std::to_string(threadId) +
                " solve_sec=" + std::to_string(stats.solveSeconds) +
                " cache_hit_rate=" + std::to_string(hitRate) +
                " cache_read_sec=" + std::to_string(stats.cacheReadSeconds) +
                " cache_write_sec=" + std::to_string(stats.cacheWriteSeconds) +
                " state_hash_sec=" + std::to_string(stats.stateHashSeconds) +
                " choose_col_sec=" +
                std::to_string(stats.chooseColumnSeconds) +
                " probe_sec=" +
                std::to_string(stats.decompositionProbeSeconds) +
                " node_lookup_sec=" + std::to_string(stats.nodeLookupSeconds) +
                " node_insert_sec=" + std::to_string(stats.nodeInsertSeconds) +
                " cover_sec=" + std::to_string(stats.coverSeconds) +
                " uncover_sec=" + std::to_string(stats.uncoverSeconds) +
                " cover_set_sec=" + std::to_string(stats.coverSetSeconds) +
                " uncover_set_sec=" + std::to_string(stats.uncoverSetSeconds) +
                " removed_rows_clear_sec=" +
                std::to_string(stats.removedRowsClearSeconds) +
                " cover_set_ops=" + std::to_string(stats.coverSetOps) +
                " uncover_set_ops=" + std::to_string(stats.uncoverSetOps) +
                " removed_rows_clear_calls=" +
                std::to_string(stats.removedRowsClearCalls) +
                " lock_wait_sec=" + std::to_string(stats.lockWaitSeconds));
        }
    }

    if (has_timeout.load()) {
        throw std::runtime_error("Time bound broken");
    }

    if (has_failure.load()) {
        return {DNNFResult(0), F};
    }
    
    // 计算总计数
    DNNFResult totalResult(1);
    vector<shared_ptr<DNNFNode>> subNodes;
    subNodes.reserve(n);
    for (int i = 0; i < n; ++i) {
        totalResult = totalResult * results[i];
        if (nodes[i]) subNodes.push_back(nodes[i]);
    }
    
    shared_ptr<DNNFNode> decompNode = buildDecomposableNode(subNodes);
 
    return {totalResult, decompNode};
}


// DXD DynDXD
std::pair<DNNFResult, shared_ptr<DNNFNode>> DanceDNNF::DXD(Block& block, int depth) {
    
    if(timer.timeBoundBroken()) {
        throw std::runtime_error("Time bound broken");
    }

    if (isDXZMode() ? isSolved() : block.cols.empty()) {
        return {DNNFResult(1), T};
    }

    vector<int> stateCols;
    size_t stateHash = isDXZMode()
        ? (encodeColState(&stateCols) ^ size_t{0xd6e8feb86659fd93ULL})
        : hashBlockState(block.cols, &stateCols);

    CacheKey state{stateHash, std::move(stateCols), isDXZMode()};

    DNNFResult cachedCount(0);
    shared_ptr<DNNFNode> cachedNode;
    if (getCachedResult(state, cachedCount, cachedNode)) {
        return {cachedCount, cachedNode};
    }

    ColumnHeader* choose =
        isDXZMode() ? selectCol() : selectOptimalColumn(block.cols);
    if (!choose || choose->size <= 0) {
        if (!isDXZMode() && isCCETTCallBudgetExhausted()) {
            stopCCForETTCallBudget();
        }
        setCachedResult(state, DNNFResult(0), F);
        return {DNNFResult(0), F};
    }


    // 是否分解查询
    bool shouldProbe = false;
    // 是否使用动态分解
    bool useDynamicEtt = false;
    // dyndxd：初始查询
    const bool initialProbe = depth == 1 && isDynDXD();

    if (isDXDMode()) {
        shouldProbe = true;
    } else if (isDynDXD()) {
        auto& policy = currentDecompositionPolicy();
        if (initialProbe) {
            // Build all root blocks with the matrix-native BFS. ETT storage is
            // created lazily only for the one block selected for probing.
            shouldProbe = true;
        } else if (policy.phase == CCProbePhase::Ett &&
                   isCCETTCallBudgetExhausted()) {
            stopCCForETTCallBudget();
        } else if (policy.phase == CCProbePhase::Bfs &&
                   isCCBFSCallBudgetExhausted()) {
            stopCCForBFSCallBudget();
        }

        if (!initialProbe && currentDecompositionPolicy().phase !=
                                CCProbePhase::Disabled) {
            auto& activePolicy = currentDecompositionPolicy();
            if (activePolicy.skipNextProbe) {
                activePolicy.skipNextProbe = false;
            } else {
                if (activePolicy.phase == CCProbePhase::Ett &&
                    !usesDynamicConnectivity()) {
                    activePolicy.phase =
                        activePolicy.ettFailureAction ==
                                EttFailureAction::FallbackToBfs
                            ? CCProbePhase::Bfs
                            : CCProbePhase::Disabled;
                }
                shouldProbe =
                    activePolicy.phase != CCProbePhase::Disabled;
                useDynamicEtt = activePolicy.phase == CCProbePhase::Ett;
            }
        }

        if (pureDynDXD && !initialProbe) {
            auto& purePolicy = currentDecompositionPolicy();
            purePolicy.phase = CCProbePhase::Ett;
            purePolicy.ettFailureAction = EttFailureAction::Disable;
            purePolicy.noSplitCount = 0;
            shouldProbe = true;
            useDynamicEtt = true;
        }
    }

    if (useDynamicEtt && !tryReserveETTCall()) {
        stopCCForETTCallBudget();
        useDynamicEtt = false;
    }

    if (shouldProbe && !useDynamicEtt && isDynDXD()) {
        if (!tryReserveBFSCall()) {
            stopCCForBFSCallBudget();
            shouldProbe = false;
        }
    }

    if (shouldProbe) {
        vector<Block> curBlock;
        BFSScanMetrics bfsMetrics;
        if (useDynamicEtt) {
            // DynDXD
            curBlock = getComponentsByETT(block.cols);
        } else {
            // DXD BFS 搜索并记录 CPU 时间
            const std::clock_t bfsStart =
                isDXDMode() && (collectCCTime || collectCCExperimentStats)
                    ? std::clock()
                    : std::clock_t{0};
            curBlock = getComponentsByBFS(
                block.cols,
                collectCCExperimentStats && isDXDMode() ? &bfsMetrics
                                                        : nullptr);
            if (isDXDMode() &&
                (collectCCTime || collectCCExperimentStats)) {
                addCCCpuTicks(bfsStart, ccBfsCpuTicks);
            }
        }

        if (collectCCExperimentStats) {
            std::lock_guard<std::mutex> lock(ccExperimentStatsMutex);
            if (isDXDMode()) {
                ccExperimentStats.bfsVertexSum += bfsMetrics.vertices;
                ccExperimentStats.bfsEdgeSum += bfsMetrics.graphEdges;
            }
        }

        if (useDynamicEtt && isCCETTCallBudgetExhausted()) {
            stopCCForETTCallBudget();
        } else if (!useDynamicEtt && isDynDXD() &&
                   currentDecompositionPolicy().phase == CCProbePhase::Bfs &&
                   isCCBFSCallBudgetExhausted()) {
            stopCCForBFSCallBudget();
        }

        updateMaxBlockCount(curBlock.size());

        if (!initialProbe && isDynDXD()) {
            updateDecompositionPolicy(currentDecompositionPolicy(),
                                      curBlock.size());
        }

        if (int(curBlock.size()) > 1) {
            if (pureDynDXD && initialProbe) {
                // Root discovery remains BFS, but every resulting block keeps
                // its ETT forest for the rest of the search.
                prepareDynamicCCForBlock(block);
            }

            vector<DecompositionPolicy> policies(
                curBlock.size(), DecompositionPolicy{});
            if (initialProbe) {
                // 初始查询出现多分块
                const size_t selected =
                    selectInitialProbeBlock(curBlock, blockArea(InitBlock),
                                            bfs_area_threshold);
                if (pureDynDXD) {
                    for (auto& policy : policies) {
                        policy = DecompositionPolicy{
                            CCProbePhase::Ett,
                            EttFailureAction::Disable, 0, false};
                    }
                } else if (currentDecompositionPolicy().phase ==
                           CCProbePhase::Ett) {
                    policies[selected] = DecompositionPolicy{
                        CCProbePhase::Ett, EttFailureAction::Disable, 0, true};
                    prepareDynamicCCForBlock(curBlock[selected]);
                }
            } else if (pureDynDXD) {
                for (auto& policy : policies) {
                    policy = DecompositionPolicy{
                        CCProbePhase::Ett,
                        EttFailureAction::Disable, 0, false};
                }
            }

            const bool needsDynamicTrees =
                isDynDXD() && std::any_of(
                             policies.begin(), policies.end(),
                             [](const DecompositionPolicy& policy) {
                                 return policy.phase == CCProbePhase::Ett;
                             });

            const bool useParallelDecomposition =
                isParallelSearch && !omp_in_parallel();
            auto decompResult = useParallelDecomposition
                ? parallelSearchUseOmp(curBlock, depth, needsDynamicTrees,
                                       std::move(policies))
                : serialSearch(curBlock, depth, std::move(policies));

            auto [result, decompNode] = decompResult;

            setCachedResult(state, result, decompNode);
            return {result, decompNode};
        }

        if (initialProbe && !curBlock.empty()) {
            if (pureDynDXD) {
                prepareDynamicCCForBlock(block);
            } else if (currentDecompositionPolicy().phase ==
                           CCProbePhase::Ett &&
                       !isCCETTCallBudgetExhausted()) {
                prepareDynamicCCForBlock(curBlock.front());
            }
        }

    }

    DNNFResult totalResult(0);
    shared_ptr<DNNFNode> x = F;

    const bool maintainRows = isDXZMode() || isDXDMode() ||
        currentDecompositionPolicy().phase != CCProbePhase::Disabled;

    set<int> deleted_rows;
    coverInBlock(choose->col, block, deleted_rows, maintainRows);

    const bool dynamicUpdated = shouldMaintainDynamicEtt();

    if (dynamicUpdated) DecUpdateCC(deleted_rows);
    if (isCCETTCallBudgetExhausted()) stopCCForETTCallBudget();

    Node* curC = choose->down;
    bool rowCovered = false;
    bool rowEttUpdated = false;
    set<int> deleted_rows_;

    auto restoreCurrentFrame = [&]() {
        if (rowCovered) {
            Node* curR = curC->left;
            while (curR != curC) {
                uncoverInBlock(curR->col, block, maintainRows);
                curR = curR->left;
            }
            if (rowEttUpdated) IncUpdateCC(deleted_rows_);
            rowEttUpdated = false;
            rowCovered = false;
        }
        uncoverInBlock(choose->col, block, maintainRows);
        if (dynamicUpdated) IncUpdateCC(deleted_rows);
    };

    try {
        while(curC != choose) {

        Node* curR = curC->right;
        deleted_rows_.clear();


        while (curR != curC) {
            coverInBlock(curR->col, block, deleted_rows_, maintainRows);
            curR = curR->right;
        }
        rowEttUpdated = shouldMaintainDynamicEtt();
        if (rowEttUpdated) DecUpdateCC(deleted_rows_);
        rowCovered = true;
 
        auto [result, sub_node] = DXD(block, depth + 1);

        if(!result.isZero()) {
            x = buildDecisionNode(curC->row, x, sub_node);
            totalResult = totalResult + result;
        }
        
        curR = curC->left;
        while (curR != curC) {
            uncoverInBlock(curR->col, block, maintainRows);
            curR = curR->left;
        }
        if (rowEttUpdated) IncUpdateCC(deleted_rows_);
        rowEttUpdated = false;
        rowCovered = false;

        curC = curC->down;
        }
    } catch (...) {
        restoreCurrentFrame();
        throw;
    }
    uncoverInBlock(choose->col, block, maintainRows);
    if (dynamicUpdated) IncUpdateCC(deleted_rows);

    setCachedResult(state, totalResult, x);
    return {totalResult, x};
}

size_t DanceDNNF::countZDDSize() const {
 
    if (!rootDNNF) {
        return 0;
    }

    std::unordered_set<size_t> visited;
 
    std::function<size_t(const shared_ptr<DNNFNode>&)> traverse =
        [&](const shared_ptr<DNNFNode>& node) -> size_t {
            if (!node)                       return 0;
            if (node->isTerminal())          return 0;   // T / F don't count
            if (visited.count(node->id))     return 0;
            visited.insert(node->id);
 
            size_t total = 1;   // this node
            total += traverse(node->left);
            total += traverse(node->right);
            return total;
        };
 
    return traverse(rootDNNF); // 加上T和F节点
}

void DanceDNNF::logCCExperimentStats(bool complete, bool quick) {
    if (!collectCCExperimentStats) return;
    (void)quick;

    CCExperimentStats stats;
    {
        std::lock_guard<std::mutex> lock(ccExperimentStatsMutex);
        stats = ccExperimentStats;
    }

    logger.logLine("CC Stats Complete: " + std::to_string(complete ? 1 : 0));
    if (isDXDMode()) {
        logger.logLine("CC Stats DXD Vertex Sum: " +
                       std::to_string(stats.bfsVertexSum));
        logger.logLine("CC Stats DXD Edge Sum: " +
                       std::to_string(stats.bfsEdgeSum));
        logger.logLine("CC Stats DXD BFS CPU: " +
                       std::to_string(ticksToSeconds(ccBfsCpuTicks)));
        return;
    }

    const double avgVertices = stats.decCalls == 0
        ? 0.0
        : static_cast<double>(stats.activeVertexSum) / stats.decCalls;
    const double avgReplacementChecks = stats.decCalls == 0
        ? 0.0
        : static_cast<double>(stats.replacementCheckSum) / stats.decCalls;
    logger.logLine("CC Stats Dyn Dec Calls: " +
                   std::to_string(stats.decCalls));
    logger.logLine("CC Stats Dyn Deleted Tree Edges: " +
                   std::to_string(stats.deletedTreeEdgeSum));
    logger.logLine("CC Stats Dyn Deleted Vertices: " +
                   std::to_string(stats.deletedVertexSum));
    logger.logLine("CC Stats Dyn Avg Vertices: " +
                   std::to_string(avgVertices));
    logger.logLine("CC Stats Dyn Avg Replacement Checks: " +
                   std::to_string(avgReplacementChecks));
    logger.logLine("CC Stats Dyn Dec CPU: " +
                   std::to_string(ticksToSeconds(ccDecrementCpuTicks)));
    logger.logLine("CC Stats Dyn Inc CPU: " +
                   std::to_string(ticksToSeconds(ccIncrementCpuTicks)));
}

void DanceDNNF::logSearchStateStats(const string& label,
                                    const SearchState& state) {
    if (!state.collectStats) return;

    const double hitRate =
        state.cacheReads == 0
            ? 0.0
            : static_cast<double>(state.cacheHits) / state.cacheReads;
    const double averageKeyColumns =
        state.cacheWrites == 0
            ? 0.0
            : static_cast<double>(state.cacheKeyColumns) / state.cacheWrites;

    logger.logLine(
        "[search-summary] label=" + label +
        " cache_reads=" + std::to_string(state.cacheReads) +
        " cache_hits=" + std::to_string(state.cacheHits) +
        " cache_hit_rate=" + std::to_string(hitRate) +
        " cache_writes=" + std::to_string(state.cacheWrites) +
        " cache_key_columns=" + std::to_string(state.cacheKeyColumns) +
        " avg_cache_key_columns=" + std::to_string(averageKeyColumns) +
        " state_hash_calls=" + std::to_string(state.stateHashCalls) +
        " choose_col_calls=" + std::to_string(state.chooseColumnCalls) +
        " probe_calls=" + std::to_string(state.decompositionProbeCalls) +
        " nodes_allocated=" + std::to_string(state.nodesAllocated) +
        " state_hash_sec=" + std::to_string(state.stateHashSeconds) +
        " choose_col_sec=" + std::to_string(state.chooseColumnSeconds) +
        " probe_sec=" + std::to_string(state.decompositionProbeSeconds) +
        " cache_read_sec=" + std::to_string(state.cacheReadSeconds) +
        " cache_write_sec=" + std::to_string(state.cacheWriteSeconds) +
        " node_lookup_sec=" + std::to_string(state.nodeLookupSeconds) +
        " node_insert_sec=" + std::to_string(state.nodeInsertSeconds) +
        " cover_sec=" + std::to_string(state.mutation.coverSeconds) +
        " uncover_sec=" + std::to_string(state.mutation.uncoverSeconds) +
        " cover_set_sec=" + std::to_string(state.mutation.coverSetSeconds) +
        " uncover_set_sec=" + std::to_string(state.mutation.uncoverSetSeconds) +
        " removed_rows_clear_sec=" +
        std::to_string(state.mutation.removedRowsClearSeconds));
}

// DXD DXZ入口
void DanceDNNF::startDXD() {
    if (!isDXZMode() && !isDXDMode()) {
        throw std::logic_error(
            "startDXD requires AlgorithmMode::DXZ or AlgorithmMode::DXD");
    }

    if(!controlOUTPUT)  logger.logLine("开始DXD搜索...");

    MAX_B_COUNT.store(1, std::memory_order_relaxed);
    createdDNNFNodes.store(0, std::memory_order_relaxed);
    ccBfsCpuTicks.store(0, std::memory_order_relaxed);
    ccDecrementCpuTicks.store(0, std::memory_order_relaxed);
    ccIncrementCpuTicks.store(0, std::memory_order_relaxed);
    if (collectCCExperimentStats) ccExperimentStats.reset();
    resetAdaptiveDecompositionState();

    activeSearchState = nullptr;
    mainSearchState.reset(debug);
    try{

        timer.reset();
        timer.markStartTime();
        auto start = std::chrono::high_resolution_clock::now();

        auto [ResSols, compiledRoot] = DXD(InitBlock, 1);
        rootDNNF = compiledRoot;

        auto end = std::chrono::high_resolution_clock::now();
        timer.markStopTime();

        searchTime = std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count();
        logger.logLine("Time: " + std::to_string(searchTime) + " s");
        if (debug) {
            logSearchStateStats(
                isDXZMode() ? "dxz" : "dxd", mainSearchState);
        }
        if (!isDXZMode() && collectCCTime) {
            logger.logLine("DXD CC BFS CPU: " + std::to_string(ticksToSeconds(ccBfsCpuTicks)) + " s");
        }
        if (!isDXZMode()) {
            logCCExperimentStats(true);
        }
        timeout = false;

        solutionCount = ResSols.toString();

        if (solutionCount.size() > 12) {
            logger.logLine("Solutions (scientific): " + ResSols.toScientificString(3));
        } else {
            logger.logLine("Solutions: " + solutionCount);
        }

        logger.logLine("Max Blocks: " +
                       std::to_string(MAX_B_COUNT.load(std::memory_order_relaxed)));



        if (isDXZMode()) {
            size_t zdd_size = countZDDSize();
            logger.logLine("ZDD Size: " + std::to_string(zdd_size));
        } else if (!skipDNNFSize) {
            logger.logLine("DNNF Size: " +
                           std::to_string(countDNNFNodes()));
        }


        return;
    } catch (std::runtime_error &e) {
        const std::string message = e.what();
        if (message.find("Time bound") == std::string::npos) {
            logger.logLine("DXD内部错误: " + message);
            throw;
        }
        timeout = true;
        if (isDXZMode()) {
            logger.logLine("DXZ搜索超时: " + message);
        } else {
            if (collectCCTime) {
                logger.logLine(
                    "DXD CC BFS CPU: " +
                    std::to_string(ticksToSeconds(ccBfsCpuTicks)) + " s");
            }
            logCCExperimentStats(false);
            logger.logLine("DXD搜索超时: " + message);
        }
        return;
    }

}

// DynDXD
void DanceDNNF::startMultiThreadDXD() {
    if (!isDynDXD()) {
        throw std::logic_error(
            "startMultiThreadDXD requires AlgorithmMode::DYNDXD");
    }

    // logger.logLine("开始多线程DXD搜索...");
    
    // isParallelSearch = true;
    MAX_B_COUNT.store(1, std::memory_order_relaxed);
    createdDNNFNodes.store(0, std::memory_order_relaxed);
    ccBfsCpuTicks.store(0, std::memory_order_relaxed);
    ccDecrementCpuTicks.store(0, std::memory_order_relaxed);
    ccIncrementCpuTicks.store(0, std::memory_order_relaxed);
    if (collectCCExperimentStats) ccExperimentStats.reset();
    resetAdaptiveDecompositionState();

    activeSearchState = nullptr;
    mainSearchState.reset(debug);

    try {

        timer.reset();
        timer.markStartTime();
        auto start = std::chrono::high_resolution_clock::now();

        auto [ResSols, compiledRoot] = DXD(InitBlock, 1);  // 多线程DXD搜索
        rootDNNF = compiledRoot; 

        if (quickStatsUpdateLimit > 0) {
            timer.markStopTime();
            logCCExperimentStats(true, true);
            timeout = false;
            return;
        }

        auto end = std::chrono::high_resolution_clock::now();
        timer.markStopTime();
   
        searchTime = std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count();
        logger.logLine("Time: " + std::to_string(searchTime) + " s");
        if (debug) {
            logSearchStateStats("dyndxd-main", mainSearchState);
        }
        if (collectCCTime) {
            logger.logLine("Dyn CC Decrement CPU: " +
                           std::to_string(ticksToSeconds(ccDecrementCpuTicks)) + " s");
        }
        logCCExperimentStats(true);
        timeout = false;

        solutionCount = ResSols.toString();
        if (solutionCount.size() > 12) {
            logger.logLine("Solutions (scientific): " + ResSols.toScientificString(3));
        } else {
            logger.logLine("Solutions: " + solutionCount);
        }
    
        logger.logLine("Max Blocks: " +
                       std::to_string(MAX_B_COUNT.load(std::memory_order_relaxed)));
        logger.logLine(
            "CC ETT Calls: " +
            std::to_string(ccEttCallsUsed.load(std::memory_order_relaxed)));
        logger.logLine(
            "CC BFS Calls: " +
            std::to_string(ccBfsCallsUsed.load(std::memory_order_relaxed)));
            
        if (!skipDNNFSize) {
            logger.logLine("DNNF Size: " +
                           std::to_string(countDNNFNodes()));
        }

        return;
    } catch (const QuickStatsComplete&) {
        timer.markStopTime();
        logCCExperimentStats(true, true);
        timeout = false;
        return;
    } catch (std::runtime_error &e) {
        const std::string message = e.what();
        if (message.find("Time bound") == std::string::npos) {
            timer.markStopTime();
            logger.logLine("DynDXD内部错误: " + message);
            throw;
        }
        timeout = true;
        timer.markStopTime();
        const double elapsed = timer.getElapsedTime();
        logger.logLine("Time: " + std::to_string(elapsed) + " s");
        if (collectCCTime) {
            logger.logLine("Dyn CC Decrement CPU: " +
                           std::to_string(ticksToSeconds(ccDecrementCpuTicks)) + " s");
        }
        logCCExperimentStats(false);
        logger.logLine("DynDXD搜索超时: " + message);
        return;
    }
}

DNNFResult DanceDNNF::parallelSearchMDLX(vector<Block>& blocks, DecomMode mode) {
    const int n = blocks.size();
    
    // 并行处理每个子块
    std::vector<DNNFResult> results(n);
    std::atomic<bool> has_timeout(false);
    std::atomic<bool> has_failure(false);
    
    std::vector<std::unique_ptr<splaytree::EulerTourTree>> extracted(n);
    std::vector<std::unique_ptr<splaytree::EulerTourTree>> returned(n);
    
    if (mode == DecomMode::Dynamic) {
        auto& comps = getComponents();
        for (int i = 0; i < n; ++i)
            extracted[i] = std::move(comps[i]);
        comps.clear();
    }

    #pragma omp parallel for
    for (int i = 0; i < n; i++) {
        // 提前检查超时标志
        if (has_timeout.load(std::memory_order_acquire) ||
             has_failure.load(std::memory_order_acquire)) {
            if (mode == DecomMode::Dynamic)
                returned[i] = std::move(extracted[i]);
            continue;
        }
        
        std::unique_ptr<ThreadLocalState> outerTls;
        if (mode == DecomMode::Dynamic) {
            outerTls = std::move(tlsState);
        }

        try {
            if (mode == DecomMode::Dynamic)
                initThreadLocalState(blocks[i], std::move(extracted[i]));
            vector<int> threadSols;
            
            // 递归调用MDLX（每个线程独立计数）
            auto result = MDLX(threadSols, blocks[i], mode);

            if (result.isZero()) {
                has_failure.store(true, std::memory_order_release);
            } else {
                results[i] = result;
            }

            if (mode == DecomMode::Dynamic && tlsState && !tlsState->components.empty())
                returned[i] = std::move(tlsState->components.front());
            
        } catch (const std::runtime_error& e) {
            // 捕获超时异常
            if (std::string(e.what()).find("Time out") != std::string::npos) {
                has_timeout.store(true, std::memory_order_release);
            } else {
                has_failure.store(true, std::memory_order_release);
            }
        } catch (...) {
            has_failure.store(true, std::memory_order_release);
            if (mode == DecomMode::Dynamic && tlsState && !tlsState->components.empty())
                returned[i] = std::move(tlsState->components.front());
        }

        if (mode == DecomMode::Dynamic) {
            cleanupThreadLocalState();
            tlsState = std::move(outerTls);
        }
    }

    if (mode == DecomMode::Dynamic) {
        auto& comps = getComponents();
        comps.resize(n);
        for (int i = 0; i < n; ++i)
            comps[i] = std::move(returned[i]);
    }
    
    if (has_timeout.load()) throw runtime_error("Time out");
    if (has_failure.load()) return DNNFResult(0);
    
    // 合并结果：计算所有子块解的笛卡尔积
    DNNFResult totalCount(1);
    for (const auto& result : results) {
        totalCount = totalCount * result;
    }
    
    return totalCount;
}

DNNFResult DanceDNNF::MDLX(vector<int>& sols, Block& block, DecomMode mode) {

    if (timer.timeBoundBroken()) {
        throw std::runtime_error("Time out");
    }

    if( block.cols.empty() ) {
        return DNNFResult(1);
    }

    bool try_decompose = (mode == DecomMode::Dynamic)
                        ? (isGraphSyncEnabled() && block.rows.size() >= 2)
                        : (block.rows.size() >= 2);
    
    if (try_decompose) {

        vector<Block> curBlock = mode == DecomMode::Dynamic ? getComponentsByETT(block.cols) : getComponentsByBFS(block.cols);

        updateMaxBlockCount(curBlock.size());

        if (curBlock.size() > 1) {
            
            if (mode == DecomMode::Dynamic) {
                turnOffGraphSync(); // 关闭图同步，提升性能
            }

            // 多线程搜索
            return parallelSearchMDLX(curBlock, mode);
        } 
    }

    ColumnHeader* choose = selectOptimalColumn(block.cols);
    if( !choose || choose->size <= 0 ) {
        return DNNFResult(0);  
    }

    DNNFResult totalResult = DNNFResult(0);

    set<int> deleted_rows;
    coverInBlock(choose->col, block, deleted_rows, true);
    if (mode == DecomMode::Dynamic) {
        DecUpdateCC(deleted_rows);
    }
    Node* curC = choose->down;  

    while( curC != choose )  
    {  
         
        Node* curR = curC->right; 
        set<int> deleted_rows_; 
        while( curR != curC )  
        {  
            coverInBlock(curR->col, block, deleted_rows_, true);
            curR = curR->right;  
        }  
        if (mode == DecomMode::Dynamic) {
            DecUpdateCC(deleted_rows_);
        }

        sols.push_back(curC->row + 1); 
        // 递归搜索
        auto result = MDLX(sols, block, mode);
        if (!result.isZero()) {
            totalResult = totalResult + result;
        }
       
        sols.pop_back();  // 回溯，移除当前行
        curR = curC->left;  
        while( curR != curC )  
        {  
            uncoverInBlock(curR->col, block, true);
            curR = curR->left;  
        }  
        if (mode == DecomMode::Dynamic) {
            IncUpdateCC(deleted_rows_);
        }
        curC = curC->down;  
    }  
    uncoverInBlock(choose->col, block, true);
    if (mode == DecomMode::Dynamic) {
        IncUpdateCC(deleted_rows);
    }
    return totalResult; 
}

void DanceDNNF::start_MDLX(DecomMode mode) {
    const bool modeMatches =
        (mode == DecomMode::Dynamic &&
         algorithmMode == AlgorithmMode::MDLX_DYNAMIC) ||
        (mode == DecomMode::Static &&
         algorithmMode == AlgorithmMode::MDLX_STATIC);
    if (!modeMatches) {
        throw std::logic_error(
            "MDLX decomposition mode does not match AlgorithmMode");
    }

    // logger.logLine("开始多线程DLX搜索...");

    const string modeStr = (mode == DecomMode::Dynamic) ? "ETT多线程DLX" : "BFS多线程DLX";
    logger.logLine("开始" + modeStr + "搜索...");
 
    p_count = 0;
    MAX_B_COUNT.store(1, std::memory_order_relaxed);
 
    // BFS 不使用 graph-sync 机制，提前关闭避免无效检查
    if (mode == DecomMode::Static)
        turnOffGraphSync();
    
    try {
        vector<int> sols;

        timer.reset();
        timer.markStartTime();
        auto start = std::chrono::high_resolution_clock::now();
        auto res = MDLX(sols, InitBlock, mode);
        auto end = std::chrono::high_resolution_clock::now();
        timer.markStopTime();

        searchTime = std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count();
        logger.logLine("Time: " + std::to_string(searchTime) + " s");
        timeout = false;

        solutionCount = res.toString();
        logger.logLine("Solutions: " + solutionCount);
        if (solutionCount.size() > 12) logger.logLine("Solutions (scientific): " + res.toScientificString(3));

        logger.logLine("Max Blocks: " +
                       std::to_string(MAX_B_COUNT.load(std::memory_order_relaxed)));
        return;
    } catch (std::runtime_error &e) {
        timeout = true;
        logger.logLine("MDLX搜索超时: " + std::string(e.what()));
        return;
    } 
}

// ═════════════════════════════════════════════════════════════════════════════
// Solution enumeration
// ═════════════════════════════════════════════════════════════════════════════

static void enumerateDFS(
    const shared_ptr<DNNFNode>&                          node,
    vector<int>&                                         hi_path,
    size_t&                                              count,
    size_t                                               max_sols,
    const function<void(const vector<int>&, size_t)>&    on_solution,
    unordered_map<size_t, vector<vector<int>>>&          memo)
{
    if (!node) return;
    if (max_sols > 0 && count >= max_sols) return;
 
    // ── Terminal ──────────────────────────────────────────────────────────────
    if (node->isTerminal()) {
        if (node->label == -1) {          // T: emit solution
            vector<int> sol = hi_path;
            sort(sol.begin(), sol.end());
            on_solution(sol, ++count);
        }
        // F: discard
        return;
    }
 
    // ── Decision(label, lo, hi) ───────────────────────────────────────────────
    if (node->type == NodeType::Decision) {
        // lo-branch: row not selected
        enumerateDFS(node->left,  hi_path, count, max_sols, on_solution, memo);
        // hi-branch: row selected → push label onto path
        if (node->right && !(max_sols > 0 && count >= max_sols)) {
            hi_path.push_back(node->label);
            enumerateDFS(node->right, hi_path, count, max_sols, on_solution, memo);
            hi_path.pop_back();
        }
        return;
    }
 
    // ── Decomposed-AND ────────────────────────────────────────────────────────
    if (node->type == NodeType::Decomposed) {
        if (node->children.empty()) {
            // Degenerate empty AND: treat as T
            vector<int> sol = hi_path;
            sort(sol.begin(), sol.end());
            on_solution(sol, ++count);
            return;
        }
 
        auto getChildSols =
            [&](const shared_ptr<DNNFNode>& child) -> const vector<vector<int>>& {
            auto it = memo.find(child->id);
            if (it != memo.end()) return it->second;
 
            vector<vector<int>> slist;
            size_t   dummy = 0;
            vector<int> empty;
            enumerateDFS(child, empty, dummy, /*max_sols=*/0,
                [&](const vector<int>& s, size_t) { slist.push_back(s); },
                memo);
            memo.emplace(child->id, move(slist));
            return memo.at(child->id);
        };
 
        // Cartesian product: combined[k] = merged solution from children 0..k-1
        vector<vector<int>> combined = {{}};   // seed with one empty partial solution
 
        for (const auto& child : node->children) {
            if (max_sols > 0 && count >= max_sols) return;
            const auto& csols = getChildSols(child);
            if (csols.empty()) return;   // this child has no solutions → AND fails
 
            vector<vector<int>> next;
            next.reserve(combined.size() * csols.size());
            for (const auto& existing : combined)
                for (const auto& csol : csols) {
                    next.emplace_back(existing);
                    next.back().insert(next.back().end(), csol.begin(), csol.end());
                }
            combined = move(next);
        }
 
        // Emit each Cartesian combination prefixed with ancestor hi_path
        for (const auto& combo : combined) {
            if (max_sols > 0 && count >= max_sols) return;
            vector<int> sol = hi_path;
            sol.insert(sol.end(), combo.begin(), combo.end());
            sort(sol.begin(), sol.end());
            on_solution(sol, ++count);
        }
        return;
    }
    // Other node types (OR, Variable) not yet used → fall through silently
}
 
// ─────────────────────────────────────────────────────────────────────────────
// DanceDNNF::printAllSolutions
// ─────────────────────────────────────────────────────────────────────────────
void DanceDNNF::printAllSolutions(ostream& out, size_t max_sols) const {
 
    if (!rootDNNF) {
        out << "[printAllSolutions] rootDNNF is not set."
               " Run startDXD() or startMultiThreadDXD() first.\n";
        return;
    }
 
    out << "══════════════════════════════════════════\n"
           " Exact-cover solutions (Decision-DNNF)\n"
           "══════════════════════════════════════════\n";
    if (max_sols > 0)
        out << " (showing first " << max_sols << " solution(s))\n";
    out << '\n';
 
    size_t count = 0;
    vector<int> hi_path;
    // memo caches Decomposed-node solution lists; a Decomposed node that is
    // shared by multiple Decision nodes is enumerated only once.
    unordered_map<size_t, vector<vector<int>>> memo;
 
    enumerateDFS(
        rootDNNF, hi_path, count, max_sols,
        [&](const vector<int>& rows, size_t idx) {
            // rows is already sorted; row ids are 0-based internally → print 1-based
            out << "  [" << idx << "] { ";
            for (int r : rows) out << (r + 1) << ' ';
            out << "}\n";
        },
        memo);
 
    out << "\n──────────────────────────────────────────\n";
    if (max_sols > 0 && count >= max_sols)
        out << "  Reached limit=" << max_sols
            << ".  Printed " << count << " solution(s); more may exist.\n";
    else
        out << "  Total: " << count << " solution(s).\n";
    out << "══════════════════════════════════════════\n";
}
