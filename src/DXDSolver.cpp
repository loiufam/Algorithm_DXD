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

    if (!useETT || isCurrentDynamicEttDisabled() || getComponents().empty()) {
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
    
    SubGraph* outerSubgraph = activeSubgraph_;

    std::vector<std::unique_ptr<splaytree::EulerTourTree>> stash;

    auto& comps = getComponents();

    stash.swap(comps);

    auto restoreStash = [&]() {
        comps.clear();
        for (auto& t : stash)
            if (t) comps.push_back(std::move(t));
        activeSubgraph_ = outerSubgraph;
    };

    try {
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
    } catch (...) {
        for (size_t i = 0; i < stash.size(); ++i) {
            if (!stash[i] && !comps.empty()) {
                stash[i] = std::move(comps.front());
                comps.clear();
                break;
            }
        }
        restoreStash();
        throw;
    }
    restoreStash();
    
    auto decompNode = buildDecomposableNode(subNodes);
    return {totalResult, decompNode};
}

//   Launch one OpenMP thread per independent block.
//   Returns the product of counts AND the Decomposed-AND node.
std::pair<DNNFResult, shared_ptr<DNNFNode>> DanceDNNF::parallelSearchUseOmp(
    vector<Block>& blocks, int parent_depth, bool disableDynamicUpdates,
    bool componentTreesAvailable) {

    const int n = blocks.size();
    std::atomic<bool> has_failure(false);
    std::atomic<bool> has_timeout(false);

    std::vector<std::unique_ptr<splaytree::EulerTourTree>> extracted(n);
    std::vector<std::unique_ptr<splaytree::EulerTourTree>> returned(n);
    
    auto& comps = getComponents();
    const bool manageTrees = useETT && componentTreesAvailable;
    if(manageTrees) {
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
            if(manageTrees) returned[i] = std::move(extracted[i]);
            continue;
        }

        std::unique_ptr<ThreadLocalState> outerTls;
        if (manageTrees) {

            outerTls = std::move(tlsState);
        }

        try {
            // === 初始化线程局部状态 ===
            if(manageTrees) {
                const int outerDepth = outerTls ? outerTls->decompose_depth : 0;
                initThreadLocalState(blocks[i], std::move(extracted[i]));
                if (tlsState) {
                    tlsState->decompose_depth = outerDepth + 1;
                    if (disableDynamicUpdates) {
                        tlsState->dynamic_ett_disabled = true;
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

        if (manageTrees) {
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

    const bool useDxzSearch = dxz_mode || isDxzFallbackMode();

    if (useDxzSearch && isSolved()) {
        return {DNNFResult(1), T};
    } else if (block.cols.empty()) {
        return {DNNFResult(1), T};
    }

    size_t state = useDxzSearch
        ? (encodeColState() ^ size_t{0xd6e8feb86659fd93ULL}) : hashBlockState(block.cols);
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

    if (shouldTryDecompose(block)) {
        const bool useDynamicEtt = shouldUseDynamicEtt(block, depth);
        BFSScanMetrics bfsMetrics;
        vector<Block> bfsBlocks;
        if (collectCCExperimentStats || !useDynamicEtt) {
            bfsBlocks = getComponentsByBFS(
                block.cols, collectCCExperimentStats ? &bfsMetrics : nullptr);
        }
        vector<Block> curBlock = useDynamicEtt? getComponentsByETT(block.cols) : std::move(bfsBlocks);
        
        if (collectCCExperimentStats) {
            std::lock_guard<std::mutex> lock(ccExperimentStatsMutex);
            
            ++ccExperimentStats.ccComputations;
            
            if (curBlock.size() > 1) {
                ccExperimentStats.cc_decompose++;
            }

            if (useDynamicEtt) {
                ++ccExperimentStats.ettCcTimes;
            }
        }

        MAX_B_COUNT = std::max(MAX_B_COUNT, curBlock.size());

        updateAdaptiveDecompositionState(block, curBlock.size());

        if (int(curBlock.size()) > 1) {

            const bool treesAvailable = useETT && !isCurrentDynamicEttDisabled();
            auto decompResult = isParallelSearch
                ? parallelSearchUseOmp(curBlock, depth, false, treesAvailable)
                : serialSearch(curBlock, depth);

            auto [result, decompNode] = decompResult;

            setCacheCount(state, result);
            setCache(state, decompNode);
            return {result, decompNode};
        }

    }

    ColumnHeader* choose = useDxzSearch ? selectCol() : selectOptimalColumn(block.cols);
    // std::cout << "Chosen column: " << choose->col << " (size: " << choose->size << ")\n";

    if(choose->size <= 0) {
        setCacheCount(state, DNNFResult(0));
        setCache(state, F);
        return {DNNFResult(0), F};
    }

    DNNFResult totalResult(0);
    shared_ptr<DNNFNode> x = F;

    set<int> deleted_rows;
    if (useDxzSearch) {
        cover(choose->col);
    } else {
        coverInBlock(choose->col, block, deleted_rows);
    }

    const bool frameEttUpdated = shouldMaintainDynamicEtt(depth);
    if (frameEttUpdated) DecUpdateCC(deleted_rows);

    Node* curC = choose->down;
    bool rowCovered = false;
    bool rowEttUpdated = false;
    set<int> deleted_rows_;

    auto restoreCurrentFrame = [&]() {
        if (rowCovered) {
            Node* curR = curC->left;
            while (curR != curC) {
                if (useDxzSearch) uncover(curR->col);
                else uncoverInBlock(curR->col, block);
                curR = curR->left;
            }
            if (rowEttUpdated) IncUpdateCC(deleted_rows_);
            rowEttUpdated = false;
            rowCovered = false;
        }
        if (useDxzSearch) uncover(choose->col);
        else uncoverInBlock(choose->col, block);
        if (frameEttUpdated) IncUpdateCC(deleted_rows);
    };

    try {
        while(curC != choose) {
        
        Node* curR = curC->right;
        deleted_rows_.clear();

        while (curR != curC) {
            if (useDxzSearch) {
                cover(curR->col);
            } else {    
                coverInBlock(curR->col, block, deleted_rows_);
            }
            curR = curR->right;
        }
        rowEttUpdated = shouldMaintainDynamicEtt(depth);
        if (rowEttUpdated) DecUpdateCC(deleted_rows_);
        rowCovered = true;
 
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
        if (rowEttUpdated) IncUpdateCC(deleted_rows_);
        rowEttUpdated = false;
        rowCovered = false;

        curC = curC->down;
        }
    } catch (...) {
        restoreCurrentFrame();
        throw;
    }
    if (useDxzSearch) {
        uncover(choose->col);
    } else {
        uncoverInBlock(choose->col, block);
    }
    if (frameEttUpdated) IncUpdateCC(deleted_rows);

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

    CCExperimentStats stats;
    {
        std::lock_guard<std::mutex> lock(ccExperimentStatsMutex);
        stats = ccExperimentStats;
    }
    logger.logLine("CC Stats Complete: " + std::to_string(complete ? 1 : 0));
    logger.logLine("CC Stats Init Graph Edges: " + std::to_string(stats.graph_init_edges));

    logger.logLine("CC Stats Query: " + std::to_string(stats.ccComputations));
    logger.logLine("CC Stats Splits: " + std::to_string(stats.cc_decompose));
    logger.logLine("CC Stats ETT CC Times: " + std::to_string(stats.ettCcTimes));
    logger.logLine("CC Stats ETT Dec Calls:" + std::to_string(stats.dec_calls));

    // DXD: |V| + |E|
    // ETT V
    logger.logLine("CC Stats ETT DXD Vertex Sum: " + std::to_string(stats.ettV));
    // ETT E
    logger.logLine("CC Stats ETT DXD Edge Sum: " + std::to_string(stats.ettE));
    
    // DynDXD: |Ed| x (log |V| + |Er| ) + |Vd|
    // ETT Vd
    logger.logLine("CC Stats Dyn ETT Updated Vertex Sum: " + std::to_string(stats.ettVd));
    // ETT Ed
    logger.logLine("CC Stats Dyn ETT Updated Edge Sum: " + std::to_string(stats.ettEd)); 
    // ETT Er
    logger.logLine("CC Stats Dyn ETT Replacement Scan Steps: " +
                   std::to_string(stats.ettEr));
}

// DXD DXZ入口
void DanceDNNF::startDXD() {

    if(!controlOUTPUT)  logger.logLine("开始DXD搜索...");

    MAX_B_COUNT = 1;
    ccCpuTime = 0.0;
    if (collectCCExperimentStats) ccExperimentStats.reset();
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
        // if (!dxz_mode) logger.logLine("DXD CC CPU: " + std::to_string(ccCpuTime) + " s");
        timeout = false;

        solutionCount = ResSols.toString();
        logger.logLine("Solutions: " + solutionCount);
        if (solutionCount.size() > 12) logger.logLine("Solutions (scientific): " + ResSols.toScientificString(3)); 
    
        logger.logLine("Max Blocks: " + std::to_string(MAX_B_COUNT));



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
        // logger.logLine("Dyn CC CPU: " + std::to_string(ccCpuTime) + " s");
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
