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
            return it->second;
        }
    }

    std::unique_lock<std::shared_mutex> writeLock(tableMutex);
    if (node_table.count(key)) return node_table[key];

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
    stash.swap(components);

    auto restoreStash = [&]() {
        components.clear();
        for (auto& t : stash)
            if (t) components.push_back(std::move(t));
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
        components.push_back(std::move(stash[i]));

        auto [result, node] = DXD(blocks[i], parent_depth + 1);

        if (!components.empty()) {
            stash[i] = std::move(components[0]);
            components.clear();
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
std::pair<DNNFResult, shared_ptr<DNNFNode>> DanceDNNF::parallelSearchUseOmp(vector<Block>& blocks, int parent_depth) {

    const int n = blocks.size();
    std::atomic<bool> has_failure(false);
    std::atomic<bool> has_timeout(false);

    std::vector<std::unique_ptr<splaytree::EulerTourTree>> extracted(n);
    std::vector<std::unique_ptr<splaytree::EulerTourTree>> returned(n);
    
    if(useETT) {
        for (int i = 0; i < n; ++i)
            extracted[i] = std::move(components[i]);
        components.clear();
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

        try {
            // === 初始化线程局部状态 ===
            if(useETT) initThreadLocalState(blocks[i], std::move(extracted[i]));

            // === 执行搜索（自动使用线程局部数据） ===
            auto [result, node] = DXD(blocks[i], parent_depth + 1);

            if (result.isZero()) {
                has_failure.store(true, std::memory_order_release);
            } else {
                results[i] = result;
                nodes[i] = node;
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
        } catch (const std::exception& e) {
            has_failure.store(true, std::memory_order_release);
            std::cerr << "Thread " << omp_get_thread_num() 
                     << " exception: " << e.what() << "\n";
        } catch (...) {
            has_failure.store(true, std::memory_order_release);
            std::cerr << "Thread " << omp_get_thread_num() 
                     << " unknown error\n";
        }

        if(useETT) cleanupThreadLocalState();
    }

    if (useETT) {
        components.resize(n);
        for (int i = 0; i < n; ++i)
            components[i] = std::move(returned[i]);
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
    
    shared_ptr<DNNFNode> decompNode;
    if (subNodes.empty()) {
        decompNode = T;
    } else if (subNodes.size() == 1) {
        decompNode = subNodes[0];
    } else {
        decompNode = buildDecomposableNode(subNodes);
    }
 
    return {totalResult, decompNode};
}


// DXD IDXD
std::pair<DNNFResult, shared_ptr<DNNFNode>> DanceDNNF::DXD(Block& block, int depth) {
    
    // std::cout << "\n============================\n";
    // std::cout << "[Before] DXD called at depth " << depth << "\n";
    // printComponents();

    if(timer.timeBoundBroken()) {
        throw std::runtime_error("Time bound broken");
    }
    
    if(block.cols.empty()) {
        return {DNNFResult(1), T};
    } 

    // 先查缓存
    size_t state = hashBlockState(block.cols); 
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

    if (block.rows.size() > 2 && shouldDecompose()) {
        
        vector<Block> curBlock;
        if (useETT ) {
            curBlock = getComponentsByETT();
        } else if (useIG) {
            curBlock = getComponentsByIG(block.rows);
        }

        MAX_B_COUNT = std::max(MAX_B_COUNT, curBlock.size());
        // addTriedNumbers(1);

        if (int(curBlock.size())  > 1) {
            // std::cout << "Detected " << curBlock.size() << " independent blocks at depth " << depth << ".\n";
            // 检测到多个独立分块，则并行处理
            if(!single_thread_mode) turnOffGraphSync();
            // addConcurrentThread(block_size);

            auto [result, decompNode] = isParallelSearch
                ? parallelSearchUseOmp(curBlock, depth)
                : serialSearch(curBlock, depth);

            setCacheCount(state, result);
            setCache(state, decompNode);
            return {result, decompNode};
        } 

    }

    ColumnHeader* choose = selectOptimalColumn(block.cols); 
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
    coverInBlock(choose->col, block, deleted_rows);
    if(useETT) DecUpdateCC(deleted_rows);

    Node* curC = choose->down;
    while(curC != choose) {
        
        Node* curR = curC->right;
        set<int> deleted_rows_;

        while (curR != curC) {
            coverInBlock(curR->col, block, deleted_rows_);
            curR = curR->right;
        }
        if(useETT) DecUpdateCC(deleted_rows_);
 
        auto [result, sub_node] = DXD(block, depth + 1);

        if(!result.isZero()) {
            x = buildDecisionNode(curC->row, x, sub_node);
            totalResult = totalResult + result;
        }
        
        curR = curC->left;
        while (curR != curC) {
            uncoverInBlock(curR->col, block);
            curR = curR->left;
        }
        if(useETT) IncUpdateCC(deleted_rows_);

        curC = curC->down;
    }
    uncoverInBlock(choose->col, block);
    if(useETT) IncUpdateCC(deleted_rows);

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
 
            size_t total = 1;   // this node
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


void DanceDNNF::startDXD() {

    if(!controlOUTPUT)  logger.logLine("开始单线程DXD搜索...");

    isParallelSearch = false; // 单线程搜索
    MAX_B_COUNT = 1;
    if(!dxz_mode) {
        single_thread_mode = true; // 启用单线程模式
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
        timeout = false;

        solutionCount = ResSols.toString();
        logger.logLine("Solutions: " + solutionCount);
    
        if(!controlOUTPUT) logger.logLine("Max Blocks: " + std::to_string(MAX_B_COUNT));

        size_t dnnf_size = countDNNFNodes();
        size_t zdd_size = countZDDSize();

        if(dxz_mode) {
            logger.logLine("ZDD Size: " + std::to_string(zdd_size));
        } else {
            logger.logLine("DNNF Size: " + std::to_string(dnnf_size));
        }

        return;
    } catch (std::runtime_error &e) {
        timeout = true;
        if(!controlOUTPUT) logger.logLine("DXD搜索超时: " + std::string(e.what()));
        return;
    }

}

void DanceDNNF::startMultiThreadDXD() {

    logger.logLine("开始多线程DXD搜索...");
    
    isParallelSearch = true;  // 开启多线程搜索标志
    MAX_B_COUNT = 1;

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
        timeout = false;

        solutionCount = ResSols.toString();
        logger.logLine("Solutions: " + solutionCount);
    
        logger.logLine("Max Blocks: " + std::to_string(MAX_B_COUNT));
            
        size_t dnnf_size = countDNNFNodes();
        size_t zdd_size = countZDDSize();

        if(dxz_mode) {
            logger.logLine("ZDD Size: " + std::to_string(zdd_size));
        } else {
            logger.logLine("DNNF Size: " + std::to_string(dnnf_size));
        }
        return;
    } catch (std::runtime_error &e) {
        timeout = true;
        logger.logLine("DXD搜索超时: " + std::string(e.what()));
        return;
    }
}

DNNFResult DanceDNNF::parallelSearchMDLX(vector<Block>& blocks) {
    const int n = blocks.size();
    
    // 并行处理每个子块
    std::vector<DNNFResult> results(n);
    std::atomic<bool> has_timeout(false);
    std::atomic<bool> has_failure(false);
    
    std::vector<std::unique_ptr<splaytree::EulerTourTree>> extracted(n);
    std::vector<std::unique_ptr<splaytree::EulerTourTree>> returned(n);
    
    for (int i = 0; i < n; ++i)
        extracted[i] = std::move(components[i]);
    components.clear();

    #pragma omp parallel for
    for (int i = 0; i < n; i++) {
        // 提前检查超时标志
        if (has_timeout.load(std::memory_order_acquire) ||
             has_failure.load(std::memory_order_acquire)) {
            returned[i] = std::move(extracted[i]);
            continue;
        }
        
        try {
            initThreadLocalState(blocks[i], std::move(extracted[i]));
            vector<int> threadSols;
            
            // 递归调用MDLX（每个线程独立计数）
            auto result = MDLX(threadSols, blocks[i]);

            if (result.isZero()) {
                has_failure.store(true, std::memory_order_release);
            } else {
                results[i] = result;
            }
            
        } catch (const std::runtime_error& e) {
            // 捕获超时异常
            if (std::string(e.what()).find("Time out") != std::string::npos) {
                has_timeout.store(true, std::memory_order_release);
            } else {
                has_failure.store(true, std::memory_order_release);
            }
        } catch (...) {
            has_failure.store(true, std::memory_order_release);
        }

        cleanupThreadLocalState();
    }

    components.resize(n);
    for (int i = 0; i < n; ++i)
        components[i] = std::move(returned[i]);
    
    // 检查超时（在主线程重新抛出）
    if (has_timeout.load()) {
        throw std::runtime_error("Time out");
    }

    // 检查失败（在主线程重新抛出）
    if (has_failure.load()) {
        return DNNFResult(0);
    }
    
    // 合并结果：计算所有子块解的笛卡尔积
    DNNFResult totalCount(1);
    for (const auto& result : results) {
        totalCount = totalCount * result;
    }
    
    return totalCount;
}

DNNFResult DanceDNNF::MDLX(vector<int>& sols, Block& block) {

    if (timer.timeBoundBroken()) {
        throw std::runtime_error("Time out");
    }

    if( block.cols.empty() ) {
        return DNNFResult(1);
    }
    
    if(isGraphSyncEnabled() && block.rows.size() >= 2) {

        vector<Block> curBlock = getComponentsByETT();

        MAX_B_COUNT = std::max(MAX_B_COUNT, curBlock.size());
        if (curBlock.size() > 1) {
            
            turnOffGraphSync(); // 关闭图同步，提升性能
            // p_count.fetch_add(1);

            // 多线程搜索
            auto parallelCount = parallelSearchMDLX(curBlock);
            return parallelCount;
        } 
    }

    ColumnHeader* choose = selectOptimalColumn(block.cols);
    if( !choose || choose->size <= 0 ) {
        return DNNFResult(0);  
    }

    DNNFResult totalResult = DNNFResult(0);

    set<int> deleted_rows;
    coverInBlock( choose->col, block, deleted_rows );
    DecUpdateCC(deleted_rows);
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
        DecUpdateCC(deleted_rows_);

        sols.push_back(curC->row + 1); 
        // 递归搜索
        auto result = MDLX(sols, block);
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
        IncUpdateCC(deleted_rows_);
        curC = curC->down;  
    }  
    uncoverInBlock( choose->col, block );  
    IncUpdateCC(deleted_rows);
    return totalResult; 
}

void DanceDNNF::start_MDLX_Search() {

    logger.logLine("开始多线程DLX搜索...");

    p_count = 0;
    MAX_B_COUNT = 1;
    
    try {
        vector<int> sols;

        timer.reset();
        timer.markStartTime();
        auto start = std::chrono::high_resolution_clock::now();
        auto res = MDLX(sols, InitBlock);
        auto end = std::chrono::high_resolution_clock::now();
        timer.markStopTime();

        searchTime = std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count();
        logger.logLine("Time: " + std::to_string(searchTime) + " s");
        timeout = false;

        solutionCount = res.toString();
        logger.logLine("Solutions: " + solutionCount);

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

