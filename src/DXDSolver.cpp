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

    {
        std::shared_lock<std::shared_mutex> readLock(tableMutex);
        auto it = node_table.find(key);
        if (it != node_table.end()) {
            if (auto cached = it->second.lock()) {
                return cached;
            }
        }
    }

    std::unique_lock<std::shared_mutex> writeLock(tableMutex);
    auto it = node_table.find(key);
    if (it != node_table.end()) {
        if (auto cached = it->second.lock()) {
            return cached;
        }
        node_table.erase(it);
    }

    auto decision_node = make_shared<DNNFNode>(NodeType::Decision, lo, hi);
    decision_node->label = r;
    node_table[key] = decision_node;

    return decision_node;
}

shared_ptr<DNNFNode> DanceDNNF::buildDecomposableNode(vector<shared_ptr<DNNFNode>>& subDNNFs) {
    auto decompose_node = make_shared<DNNFNode>(NodeType::Decomposed, -3);
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
        useETT && std::any_of(policies.begin(), policies.end(),
                              [](const DecompositionPolicy& policy) {
                                  return policy.phase == CCProbePhase::Ett;
                              });
    const DecompositionPolicy outerPolicy = decompositionPolicy;

    if (!needsComponentTrees || getComponents().empty()) {
        try {
            for (size_t i = 0; i < blocks.size(); ++i) {
                decompositionPolicy = policies[i];
                auto [result, node] = DXD(blocks[i], parent_depth + 1);
                if (result.isZero()) {
                    decompositionPolicy = outerPolicy;
                    return {DNNFResult(0), F};
                }
                totalResult = totalResult * result;
                subNodes.push_back(node);
            }
        } catch (...) {
            decompositionPolicy = outerPolicy;
            throw;
        }
        decompositionPolicy = outerPolicy;
        auto decompNode = buildDecomposableNode(subNodes);
        return {totalResult, decompNode};
    }
    
    SubGraph* outerSubgraph = activeSubgraph_;

    std::vector<std::unique_ptr<splaytree::EulerTourTree>> stash;

    auto& comps = getComponents();

    std::vector<std::unique_ptr<splaytree::EulerTourTree>> forest;
    forest.swap(comps);
    stash.resize(blocks.size());
    std::vector<bool> used(forest.size(), false);
    for (size_t i = 0; i < blocks.size(); ++i) {
        for (size_t j = 0; j < forest.size(); ++j) {
            if (used[j] || !forest[j]) continue;
            const int representative = forest[j]->getAnyVertex();
            if (representative >= 0 && blocks[i].rows.count(representative)) {
                stash[i] = std::move(forest[j]);
                used[j] = true;
                break;
            }
        }
        if (!stash[i]) {
            for (auto& tree : stash)
                if (tree) comps.push_back(std::move(tree));
            for (auto& tree : forest)
                if (tree) comps.push_back(std::move(tree));
            activeSubgraph_ = outerSubgraph;
            return {DNNFResult(0), F};
        }
    }
    for (size_t j = 0; j < forest.size(); ++j) {
        if (!used[j] && forest[j]) {
            // The forest must describe exactly the blocks being searched.
            for (auto& tree : stash)
                if (tree) comps.push_back(std::move(tree));
            for (auto& tree : forest)
                if (tree) comps.push_back(std::move(tree));
            activeSubgraph_ = outerSubgraph;
            return {DNNFResult(0), F};
        }
    }

    auto restoreStash = [&]() {
        comps.clear();
        for (auto& t : stash)
            if (t) comps.push_back(std::move(t));
        activeSubgraph_ = outerSubgraph;
    };

    try {
        for (size_t i = 0; i < blocks.size(); ++i) {

        if (!stash[i]) {
            std::cerr << "serialSearch: component " << i << " missing\n";
            restoreStash();
            decompositionPolicy = outerPolicy;
            return {DNNFResult(0), F};
        }

        int anyV = stash[i]->getAnyVertex();
        activeSubgraph_ = (anyV >= 0) ? graph->subgraphOf(anyV) : nullptr;
        comps.push_back(std::move(stash[i]));

        decompositionPolicy = policies[i];

        std::pair<DNNFResult, shared_ptr<DNNFNode>> childResult;
        try {
            childResult = DXD(blocks[i], parent_depth + 1);
        } catch (...) {
            decompositionPolicy = outerPolicy;
            throw;
        }
        auto& [result, node] = childResult;

        if (!comps.empty()) {
            stash[i] = std::move(comps[0]);
            comps.clear();
        }

        if (result.isZero()) {
            restoreStash();
            decompositionPolicy = outerPolicy;
            return {DNNFResult(0), F};
        }
        totalResult = totalResult * result;
        subNodes.push_back(node);
        }
    } catch (...) {
        for (size_t i = 0; i < stash.size(); ++i) {
            if (!stash[i] && !comps.empty()) {
                stash[i] = std::move(comps.front());
                comps.clear();
                break;
            }
        }
        restoreStash();
        decompositionPolicy = outerPolicy;
        throw;
    }
    restoreStash();
    decompositionPolicy = outerPolicy;
    
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
    std::vector<std::unique_ptr<splaytree::EulerTourTree>> returned(n);
    
    auto& comps = getComponents();
    bool manageTrees = useETT && componentTreesAvailable;
    const bool manageThreadState = useETT;
    if (manageTrees) {
        std::vector<int> treeForBlock(n, -1);
        std::vector<bool> treeUsed(comps.size(), false);
        for (int i = 0; i < n && manageTrees; ++i) {
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
            for (int i = 0; i < n; ++i)
                extracted[i] = std::move(comps[treeForBlock[i]]);
            comps.clear();
        }
    }

    std::vector<DNNFResult> results(n);
    std::vector<shared_ptr<DNNFNode>>  nodes(n, nullptr);

    #pragma omp parallel for schedule(dynamic)
    for (int i = 0; i < n; i++) {
        if (has_failure.load(std::memory_order_acquire) || 
            has_timeout.load(std::memory_order_acquire)) {
            if(manageTrees) returned[i] = std::move(extracted[i]);
            continue;
        }

        std::unique_ptr<ThreadLocalState> outerTls;
        if (manageThreadState) {
            outerTls = std::move(tlsState);
        }

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

            if (manageTrees && tlsState && !tlsState->components.empty()) {
                returned[i] = std::move(tlsState->components.front());
            }
        } catch (const std::runtime_error& e) {
            std::string msg = e.what();
            if (msg.find("Time bound") != std::string::npos) {
                has_timeout.store(true, std::memory_order_release);
            } else {
                has_failure.store(true, std::memory_order_release);
                std::cerr << "Thread " << omp_get_thread_num() 
                         << " error: " << msg << "\n";
            }
            if (manageTrees && tlsState && !tlsState->components.empty()) {
                returned[i] = std::move(tlsState->components.front());
            }
        } catch (const std::exception& e) {
            has_failure.store(true, std::memory_order_release);
            std::cerr << "Thread " << omp_get_thread_num() 
                     << " exception: " << e.what() << "\n";
            if (manageTrees && tlsState && !tlsState->components.empty()) {
                returned[i] = std::move(tlsState->components.front());
            }
        } catch (...) {
            has_failure.store(true, std::memory_order_release);
            std::cerr << "Thread " << omp_get_thread_num() 
                     << " unknown error\n";
            if (manageTrees && tlsState && !tlsState->components.empty()) {
                returned[i] = std::move(tlsState->components.front());
            }
        }

        if (manageThreadState) {
            cleanupThreadLocalState();
            tlsState = std::move(outerTls);
        }
    }

    if (manageTrees) {
        comps.resize(n);
        for (int i = 0; i < n; ++i)
            comps[i] = std::move(returned[i]);
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

    if (dxz_mode ? isSolved() : block.cols.empty()) {
        return {DNNFResult(1), T};
    }

    vector<int> stateCols;
    size_t stateHash = dxz_mode
        ? (encodeColState(&stateCols) ^ size_t{0xd6e8feb86659fd93ULL})
        : hashBlockState(block.cols, &stateCols);
    CacheKey state{stateHash, std::move(stateCols), dxz_mode};

    DNNFResult cachedCount(0);
    shared_ptr<DNNFNode> cachedNode;
    if (getCachedResult(state, cachedCount, cachedNode)) {
        return {cachedCount, cachedNode};
    }

    // 是否分解查询
    bool shouldProbe = false;
    // 是否使用动态分解
    bool useDynamicEtt = false;
    // dyndxd：初始查询
    const bool initialProbe = depth == 1 && !dxd_mode && !dxz_mode;

    if (dxd_mode) {
        shouldProbe = true;
    } else if (!dxz_mode) { // dyndxd 根据分解策略决定
        auto& policy = currentDecompositionPolicy();
        if (isCCETTCallBudgetExhausted()) {
            // 禁用 ETT 分解
            stopCCForETTCallBudget();
        } else if (policy.skipNextProbe) {
            policy.skipNextProbe = false;
        } else if (policy.phase != CCProbePhase::Disabled) {
            if (policy.phase == CCProbePhase::Ett && !useETT) {
                policy.phase = policy.ettFailureAction ==
                                       EttFailureAction::FallbackToBfs
                                   ? CCProbePhase::Bfs
                                   : CCProbePhase::Disabled;
            }
            shouldProbe = policy.phase != CCProbePhase::Disabled;
            useDynamicEtt = policy.phase == CCProbePhase::Ett;
        }
    }

    if (useDynamicEtt && !tryReserveETTCall()) {
        stopCCForETTCallBudget();
        shouldProbe = false;
        useDynamicEtt = false;
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
                dxd_mode && (collectCCTime || collectCCExperimentStats)
                    ? std::clock()
                    : std::clock_t{0};
            curBlock = getComponentsByBFS(
                block.cols,
                collectCCExperimentStats && dxd_mode ? &bfsMetrics : nullptr);
            if (dxd_mode && (collectCCTime || collectCCExperimentStats)) {
                addCCCpuTicks(bfsStart, ccBfsCpuTicks);
            }
        }

        if (collectCCExperimentStats) {
            std::lock_guard<std::mutex> lock(ccExperimentStatsMutex);
            if (dxd_mode) {
                ccExperimentStats.bfsVertexSum += bfsMetrics.vertices;
                ccExperimentStats.bfsEdgeSum += bfsMetrics.graphEdges;
            }
        }

        if (useDynamicEtt && isCCETTCallBudgetExhausted()) {
            stopCCForETTCallBudget();
        }

        updateMaxBlockCount(curBlock.size());

        if (!initialProbe && !dxd_mode) {
            updateDecompositionPolicy(currentDecompositionPolicy(),
                                      curBlock.size());
        }

        if (int(curBlock.size()) > 1) {
            vector<DecompositionPolicy> policies(
                curBlock.size(), DecompositionPolicy{});
            if (initialProbe) {
                // 初始查询出现多分块
                const size_t selected =
                    selectInitialProbeBlock(curBlock, blockArea(InitBlock),
                                            bfs_area_threshold);
                if (currentDecompositionPolicy().phase == CCProbePhase::Ett) {
                    policies[selected] = DecompositionPolicy{
                        CCProbePhase::Ett, EttFailureAction::Disable, 0, true};
                }
            }

            const bool treesAvailable =
                useETT && std::any_of(
                             policies.begin(), policies.end(),
                             [](const DecompositionPolicy& policy) {
                                 return policy.phase == CCProbePhase::Ett;
                             });

            auto decompResult = isParallelSearch
                ? parallelSearchUseOmp(curBlock, depth, treesAvailable,
                                       std::move(policies))
                : serialSearch(curBlock, depth, std::move(policies));

            auto [result, decompNode] = decompResult;

            setCachedResult(state, result, decompNode);
            return {result, decompNode};
        }

    }

    ColumnHeader* choose = dxz_mode ? selectCol() : selectOptimalColumn(block.cols);
    // std::cout << "Chosen column: " << choose->col << " (size: " << choose->size << ")\n";

    if(choose->size <= 0) {
        if (isCCETTCallBudgetExhausted()) stopCCForETTCallBudget();
        setCachedResult(state, DNNFResult(0), F);
        return {DNNFResult(0), F};
    }

    DNNFResult totalResult(0);
    shared_ptr<DNNFNode> x = F;

    auto coverCurrentBlock = [&](int col, set<int>& removedRows) {
        coverInBlock(col, block, removedRows);
    };
    auto uncoverCurrentBlock = [&](int col) {
        uncoverInBlock(col, block);
    };
    const bool collectUpdateTiming =
        collectCCTime || collectCCExperimentStats;

    auto decrementCurrentCC = [&](const set<int>& rows) {
        const std::clock_t start =
            collectUpdateTiming ? std::clock() : std::clock_t{0};
        DecUpdateCC(rows);
        if (collectUpdateTiming) {
            addCCCpuTicks(start, ccDecrementCpuTicks);
        }
        if (quickStatsUpdateLimit > 0) {
            std::lock_guard<std::mutex> lock(ccExperimentStatsMutex);
            if (ccExperimentStats.decCalls >= quickStatsUpdateLimit) {
                throw QuickStatsComplete();
            }
        }
    };
    auto incrementCurrentCC = [&](const set<int>& rows) {
        const std::clock_t start =
            collectUpdateTiming ? std::clock() : std::clock_t{0};
        IncUpdateCC(rows);
        if (collectUpdateTiming) {
            addCCCpuTicks(start, ccIncrementCpuTicks);
        }
    };

    set<int> deleted_rows;
    if (dxz_mode) {
        cover(choose->col);
    } else {
        coverCurrentBlock(choose->col, deleted_rows);
    }

    const bool frameEttUpdated = shouldMaintainDynamicEtt();

    if (frameEttUpdated) decrementCurrentCC(deleted_rows);
    if (isCCETTCallBudgetExhausted()) stopCCForETTCallBudget();

    Node* curC = choose->down;
    bool rowCovered = false;
    bool rowEttUpdated = false;
    set<int> deleted_rows_;

    auto restoreCurrentFrame = [&]() {
        if (rowCovered) {
            Node* curR = curC->left;
            while (curR != curC) {
                if (dxz_mode) uncover(curR->col);
                else uncoverCurrentBlock(curR->col);
                curR = curR->left;
            }
            if (rowEttUpdated) incrementCurrentCC(deleted_rows_);
            rowEttUpdated = false;
            rowCovered = false;
        }
        if (dxz_mode) uncover(choose->col);
        else uncoverCurrentBlock(choose->col);
        if (frameEttUpdated) incrementCurrentCC(deleted_rows);
    };

    try {
        while(curC != choose) {
        
        Node* curR = curC->right;
        deleted_rows_.clear();

        while (curR != curC) {
            if (dxz_mode) {
                cover(curR->col);
            } else {    
                coverCurrentBlock(curR->col, deleted_rows_);
            }
            curR = curR->right;
        }
        rowEttUpdated = shouldMaintainDynamicEtt();
        if (rowEttUpdated) decrementCurrentCC(deleted_rows_);
        rowCovered = true;
 
        auto [result, sub_node] = DXD(block, depth + 1);

        if(!result.isZero()) {
            x = buildDecisionNode(curC->row, x, sub_node);
            totalResult = totalResult + result;
        }
        
        curR = curC->left;
        while (curR != curC) {
            if (dxz_mode) {
                uncover(curR->col);
            } else {
                uncoverCurrentBlock(curR->col);
            }
            curR = curR->left;
        }
        if (rowEttUpdated) incrementCurrentCC(deleted_rows_);
        rowEttUpdated = false;
        rowCovered = false;

        curC = curC->down;
        }
    } catch (...) {
        restoreCurrentFrame();
        throw;
    }
    if (dxz_mode) {
        uncover(choose->col);
    } else {
        uncoverCurrentBlock(choose->col);
    }
    if (frameEttUpdated) incrementCurrentCC(deleted_rows);

    setCachedResult(state, totalResult, x);
    return {totalResult, x};
}

