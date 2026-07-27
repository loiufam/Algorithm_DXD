#include "../include/DXD.h"

shared_ptr<DNNFNode> DanceDNNF::buildDecisionNode(int r, shared_ptr<DNNFNode> lo, shared_ptr<DNNFNode> hi) {
    if (hi == F) {
        return lo;
    }

    size_t key = gen_key(r, lo.get(), hi.get());

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
std::pair<DNNFResult, shared_ptr<DNNFNode>> DanceDNNF::serialSearch(vector<Block>& blocks, int parent_depth) {

    DNNFResult totalResult(1);
    vector<shared_ptr<DNNFNode>> subNodes;
    subNodes.reserve(blocks.size());

    if (!useETT) {
        for (size_t i = 0; i < blocks.size(); ++i) {
            auto [result, node] = DXD(blocks[i], parent_depth + 1);
            if (result.isZero()) {
                return {DNNFResult(0), F};
            }
            totalResult = totalResult * result;
            subNodes.push_back(node);
        }
        auto decompNode = buildDecomposableNode(subNodes);
        return {totalResult, decompNode};
    }
    
    // ── ETT: thread-local component management 
    SubGraph* outerSubgraph = activeSubgraph_;

    std::vector<std::unique_ptr<splaytree::EulerTourTree>> stash;

    auto& comps = getComponents(); // 获取当前组件

    stash.swap(comps);

    auto restoreStash = [&]() {
        comps.clear();
        for (auto& t : stash)
            if (t) comps.push_back(std::move(t));
        activeSubgraph_ = outerSubgraph;
    };

    for (size_t i = 0; i < blocks.size(); ++i) {

        if (i >= stash.size() || !stash[i]) {
            std::cerr << "serialSearch: component " << i << " missing\n";
            restoreStash();
            return {DNNFResult(0), F};
        }

        int anyV = stash[i]->getAnyVertex();
        activeSubgraph_ = (anyV >= 0) ? graph->subgraphOf(anyV) : nullptr;
        comps.push_back(std::move(stash[i]));

        auto [result, node] = DXD(blocks[i], parent_depth + 1);

        // if (comps.size() > 1) {
        //     std::cerr << "serialSearch: invariant violated, comps.size()=" << comps.size()
        //               << " after DXD(block " << i << ") returned\n";
        //     // 仍然保留首棵作为最佳猜测，避免崩溃；其余直接丢弃以便快速复现 bug。
        // }
        if (!comps.empty()) {
            stash[i] = std::move(comps[0]);
            comps.clear();
        }

        if (result.isZero()) {
            restoreStash();
            return {DNNFResult(0), F};
        }
        totalResult = totalResult * result;
        subNodes.push_back(node);
    }
    restoreStash();
    
    auto decompNode = buildDecomposableNode(subNodes);
    return {totalResult, decompNode};
}

//   Launch one OpenMP thread per independent block.
//   Returns the product of counts AND the Decomposed-AND node.
std::pair<DNNFResult, shared_ptr<DNNFNode>> DanceDNNF::parallelSearchUseOmp(
    vector<Block>& blocks, int parent_depth, bool disableDynamicUpdates) {

    const int n = blocks.size();
    std::atomic<bool> has_failure(false);
    std::atomic<bool> has_timeout(false);

    std::vector<std::unique_ptr<splaytree::EulerTourTree>> extracted(n);
    std::vector<std::unique_ptr<splaytree::EulerTourTree>> returned(n);
    
    auto& comps = getComponents(); // 获取当前组件
    if(useETT) {
        for (int i = 0; i < n; ++i)
            extracted[i] = std::move(comps[i]);
        comps.clear();
    }

    std::vector<DNNFResult> results(n);
    std::vector<shared_ptr<DNNFNode>>  nodes(n, nullptr);

    #pragma omp parallel for schedule(dynamic)
    for (int i = 0; i < n; i++) {
        if (has_failure.load(std::memory_order_acquire) || 
            has_timeout.load(std::memory_order_acquire)) {
            if(useETT) returned[i] = std::move(extracted[i]);
            continue;
        }

        std::unique_ptr<ThreadLocalState> outerTls;
        if (useETT) {
            // 嵌套并行分解时，同一个 OpenMP worker 可能已经持有父级子块的 TLS。
            // 暂存父 TLS，再为当前子分量安装独立 TLS，避免 cleanup 把父级状态释放。
            outerTls = std::move(tlsState);
        }

        try {
            // === 初始化线程局部状态 ===
            if(useETT) {
                const int outerDepth = outerTls ? outerTls->decompose_depth : 0;
                initThreadLocalState(blocks[i], std::move(extracted[i]));
                if (tlsState) {
                    tlsState->decompose_depth = outerDepth + 1;
                    // 父块已经分出足够多的独立子块时，子块只负责搜索；不再维护
                    // ETT，也不再尝试更深层分解。该标志必须显式传入新建的 TLS，
                    // 否则父 TLS 中的关闭状态会在这里丢失。
                    if (disableDynamicUpdates) {
                        tlsState->dynamic_ett_disabled = true;
                        tlsState->decomposition_disabled = true;
                    }
                }
            }

            // === 执行搜索（自动使用线程局部数据） ===
            auto [result, node] = DXD(blocks[i], parent_depth + 1);

            if (result.isZero()) {
                has_failure.store(true, std::memory_order_release);
            } else {
                results[i] = result;
                nodes[i] = node;
            }

            if (useETT && tlsState && !tlsState->components.empty()) {
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
            // P1-4 fix: 异常路径同样要把线程局部 ETT 抢救回来，
            // 否则 cleanupThreadLocalState() 会析构掉，主线程合并 comps 时槽位为空。
            if (useETT && tlsState && !tlsState->components.empty()) {
                returned[i] = std::move(tlsState->components.front());
            }
        } catch (const std::exception& e) {
            has_failure.store(true, std::memory_order_release);
            std::cerr << "Thread " << omp_get_thread_num() 
                     << " exception: " << e.what() << "\n";
            if (useETT && tlsState && !tlsState->components.empty()) {
                returned[i] = std::move(tlsState->components.front());
            }
        } catch (...) {
            has_failure.store(true, std::memory_order_release);
            std::cerr << "Thread " << omp_get_thread_num() 
                     << " unknown error\n";
            if (useETT && tlsState && !tlsState->components.empty()) {
                returned[i] = std::move(tlsState->components.front());
            }
        }

        if (useETT) {
            cleanupThreadLocalState();
            tlsState = std::move(outerTls);
        }
    }

    if (useETT) {
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


// DXD IDXD
std::pair<DNNFResult, shared_ptr<DNNFNode>> DanceDNNF::DXD(Block& block, int depth) {
    
    // std::ostringstream oss;
    // oss << "\n============================\n";
    // oss << "[Before] DXD called at depth " << depth
    //     << ", omp_tid = " << omp_get_thread_num()
    //     << "\n";
    // std::cout << oss.str();
    // printComponents();

    // A dynamic-connectivity operation can run past the subprocess grace
    // period without reaching another timeout check.  Keep a recent, fully
    // flushed snapshot so a hard kill retains the work done up to that point.
    if (collectCCExperimentStats && timer.getElapsedTime() >= nextCCStatsSnapshotTime) {
        logCCExperimentStats(false);
        nextCCStatsSnapshotTime = timer.getElapsedTime() + 1.0;
    }

    if(timer.timeBoundBroken()) {
        throw std::runtime_error("Time bound broken");
    }

    const bool useDxzSearch = dxz_mode || isDxzFallbackMode();

    if (useDxzSearch && isSolved()) {
        return {DNNFResult(1), T};
    } else if (block.cols.empty()) {
        return {DNNFResult(1), T};
    }

    size_t state = useDxzSearch
        ? (encodeColState() ^ size_t{0xd6e8feb86659fd93ULL})
        : hashBlockState(block.cols);
    {
        std::shared_lock<std::shared_mutex> readLock(cacheMutex);
        auto countIt = countCache.find(state);
        if (countIt != countCache.end()) {
            // Retrieve the cached compiled node as well
            auto nodeIt = C.find(state);
            shared_ptr<DNNFNode> cached_node =
                (nodeIt != C.end()) ? nodeIt->second : T;
            return {countIt->second, cached_node};
        }
    }

    // dxz模式跳过分块检测，直接进行列选择和分支
    if (!dxz_mode && shouldTryDecompose(block, depth)) {
        
        // const bool useDynamicEttForSplit = shouldUseDynamicEtt(block, depth);
        // const bool canComputeComponents = useDynamicEttForSplit || dxd_mode || !useETT;
        // if (canComputeComponents) {
            vector<Block> curBlock;
            if (useETT) {
                const std::clock_t ccStart = std::clock();
                curBlock = getComponentsByETT(block.cols);
                ccCpuTime += static_cast<double>(std::clock() - ccStart) / CLOCKS_PER_SEC;
            } else {
                // DXD 从头执行 BFS，并把 BFS 与 Block 集合构建整体计入 CC CPU。
                const std::clock_t ccStart = std::clock();
                curBlock = getComponentsByBFS(block.cols);
                ccCpuTime += static_cast<double>(std::clock() - ccStart) / CLOCKS_PER_SEC;
            }

            MAX_B_COUNT = std::max(MAX_B_COUNT, curBlock.size());
            updateAdaptiveDecompositionState(block, curBlock.size());

            if (int(curBlock.size()) > 1) {
                if (collectCCExperimentStats) ++ccExperimentStats.cc_decompose;
            // std::cout << "Detected " << curBlock.size() << " independent blocks at depth " << depth << ".\n";

                const bool stopDynamicUpdates =
                    !collectCCExperimentStats &&
                    curBlock.size() >= DYNAMIC_STOP_COMPONENT_COUNT;
                if (stopDynamicUpdates) {
                    disableDynamicEttForCurrentState();
                    if (isThreadLocal()) {
                        tlsState->decomposition_disabled = true;
                    } else {
                        decomposition_disabled = true;
                    }
                }

                auto decompResult = isParallelSearch
                    ? parallelSearchUseOmp(curBlock, depth, stopDynamicUpdates)
                    : serialSearch(curBlock, depth);

                auto [result, decompNode] = decompResult;

                setCacheCount(state, result);
                setCache(state, decompNode);
                return {result, decompNode};
            }
        // }

    }

    ColumnHeader* choose = useDxzSearch ? selectCol() : selectOptimalColumn(block.cols);
    // std::cout << "Chosen column: " << choose->col << " (size: " << choose->size << ")\n";

    if(choose->size <= 0) {
        setCacheCount(state, DNNFResult(0));
        setCache(state, F);
        return {DNNFResult(0), F};
    }

    // 将choose列下的行节点作为Decision节点加入children

    DNNFResult totalResult(0);
    shared_ptr<DNNFNode> x = F;

    set<int> deleted_rows;
    if (useDxzSearch) {
        cover(choose->col);
    } else {
        coverInBlock(choose->col, block, deleted_rows);
    }
    if(shouldMaintainDynamicEtt(depth)) timedDecUpdateCC(deleted_rows);

    Node* curC = choose->down;
    while(curC != choose) {
        
        Node* curR = curC->right;
        set<int> deleted_rows_;

        while (curR != curC) {
            if (useDxzSearch) {
                cover(curR->col);
            } else {    
                coverInBlock(curR->col, block, deleted_rows_);
            }
            curR = curR->right;
        }
        if(shouldMaintainDynamicEtt(depth)) timedDecUpdateCC(deleted_rows_);
 
        auto [result, sub_node] = DXD(block, depth + 1);

        if(!result.isZero()) {
            x = buildDecisionNode(curC->row, x, sub_node);
            totalResult = totalResult + result;
        }
        
        curR = curC->left;
        while (curR != curC) {
            if (useDxzSearch) {
                uncover(curR->col);
            } else {
                uncoverInBlock(curR->col, block);
            }
            curR = curR->left;
        }
        if(shouldMaintainDynamicEtt(depth)) timedIncUpdateCC(deleted_rows_);

        curC = curC->down;
    }
    if (useDxzSearch) {
        uncover(choose->col);
    } else {
        uncoverInBlock(choose->col, block);
    }
    if(shouldMaintainDynamicEtt(depth)) timedIncUpdateCC(deleted_rows);

    // std::cout << "\n============================\n";
    // std::cout << "[After] DXD called at depth " << depth << "\n";
    // printComponents();

    // 插入缓存
    setCacheCount(state, totalResult);
    setCache(state, x);
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

void DanceDNNF::logCCExperimentStats(bool complete) {
    if (!collectCCExperimentStats) return;
    const auto& stats = ccExperimentStats;
    logger.logLine("CC Stats Complete: " + std::to_string(complete ? 1 : 0));
    logger.logLine("CC Stats Calls: " + std::to_string(stats.calls()));
    logger.logLine("CC Stats Dec Calls: " + std::to_string(stats.decCalls));
    // logger.logLine("CC Stats Inc Calls: " + std::to_string(stats.incCalls));
    logger.logLine("CC Stats Merges: " + std::to_string(stats.merges));
    logger.logLine("CC Stats Tree Edge Cuts: " + std::to_string(stats.treeEdge_cuts)); 
    logger.logLine("CC Stats Splits: " + std::to_string(stats.splits));
    logger.logLine("CC Stats Decompose: " + std::to_string(stats.cc_decompose));

    logger.logLine("CC Stats Dec Vertex Sum: " + std::to_string(stats.V1));
    logger.logLine("CC Stats Dec Edge Sum: " + std::to_string(stats.E1));
    logger.logLine("CC Stats DecUpdate Vertex Sum: " + std::to_string(stats.Vd));
    logger.logLine("CC Stats DecUpdate Edge Sum: " + std::to_string(stats.Ed));

    logger.logLine("CC Stats Inc Vertex Sum: " + std::to_string(stats.V2));
    logger.logLine("CC Stats Inc Edge Sum: " + std::to_string(stats.E2));
    logger.logLine("CC Stats IncUpdate Vertex Sum: " + std::to_string(stats.Vi));
    logger.logLine("CC Stats IncUpdate Edge Sum: " + std::to_string(stats.Ei)); 
    // logger.logLine("CC Stats En Samples: " + std::to_string(stats.enSamples));
    logger.logLine("CC Stats En Sum: " + std::to_string(stats.enSum));
    // logger.logLine("CC Stats En Positive Updates: " + std::to_string(stats.enPositiveUpdates));
    // logger.logLine("CC Stats En Update Average Sum: " + std::to_string(stats.enUpdateAverageSum));
    logger.logLine("CC Stats Replacement Searches: " + std::to_string(stats.replacementSearchCalls));
    logger.logLine("CC Stats Replacement Scan Steps: " + std::to_string(stats.replacementScanSteps));
    // logger.logLine("CC Stats Early Breaks: " + std::to_string(stats.replacementEarlyBreaks));
    // logger.logLine("CC Stats Full Scans: " + std::to_string(stats.replacementFullScans));
}

// DXD DXZ入口
void DanceDNNF::startDXD() {

    if(!controlOUTPUT)  logger.logLine("开始DXD搜索...");

    MAX_B_COUNT = 1;
    ccCpuTime = 0.0;
    if (collectCCExperimentStats) ccExperimentStats.reset();
    nextCCStatsSnapshotTime = 0.0;
    resetAdaptiveDecompositionState();

    {
        std::unique_lock<std::shared_mutex> cacheLock(cacheMutex);
        C.clear();
        countCache.clear();
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
        if (!dxz_mode) logger.logLine("DXD CC CPU: " + std::to_string(ccCpuTime) + " s");
        timeout = false;

        solutionCount = ResSols.toString();
        logger.logLine("Solutions: " + solutionCount);
        if (solutionCount.size() > 12) logger.logLine("Solutions (scientific): " + ResSols.toScientificString(3)); 
    
        // if(!controlOUTPUT) logger.logLine("Max Blocks: " + std::to_string(MAX_B_COUNT));


        if(dxz_mode) {
            size_t zdd_size = countZDDSize();
            logger.logLine("ZDD Size: " + std::to_string(zdd_size));
        } else {
            // size_t dnnf_size = countDNNFNodes();
            // logger.logLine("DNNF Size: " + std::to_string(dnnf_size));
        }


        return;
    } catch (std::runtime_error &e) {
        timeout = true;
        if(dxz_mode) { 
            logger.logLine("DXZ搜索超时: " + std::string(e.what()));
        } else {
            logger.logLine("DXD CC CPU: " + std::to_string(ccCpuTime) + " s");
            logger.logLine("DXD搜索超时: " + std::string(e.what()));
        }
        return;
    }

}

// DynDXD
void DanceDNNF::startMultiThreadDXD() {

    logger.logLine("开始多线程DXD搜索...");
    
    // isParallelSearch = true;
    MAX_B_COUNT = 1;
    ccCpuTime = 0.0;
    if (collectCCExperimentStats) ccExperimentStats.reset();
    nextCCStatsSnapshotTime = 0.0;
    resetAdaptiveDecompositionState();
    if (!collectCCExperimentStats && isSmallInstanceForDynamicEtt()) {
        dynamic_ett_disabled = true;
        decomposition_disabled = true;
        dxz_fallback_mode = true;
        turnOffGraphSync();
    }

    {
        std::unique_lock<std::shared_mutex> cacheLock(cacheMutex);
        C.clear();
        countCache.clear();
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

        auto end = std::chrono::high_resolution_clock::now();
        timer.markStopTime();
   
        searchTime = std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count();
        logger.logLine("Time: " + std::to_string(searchTime) + " s");
        logger.logLine("Dyn CC CPU: " + std::to_string(ccCpuTime) + " s");
        logger.logLine("Dyn CC CPU Ratio: " +
                       std::to_string(searchTime > 0.0 ? ccCpuTime / searchTime : 0.0));
        logCCExperimentStats(true);
        timeout = false;

        solutionCount = ResSols.toString();
        logger.logLine("Solutions: " + solutionCount);
        if (solutionCount.size() > 12) logger.logLine("Solutions (scientific): " + ResSols.toScientificString(3));
    
        logger.logLine("Max Blocks: " + std::to_string(MAX_B_COUNT));
            
        size_t dnnf_size = countDNNFNodes();

        logger.logLine("DNNF Size: " + std::to_string(dnnf_size));

        return;
    } catch (std::runtime_error &e) {
        timeout = true;
        timer.markStopTime();
        const double elapsed = timer.getElapsedTime();
        logger.logLine("Time: " + std::to_string(elapsed) + " s");
        logger.logLine("Dyn CC CPU: " + std::to_string(ccCpuTime) + " s");
        logger.logLine("Dyn CC CPU Ratio: " +
                       std::to_string(elapsed > 0.0 ? ccCpuTime / elapsed : 0.0));
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

        MAX_B_COUNT = std::max(MAX_B_COUNT, curBlock.size());

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
    MAX_B_COUNT = 1;
 
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

        logger.logLine("Max Blocks: " + std::to_string(MAX_B_COUNT));
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
 
// ─────────────────────────────────────────────────────────────────────────────
// enumerateDFS  (file-scope static helper, not a class member)
//
// Depth-first traversal of the Decision-DNNF DAG.
//
// Parameters
//   node         – current node
//   hi_path      – row-ids accumulated on hi-branches from the root to here
//   count        – running solution counter (in/out)
//   max_sols     – cap; stop once count reaches this (0 = unlimited)
//   on_solution  – callback(sorted_row_vector, 1-based_index) per solution
//   memo         – solution-list cache keyed on node->id for Decomposed nodes;
//                  shared nodes are expanded only once
//
// Decision node  Decision(label, left=lo, right=hi):
//   lo (left)  → row `label` not selected; recurse without changing hi_path
//   hi (right) → row `label` selected; push label, recurse, pop
//
// Decomposed node  Decomposed(children=[c0,c1,...]):
//   Each child encodes an independent sub-problem.
//   Solutions = Cartesian product of all children's solution sets,
//   each result prefixed with the hi_path accumulated from ancestors.
//   Children are memoised so shared sub-trees are only expanded once.
//
// Terminal T (label==-1): valid path → invoke callback
// Terminal F (label==-2): dead end   → silent return
// ─────────────────────────────────────────────────────────────────────────────
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
 
        // Retrieve or compute the solution list for one child.
        // We pass max_sols=0 when filling the memo so the cached list is
        // complete and can be reused by any parent context.
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