size_t DanceDNNF::countDNNFNodes() const {
 
    if (!rootDNNF) {
        return 0;
    }
 
    // visited is keyed on node->id, not on the raw pointer
    std::unordered_set<size_t> visited;
 
    std::function<size_t(const shared_ptr<DNNFNode>&)> traverse =
        [&](const shared_ptr<DNNFNode>& node) -> size_t {
            if (!node)                       return 0;
            if (node->isTerminal())          return 0;   // T / F don't count
            if (visited.count(node->id))     return 0;   // already counted (DAG sharing)
            visited.insert(node->id);
 
            size_t total = 1;
            total += traverse(node->left);
            total += traverse(node->right);
            for (const auto& child : node->children) total += traverse(child);
            return total;
        };
 
    return traverse(rootDNNF);
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
    if (dxd_mode) {
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

// DXD DXZ入口
void DanceDNNF::startDXD() {

    if(!controlOUTPUT)  logger.logLine("开始DXD搜索...");

    MAX_B_COUNT.store(1, std::memory_order_relaxed);
    ccBfsCpuTicks.store(0, std::memory_order_relaxed);
    ccDecrementCpuTicks.store(0, std::memory_order_relaxed);
    ccIncrementCpuTicks.store(0, std::memory_order_relaxed);
    if (collectCCExperimentStats) ccExperimentStats.reset();
    resetAdaptiveDecompositionState();

    {
        std::unique_lock<std::shared_mutex> cacheLock(cacheMutex);
        cache.clear();
    }
    {
        std::unique_lock<std::shared_mutex> tableLock(tableMutex);
        node_table.clear();
    }
    if(!dxz_mode) {
        dxd_mode = true; // 启用DXD模式
    }
    
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
        if (!dxz_mode && collectCCTime) {
            logger.logLine("DXD CC BFS CPU: " + std::to_string(ticksToSeconds(ccBfsCpuTicks)) + " s");
        }
        if (!dxz_mode) {
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



        if(dxz_mode) {
            size_t zdd_size = countZDDSize();
            logger.logLine("ZDD Size: " + std::to_string(zdd_size));
        } else {
            size_t dnnf_size = countDNNFNodes();
            logger.logLine("DNNF Size: " + std::to_string(dnnf_size));
        }


        return;
    } catch (std::runtime_error &e) {
        timeout = true;
        if(dxz_mode) { 
            logger.logLine("DXZ搜索超时: " + std::string(e.what()));
        } else {
            if (collectCCTime) {
                logger.logLine("DXD CC BFS CPU: " +
                               std::to_string(ticksToSeconds(ccBfsCpuTicks)) + " s");
            }
            logCCExperimentStats(false);
            logger.logLine("DXD搜索超时: " + std::string(e.what()));
        }
        return;
    }

}

// DynDXD
void DanceDNNF::startMultiThreadDXD() {

    // logger.logLine("开始多线程DXD搜索...");
    
    // isParallelSearch = true;
    MAX_B_COUNT.store(1, std::memory_order_relaxed);
    ccBfsCpuTicks.store(0, std::memory_order_relaxed);
    ccDecrementCpuTicks.store(0, std::memory_order_relaxed);
    ccIncrementCpuTicks.store(0, std::memory_order_relaxed);
    if (collectCCExperimentStats) ccExperimentStats.reset();
    resetAdaptiveDecompositionState();

    {
        std::unique_lock<std::shared_mutex> cacheLock(cacheMutex);
        cache.clear();
    }
    {
        std::unique_lock<std::shared_mutex> tableLock(tableMutex);
        node_table.clear();
    }

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
            
        size_t dnnf_size = countDNNFNodes();

        logger.logLine("DNNF Size: " + std::to_string(dnnf_size));

        return;
    } catch (const QuickStatsComplete&) {
        timer.markStopTime();
        logCCExperimentStats(true, true);
        timeout = false;
        return;
    } catch (std::runtime_error &e) {
        timeout = true;
        timer.markStopTime();
        const double elapsed = timer.getElapsedTime();
        logger.logLine("Time: " + std::to_string(elapsed) + " s");
        if (collectCCTime) {
            logger.logLine("Dyn CC Decrement CPU: " +
                           std::to_string(ticksToSeconds(ccDecrementCpuTicks)) + " s");
        }
        logCCExperimentStats(false);
        logger.logLine("DynDXD搜索超时: " + std::string(e.what()));
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
    coverInBlock( choose->col, block, deleted_rows );
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
            coverInBlock( curR->col, block, deleted_rows_ );  
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
            uncoverInBlock( curR->col, block );  
            curR = curR->left;  
        }  
        if (mode == DecomMode::Dynamic) {
            IncUpdateCC(deleted_rows_);
        }
        curC = curC->down;  
    }  
    uncoverInBlock( choose->col, block );  
    if (mode == DecomMode::Dynamic) {
        IncUpdateCC(deleted_rows);
    }
    return totalResult; 
}

void DanceDNNF::start_MDLX(DecomMode mode) {

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
