#include "../include/DXD.h"
#include "../include/ConnectedGraph.h"

void DanceDNNF::batchCover(const std::vector<int>& columns) {
    CoverOperation batchOp;
    batchOp.columns = columns; // 标记为批量操作
    
    for(int c : columns) {
        ColumnHeader* col = getColumnHeader(c);
        
        // 列头节点连接信息
        batchOp.columnLinks.push_back({(ColumnHeader*)col->left, (ColumnHeader*)col->right});
        
        // 从链表中移除列头
        col->right->left = col->left;
        col->left->right = col->right;
        // removeColumn(c);

        // 覆盖该列中的所有行
        Node* curR, *curC = col->down;
        while(curC != col) {
            Node* noteR = curC;
            curR = noteR->right;
            while(curR != noteR) {
                curR->down->up = curR->up;
                curR->up->down = curR->down;
                decColSize(curR->col);
                batchOp.coveredNodes.push_back(curR);
                curR = curR->right;
            }
            curC = curC->down;
        }
    }

    
    operationStack.push(batchOp);
}

void DanceDNNF::batchUncover() {
    if(operationStack.empty()) return;
    
    CoverOperation batchOp = operationStack.top();
    operationStack.pop();
    
    // 步骤1: 逆序恢复所有被覆盖的节点
    // 这一步必须按照覆盖的逆序进行，以正确恢复双向链表
    for(auto it = batchOp.coveredNodes.rbegin(); 
        it != batchOp.coveredNodes.rend(); ++it) {
        Node* curR = *it;
        incColSize(curR->col);    // 恢复列的大小计数
        curR->down->up = curR;          // 恢复下方节点的上指针
        curR->up->down = curR;          // 恢复上方节点的下指针
    }
    
    // 步骤2: 逆序恢复列头的链接
    // 必须逆序，因为cover时是正序进行的
    for(int i = batchOp.columns.size() - 1; i >= 0; --i) {
        int colIndex = batchOp.columns[i];
        ColumnHeader* col = getColumnHeader(colIndex);
        
        // 从保存的链接信息中恢复
        ColumnHeader* leftCol = batchOp.columnLinks[i].first;
        ColumnHeader* rightCol = batchOp.columnLinks[i].second;
        
        // 将当前列重新插入到列头链表中
        leftCol->right = col;           // 左邻居指向当前列
        rightCol->left = col;           // 右邻居指向当前列
        col->left = leftCol;            // 当前列指向左邻居
        col->right = rightCol;          // 当前列指向右邻居s
    }
    
}

std::shared_ptr<ORNode> DanceDNNF::make_node(int row) {
    auto left = std::make_shared<ANDNode>(row);
    auto right = std::make_shared<ANDNode>();
    return std::make_shared<ORNode>(row, left, right);
}

std::shared_ptr<ORNode> DanceDNNF::Search(Node* curC) {
    
    int c = curC->col;
    if(curC == getColumnHeader(c)) {
        return std::make_shared<ORNode>(-2);
    }
    
    size_t state = getColumnState();
    // vector<Block> blocks = detectBlocks();
    // if(blocks.size() > 1){
    //     matrix_is_decomposed.insert({state, true});
    // } 
    
    // 检查缓存
    auto cacheIt = Cache.find(state);
    if(cacheIt != Cache.end()) {
        return cacheIt->second;
    }

    auto X = make_node(curC->row); 

    // 覆盖矩阵 递归
    std::vector<int> columnsToCover;
    columnsToCover.push_back(c);
    
    Node* curR = curC->right;
    while(curR != curC) {
        columnsToCover.push_back(curR->col);
        curR = curR->right;
    }
    
    // 批量覆盖
    batchCover(columnsToCover);


    // 先处理选择的情况
    if(isSolved()){
        // count++;
        X->left->next = std::make_shared<ORNode>(-1); // 找到解，返回T
    } else {
        ColumnHeader* choose = selectCol();  
        X->left->next = Search(choose->down); // 选择该行，递归搜索
    }
    
    // 恢复矩阵 回溯
    // 批量恢复
    batchUncover();


    X->right->next = Search(curC->down); // 不选择该行的情况
    
    
    Cache[state] = X;
    return X;
}

void DanceDNNF::startSearch(bool g)
{
    ColumnHeader* choose = selectCol();
    if( choose->size <= 0 ){
        std::cout << "没有可选列，无精确覆盖解，搜索结束。" << std::endl;
        return;  
    }
    auto start = std::chrono::high_resolution_clock::now();
    rootOR = Search(choose->down);
    auto end = std::chrono::high_resolution_clock::now();

    searchTimeSeconds = std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count();

    std::cout << "搜索完成，耗时: " << searchTimeSeconds << " 秒。" << std::endl;
}

void mergeSets(vector<set<int>>& merged_sets, const set<int>& new_set) {
    vector<size_t> to_erase_indices;
    set<int> merged = new_set;

    for (size_t i = 0; i < merged_sets.size(); ++i) {
        set<int> intersection;
        set_intersection(merged_sets[i].begin(), merged_sets[i].end(),
                              new_set.begin(), new_set.end(),
                              inserter(intersection, intersection.begin()));
        if (!intersection.empty()) {
            merged.insert(merged_sets[i].begin(), merged_sets[i].end());
            to_erase_indices.push_back(i);
        }
    }

    // 删除被合并的集合（从后往前删除避免下标混乱）
    for (auto it = to_erase_indices.rbegin(); it != to_erase_indices.rend(); ++it) {
        merged_sets.erase(merged_sets.begin() + *it);
    }

    merged_sets.push_back(std::move(merged));
}

// 合并相交的集合
vector<set<int>> DanceDNNF::mergeRowSets(Block& block){
    
    vector<set<int>> merged_sets;

    for(int col : block.cols){
        ColumnHeader* colHead = getColumnHeader(col);
        
        if(merged_sets.empty()){
            merged_sets.push_back(colHead->rows);
            continue;
        }

        mergeSets(merged_sets, colHead->rows);
    }

    return merged_sets;
}

vector<Block> DanceDNNF::spilitBlock(const vector<set<int>>& mergeRowSets){
    
    vector<Block> blocks;
    for(int i = 0; i < mergeRowSets.size(); ++i) {
        const set<int>& rows = mergeRowSets[i];
        set<int> cols;
        // 获取所有行涉及的列的并集
        for(int row : rows) {
            const RowNode* rowHead = getRowHeader(row);
            cols.insert(rowHead->cols.begin(), rowHead->cols.end());
        }

        // 过滤：只保留在当前行集合中实际存在的列
        set<int> validCols;
        for(int col : cols) {
            const ColumnHeader* colHead = getColumnHeader(col);
            set<int> intersection;
            set_intersection(rows.begin(), rows.end(),
                           colHead->rows.begin(), colHead->rows.end(),
                           inserter(intersection, intersection.begin()));
            if(!intersection.empty()) {
                validCols.insert(col);
            }
        }
        Block newBlock(rows, cols);
        blocks.push_back(newBlock);
    }
    return blocks;
}

vector<Block> DanceDNNF::spilitBlockParallel(const vector<set<int>>& mergeRowSets) {
   
    vector<std::future<Block>> futures;
    
    // 创建异步任务
    for(const auto& rows : mergeRowSets) {
        futures.push_back(std::async(std::launch::async, [this, rows]() -> Block {
            set<int> cols;
            
            // 获取所有行涉及的列的并集
            for(int row : rows) {
                const RowNode* rowHead = getRowHeader(row);
                cols.insert(rowHead->cols.begin(), rowHead->cols.end());
            }
            
            // 过滤：只保留在当前行集合中实际存在的列
            set<int> validCols;
            for(int col : cols) {
                const ColumnHeader* colHead = getColumnHeader(col);
                set<int> intersection;
                set_intersection(rows.begin(), rows.end(),
                               colHead->rows.begin(), colHead->rows.end(),
                               inserter(intersection, intersection.begin()));
                if(!intersection.empty()) {
                    validCols.insert(col);
                }
            }
            
            return Block(rows, validCols);
        }));
    }
    
    // 收集结果
    vector<Block> blocks;
    blocks.reserve(futures.size());
    for(auto& future : futures) {
        blocks.push_back(future.get());
    }
    
    return blocks;

}

vector<DXD_Block> DanceDNNF::detectBlocks(const DXD_Block& currentBlock) {
        // 使用BFS找连通分量
        unordered_set<int> visitedRows;
        unordered_set<int> visitedCols;
        vector<unordered_set<int>> tmp_blocks;  // 临时块，放连通分量的列id
        vector<DXD_Block> blocks;

        for (const auto& [startRow, _] : currentBlock.rowToCols) {
            if (visitedRows.count(startRow)) continue;

            // unordered_set<int> blockRows;
            unordered_set<int> blockCols;
            queue<pair<bool, int>> q; // true: row, false: col
            q.emplace(true, startRow);

            while (!q.empty()) {
                auto [isRow, id] = q.front();
                q.pop();

                if (isRow) {
                    if (visitedRows.count(id)) continue;
                    visitedRows.insert(id);
                    // blockRows.insert(id);

                    // 安全访问映射
                    auto it = currentBlock.rowToCols.find(id);
                    if (it != currentBlock.rowToCols.end()) {
                        for (int col : it->second) {
                            if (!visitedCols.count(col)) {
                                q.emplace(false, col);
                            }
                        }
                    }
                } else {
                    if (visitedCols.count(id)) continue;
                    visitedCols.insert(id);
                    blockCols.insert(id);

                    // 安全访问映射
                    auto it = currentBlock.colToRows.find(id);
                    if (it != currentBlock.colToRows.end()) {
                        for (int row : it->second) {
                            if (!visitedRows.count(row)) {
                                q.emplace(true, row);
                            }
                        }
                    }
                }
            }

            if (blockCols.size() >= 1) {
                tmp_blocks.push_back(blockCols);
            }
        }

        if(tmp_blocks.size() > 1){
            for(auto &blockCols: tmp_blocks){
                unordered_map<int, set<int>> newRowToCols;
                unordered_map<int, set<int>> newColToRows;
                build_mapping_from_cols(blockCols, newRowToCols, newColToRows);
                blocks.push_back(DXD_Block(blockCols, newRowToCols, newColToRows));
            }
        }

        return blocks;
}


// 串行处理每个子块，组合为 分解 节点
DNNFResult DanceDNNF::serialSearch(vector<Block>& blocks) {

    DNNFResult totalResult(1);
    
    for (auto& b : blocks) {
        auto result = DXD(b);
        if (result.isZero()) {
            return DNNFResult(0);
        }
        
        totalResult = totalResult * result;
    }
    
    return totalResult;
}


shared_ptr<DNNFNode> DanceDNNF::parallelSearch(vector<Block>& blocks) {

    std::vector<std::future<std::shared_ptr<DNNFNode>>> futures;

    for (auto& block : blocks) {

        // futures.push_back(pool.enqueue([this, b = Block(block)]() mutable {
        //     return DXD(b);
        // }));
        // futures.push_back(async(launch::async, [this, block]() {  
        //     Block blockCopy = block;  // 或直接使用 block
        //     return DXD(blockCopy);
        // }));
    }

    // 收集结果并创建Decomposed节点
    auto andNode = std::make_shared<DNNFNode>(NodeType::Decomposed, -1, 1);

    for (auto& future : futures) {
        auto result = future.get();

        if (!result || result->label == -2) { 
            return F;
        }
        andNode->children.push_back(result);
        andNode->count *= result->count;
    }
    
    return andNode;
}

DNNFResult DanceDNNF::parallelSearchUseOmp(vector<Block>& blocks) {
    const int n = blocks.size();
    std::vector<DNNFResult> results(n);
    std::atomic<bool> has_failure(false);
    std::atomic<bool> has_timeout(false);
    
    
    #pragma omp parallel for schedule(dynamic) if(n > 4)
    for (int i = 0; i < n; i++) {
        if (has_failure.load(std::memory_order_acquire) || 
            has_timeout.load(std::memory_order_acquire)) {
            continue;
        }
        
        try {
            Block blockCopy = blocks[i];
            auto result = DXD(blockCopy);
            
            if (result.isZero()) {
                has_failure.store(true, std::memory_order_release);
            } else {
                results[i] = result;
            }
        } catch (const std::runtime_error& e) {
            if (std::string(e.what()).find("Time bound") != std::string::npos) {
                has_timeout.store(true, std::memory_order_release);
            } else {
                has_failure.store(true, std::memory_order_release);
            }
        } catch (...) {
            has_failure.store(true, std::memory_order_release);
        }
    }
    
    if (has_timeout.load()) {
        throw std::runtime_error("Time bound broken");
    }
    
    if (has_failure.load()) {
        return DNNFResult(0);
    }
    
    // 计算总计数
    DNNFResult totalResult(1);
    for (const auto& result : results) {
        totalResult = totalResult * result;
    }
    
    return totalResult;
}


// DXD单线程（要体现分解性）
DNNFResult DanceDNNF::DXD(Block& block) {
    
    if(timer.timeBoundBroken()) {
        throw std::runtime_error("Time bound broken");
    }
    
    if(block.cols.empty()) {
        return DNNFResult(1);
    } 

    // 先查缓存
    size_t state = hashBlockState(block.cols); 
    // shared_ptr<DNNFNode> cacheRes = getCache(state);
    // if(cacheRes != nullptr) {
    //     return cacheRes;
    // }
    bool success = true;
    auto cacheCount = getCacheCount(state, success);
    if (success) {
        return DNNFResult(cacheCount);
    }

    if(p_count < MAX_P_COUNT && block.rows.size() >= MIN_BLOCK_ROWS) {

        vector<Block> curBlock;
        if (useETT) {
            curBlock = getComponentsByETT(block.rows);
        } else if (useIG) {
            curBlock = getComponents(block.rows);
        }
        // if (debug) record_detect_count++;

        MAX_B_COUNT = std::max(MAX_B_COUNT, curBlock.size());
        if (curBlock.size() > 1) {
            // if (debug) {
            //     cout << "第 " << record_detect_count << " 次检测到块分解。" << endl;
            //     cout << "块分解为 " << curBlock.size() << " 个子块" << endl;
            //     int i = 1;
            //     for (auto& b : curBlock) {
            //         b.printBlock(i++);
            //     }
            // }
            turnOffGraphSync(); // 关闭图同步，提升性能

            // shared_ptr<DNNFNode> res_and_node;
            p_count++;

            if (isParallelSearch) {
                // res_and_node = parallelSearch(curBlock); 
                auto result = parallelSearchUseOmp(curBlock);
                // 缓存时只存储精确值或标记溢出
                if (!result.overflowed) {
                    setCacheCount(state, result.count);
                } else {
                    // 溢出的不缓存，或者存储特殊标记
                    setCacheCount(state, ULLONG_MAX);
                }
                return result;
            } else {
                auto result = serialSearch(curBlock);
                // 缓存时只存储精确值或标记溢出
                if (!result.overflowed) {
                    setCacheCount(state, result.count);
                } else {
                    // 溢出的不缓存，或者存储特殊标记
                    setCacheCount(state, ULLONG_MAX);
                }
                return result;
            }

        } 
    }

    ColumnHeader* choose = selectColumnHeuristic(block.cols); 

    if(!choose || choose->size <= 0) {
        setCacheCount(state, 0);
        return DNNFResult(0);
    }

    // 将choose列下的行节点作为Decision节点加入children
    // shared_ptr<DNNFNode> orNode = buildTree ? 
    //         make_shared<DNNFNode>(NodeType::OR, choose->col, 0) : nullptr;
    DNNFResult totalResult(0);

    coverInBlock(choose->col, block);
    Node* curC = choose->down;

    while(curC != choose) {
        
        Node* curR = curC->right;
        while (curR != curC) {
            coverInBlock(curR->col, block);
            curR = curR->right;
        }
 
        auto result = DXD(block);

        if(!result.isZero()) {
            totalResult = totalResult + result;
        }
        

        curR = curC->left;
        while (curR != curC) {
            uncoverInBlock(curR->col, block);
            curR = curR->left;
        }
        curC = curC->down;
    }
    uncoverInBlock(choose->col, block);

    // 插入缓存
    if (!totalResult.overflowed) {
        setCacheCount(state, totalResult.count);
    } else {
        setCacheCount(state, ULLONG_MAX);
    }

    return totalResult;
}


void DanceDNNF::startDXD() {

    logger.logLine("开始单线程DXD搜索...");

    p_count = 0;
    isParallelSearch = false; // 单线程搜索
    MAX_B_COUNT = 1;
    
    try{

        timer.reset();
        timer.markStartTime();
        auto start = std::chrono::high_resolution_clock::now();
        auto ResSols = DXD(InitBlock);  
        auto end = std::chrono::high_resolution_clock::now();
        timer.markStopTime();

        searchTime = std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count();
        logger.logLine("Time: " + std::to_string(searchTime) + " s");
        timeout = false;

        solutionCount = ResSols.toString();
        logger.logLine("Solutions: " + solutionCount);
    
        logger.logLine("Max Blocks: " + std::to_string(MAX_B_COUNT));

        return;
    } catch (std::runtime_error &e) {
        timeout = true;
        logger.logLine("DXD搜索超时: " + std::string(e.what()));
        return;
    }

}

void DanceDNNF::startMultiThreadDXD() {

    logger.logLine("开始多线程DXD搜索...");
    
    p_count = 0;
    isParallelSearch = true;  // 开启多线程搜索标志
    MAX_B_COUNT = 1;

    try {

        timer.reset();
        timer.markStartTime();
        auto start = std::chrono::high_resolution_clock::now();
        auto ResSols = DXD(InitBlock);  // 多线程DXD搜索
        auto end = std::chrono::high_resolution_clock::now();
        timer.markStopTime();
   
        searchTime = std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count();
        logger.logLine("Time: " + std::to_string(searchTime) + " s");
        timeout = false;

        solutionCount = ResSols.toString();
        logger.logLine("Solutions: " + solutionCount);
    
        logger.logLine("Max Blocks: " + std::to_string(MAX_B_COUNT));

        return;
    } catch (std::runtime_error &e) {
        timeout = true;
        logger.logLine("DXD搜索超时: " + std::string(e.what()));
        return;
    }
}

uint64_t DanceDNNF::parallelSearchMDLX(vector<Block>& blocks) {
    const int n = blocks.size();
    
    // 并行处理每个子块
    std::vector<long long> results(n, 0);
    std::atomic<bool> has_timeout(false);
    
    #pragma omp parallel for schedule(dynamic) if(n > 2)
    for (int i = 0; i < n; i++) {
        // 提前检查超时标志
        if (has_timeout.load(std::memory_order_acquire)) {
            results[i] = 0;
            continue;
        }
        
        try {
            // 每个线程使用block的副本和独立的sols
            Block blockCopy = blocks[i];
            vector<int> threadSols;
            uint64_t threadCount = 0;
            
            // 递归调用MDLX（每个线程独立计数）
            MDLX(threadSols, threadCount, blockCopy);
            
            results[i] = threadCount;
            
        } catch (const std::runtime_error& e) {
            // 捕获超时异常
            if (std::string(e.what()).find("Time out") != std::string::npos) {
                has_timeout.store(true, std::memory_order_release);
            }
            results[i] = 0;
        } catch (...) {
            // 捕获其他异常
            results[i] = 0;
        }
    }
    
    // 检查超时（在主线程重新抛出）
    if (has_timeout.load()) {
        throw std::runtime_error("Time out");
    }
    
    // 合并结果：计算所有子块解的笛卡尔积
    long long totalCount = 1;
    for (int i = 0; i < n; i++) {
        if (results[i] == 0) {
            // 如果某个子块无解，整体无解
            return 0;
        }
        
        // 检查乘法溢出
        if (totalCount > LLONG_MAX / results[i]) {
            // 发生溢出，返回错误标记
            return -1;
        }
        
        totalCount *= results[i];
    }
    
    return totalCount;
}

void DanceDNNF::MDLX(vector<int>& sols, uint64_t& count, Block& block) {

    if (timer.timeBoundBroken()) {
        throw std::runtime_error("Time out");
    }

    if( block.cols.empty() ) {
        count++;
        return;
    } 

    if(p_count < MAX_P_COUNT && block.rows.size() >= MIN_BLOCK_ROWS) {

        vector<Block> curBlock;
        if (useETT) {
            curBlock = getComponentsByETT(block.rows);
        } else if (useIG) {
            curBlock = getComponents(block.rows);
        }

        MAX_B_COUNT = std::max(MAX_B_COUNT, curBlock.size());
        if (curBlock.size() > 1) {

            // 多线程搜索
            uint64_t parallelCount = parallelSearchMDLX(curBlock);
            if (parallelCount >= 0) {
                count += parallelCount;
                return;
            }
            // 如果并行搜索失败（返回-1），继续串行搜索
        } 
    }

    ColumnHeader* choose = selectColumnHeuristic(block.cols);
    if( !choose || choose->size <= 0 ) {
        return;  
    }


    coverInBlock( choose->col, block );
    Node* curC = choose->down;  
    while( curC != choose )  
    {  
         
        Node* curR = curC->right;  
        while( curR != curC )  
        {  
            coverInBlock( curR->col, block );  
            curR = curR->right;  
        }  

        sols.push_back(curC->row + 1); 
        // 递归搜索
        MDLX(sols, count, block);
       
        sols.pop_back();  // 回溯，移除当前行
        curR = curC->left;  
        while( curR != curC )  
        {  
            uncoverInBlock( curR->col, block );  
            curR = curR->left;  
        }  
        curC = curC->down;  
    }  
    uncoverInBlock( choose->col, block );  
    return; 
}

void DanceDNNF::start_MDLX_Search() {

    logger.logLine("开始多线程DLX搜索...");

    p_count = 0;
    
    try{
        uint64_t count = 0; 
        vector<int> sols;

        timer.reset();
        timer.markStartTime();
        auto start = std::chrono::high_resolution_clock::now();
        MDLX(sols, count, InitBlock);
        auto end = std::chrono::high_resolution_clock::now();
        timer.markStopTime();
        timer.reset();
        timeout = false;

        searchTimeSeconds = std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count();
        searchTime = searchTimeSeconds;
        logger.logLine("Time: " + std::to_string(searchTimeSeconds) + " s");

        logger.logLine("Solutions: " + std::to_string(count));
        solutionCount = count;

        logger.logLine("Max Blocks: " + std::to_string(MAX_B_COUNT));
        return;
    } catch (std::runtime_error &e) {
        timeout = true;
        logger.logLine("MDLX搜索超时: " + std::string(e.what()));
        return;
    } 
}


shared_ptr<DNNFNode> DecisionDNNF::solveSingle(DancingMatrix& matrix, unordered_map<size_t, shared_ptr<DNNFNode>>& localCache)
{
    if (matrix.isSolved()) {
        return T; // 所有列都被覆盖，找到一个解
    }

    size_t state = matrix.getColumnState();
    auto cacheIt = localCache.find(state);
    if (cacheIt != localCache.end()) {
        return cacheIt->second; 
    }

    ColumnHeader* chosenCol = matrix.selectCol();

    if (chosenCol->size <= 0) {
        return F; // 没有可选列，返回F
    }

    auto orNode = make_shared<DNNFNode>(NodeType::OR, chosenCol->col, 0);

    matrix.cover(chosenCol->col);
    Node* curC = chosenCol->down;
    while (curC != chosenCol) {
        Node* curR = curC->right;
        while (curR != curC) {
            matrix.cover(curR->col);
            curR = curR->right;
        }

        auto node = solveSingle(matrix, localCache); // 递归左分支

        if (node->label != -2) {
            // auto var = make_shared<DNNFNode>(NodeType::Variable, curC->row + 1);
            auto and_node = make_shared<DNNFNode>(curC->row + 1, node, node->count);
            orNode->count += node->count; // 累加当前Decision节点的计数
            orNode->children.push_back(and_node);
        }

        curR = curC->left;
        while (curR != curC) {
            matrix.uncover(curR->col);
            curR = curR->left;     
        }

        curC = curC->down;
    }
    matrix.uncover(chosenCol->col);

    if (orNode->children.empty()) {
        orNode = F;
    }

    localCache[state] = orNode; // 缓存结果
    return orNode;
}

// 多线程并发搜索独立子矩阵
shared_ptr<DNNFNode> DecisionDNNF::solve() {

    vector<std::future<shared_ptr<DNNFNode>>> futures;
    // futures.reserve(matrices.size());

    // for (auto& matrixPtr : matrices) {
    //     // 使用move避免拷贝，每个线程获得独立所有权
    //     futures.push_back(pool.enqueue([this](unique_ptr<DancingMatrix> matrix) {
    //         unordered_map<size_t, shared_ptr<DNNFNode>> localCache;
    //         return solveSingle(*matrix, localCache);
    //     }, std::move(matrixPtr)));
    // }

    // auto rootNode = make_shared<DNNFNode>();
    // rootNode->count = 0;

    // for (auto& future : futures) {
    //     auto result = future.get();
    //     rootNode->children.push_back(result);
    //     rootNode->count += result->count;
    // }

    // Semaphore semaphore(maxConcurrentMatrices);  // 控制并发数
        
    for (const auto& task : tasks) {
        auto future = pool.enqueue([this, task]() -> shared_ptr<DNNFNode> {
            // semaphore.acquire();  // 获取资源
            
            try {
                // 按需创建矩阵
                auto matrix = make_unique<DancingMatrix>(task.input_file, task.from);
                
                // 应用行选择和列覆盖
                applyRowSelection(*matrix, task.selectedRow);
                
                unordered_map<size_t, shared_ptr<DNNFNode>> localCache;
                auto result = solveSingle(*matrix, localCache);
                
                // 矩阵使用完立即释放
                matrix.reset();
                
                // semaphore.release();  // 释放资源
                return result;
            } catch (const std::exception& e) {
                // semaphore.release();
                throw std::runtime_error(e.what());
            }
        });
        futures.push_back(std::move(future));
    }
    
    // 合并结果
    return mergeResults(futures);
}

shared_ptr<ExperimentResult> ExactCoverSolver::searchEC() {
    cur_result->instance_name = cur_instance;
    
    try{
        logger.logLine("最大线程数: " + to_string(poolSize));

        // DancingMatrix dm(input_file, from);

        // getClosedSizeCol(poolSize)
        col_id selectCol;
        auto assignRows = getAssignCol(selectCol);
        int size = assignRows.size();
        // ColumnHeader* colHead = dm.getColumnHeader(selectCol);
        std::cout << "选择列: " << selectCol << " size: " << size << std::endl;

        vector<SubMatrixTask> tasks;
        tasks.reserve(size);
        
        for (auto& row : assignRows) {
            tasks.emplace_back(row, input_file, from);

        }


        int maxConcurrent = std::min(poolSize, size);  // 最多同时4个矩阵
        DecisionDNNF solver(std::move(tasks), maxConcurrent);

        auto start = std::chrono::high_resolution_clock::now();
        shared_ptr<DNNFNode> result = solver.solve();
        auto end = std::chrono::high_resolution_clock::now();

        double elapsedSeconds = std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count();
        // cout << "搜索完成，耗时: " << elapsedSeconds << " 秒。" << endl;
        logger.logLine("多线程DXD搜索完成, 耗时: " + to_string(elapsedSeconds) + " 秒。");
        cur_result->runtime = to_string(elapsedSeconds);
        // cout << "搜索到的解个数: " << result->count << endl;
        logger.logLine("搜索到的解个数: " + to_string(result->count));
        cur_result->solution_count = result->count;

        return cur_result;
    } catch (const std::exception& e) {
        cur_result->runtime = "failed";
        logger.logLine(string(e.what()));
        return cur_result;
    }

}

const std::vector<row_id>& ExactCoverSolver::getAssignCol(col_id& selectedCol) { 
    
    if (matrix.size() <= 1) {
        throw std::runtime_error("矩阵未初始化");
    }
    
    // 使用标准算法查找最小元素
    auto min_it = std::min_element(
        matrix.begin() + 1, 
        matrix.end(),
        [](const std::vector<row_id>& a, const std::vector<row_id>& b) {
            return a.size() < b.size();
        }
    );
    
    selectedCol = std::distance(matrix.begin(), min_it);
    return *min_it;
}

// 主搜索函数(多线程版本)
shared_ptr<DNNFNode> DanceDNNF::parallelDXD(Block& block) {
    if (block.cols.empty()) {
        return T; 
    }

    size_t state = hashBlockState(block.cols); // 编码当前块状态

    // 检查缓存
    auto cachedResult = getCache(state);
    if (cachedResult) {
        return cachedResult;
    }

    // if(block.cols.size() < MAX_BLOCK_COLS && detect_records.find(state) == detect_records.end()) {

    //     vector<DXD_Block> blocks = detectBlocks(block);

    //     if(blocks.size() > 1){
    //         p_count++; // 分解成功次数
    //         auto res_and_node = parallelSearch(blocks); // 多线程实现不同块搜索
    //         setCachedResult(state, res_and_node); // 缓存结果
    //         return res_and_node; // 返回分解节点
    //     }else{
    //         detect_records.insert(state);
    //     }
    // }
    
    ColumnHeader* selected_col = selectColumnHeuristic(block.cols);  

    if(selected_col->size <= 0) {
        return F; // 如果没有可选列，返回F
    }

    auto orNode = make_shared<DNNFNode>(NodeType::OR, selected_col->col, 0);

    coverInBlock(selected_col->col, block); 
    Node* curC = selected_col->down;
    while(curC != selected_col) {

        Node* noteR = curC; 
        Node* curR = noteR->right; 
        while(curR != noteR) {
            coverInBlock(curR->col, block); 
            curR = curR->right; 
        }

        auto node = parallelDXD(block); // 递归左分支

        if(node->label != -2){
            auto it = V_Table.find(curC->row);
            shared_ptr<DNNFNode> var;
            if (it != V_Table.end()) {
                var = it->second;
            } else {
                var = make_shared<DNNFNode>(NodeType::Variable, curC->row + 1);
                V_Table[curC->row] = var;
            }
            auto and_node = make_shared<DNNFNode>(NodeType::Decision, var, node);
            and_node->count = node->count;
            orNode->count += and_node->count; // 累加当前Decision节点的计数
            orNode->children.push_back(and_node);
        }
        
        
        curR = noteR->left; 
        while(curR != noteR) {
            uncoverInBlock(curR->col, block); 
            curR = curR->left; 
        }
        curC = curC->down;
    }
    uncoverInBlock(selected_col->col, block);

    if(orNode->children.empty()) {
        orNode = F;
    }   
    // 插入缓存
    setCache(state, orNode);
    return orNode;
}


// 批量处理舞蹈链，并更新block状态
void DanceDNNF::batchCoverInBlock(Node* curC, Block& block) 
{
    BatchOperation batchOp;

    Node* curNode = curC;
    Node* startNode = curNode;
    do {  // 遍历该行所有列
        ColumnHeader* col = getColumnHeader(curNode->col);
        
        // 从链表中移除列头
        col->right->left = col->left;
        col->left->right = col->right;

        block.cols.erase(col->col); // 从块中移除列

        Node* rowNode = col->down, *curR;
        while(rowNode != col){  // 遍历列中的行节点
            Node* noteR = rowNode;
            curR = noteR->right;
            while(curR != noteR){
                curR->down->up = curR->up;
                curR->up->down = curR->down;
                decColSize(curR->col);

                // 更新列头和行头的元素集合
                // ColIndex[curR->col].rows.erase(curR->row);  // 从列的行集合中移除该行
                // RowIndex[curR->row].cols.erase(curR->col);  // 从行的列集合中移除该列

                curR = curR->right;
            }

            // 更新block信息
            // block.rows.erase(rowNode->row); // 从块中移除行 
            
            rowNode = rowNode->down;
        }

        curNode = curNode->right; // 移动到下一个列
    }while(curNode != startNode);

    block.is_spilited = false;  // 重置块状态
    // batchOperationStack.push(batchOp);
    batchOpStack.push_back(batchOp);
}

// 批量恢复舞蹈链和块
void DanceDNNF::batchUncoverInBlock(Block& block)
{ 
    if(batchOpStack.empty()) return;

    BatchOperation batchOp = batchOpStack.back();
    batchOpStack.pop_back();

    for(auto it = batchOp.coveredRows.rbegin(); 
    it != batchOp.coveredRows.rend(); ++it) {
        Node* curR = getRowHeader(*it);
        incColSize(curR->col);     // 恢复列的大小计数
        curR->down->up = curR;          // 恢复下方节点的上指针
        curR->up->down = curR;          // 恢复上方节点的下指针

        // 恢复列头和行头的元素集合
        // ColIndex[curR->col].rows.insert(curR->row);  // 恢复列的行集合
        // RowIndex[curR->row].cols.insert(curR->col);  // 恢复行的列集合
    }

    for(int i = batchOp.coveredCols.size() - 1; i >= 0; --i) {
        int colIndex = batchOp.coveredCols[i];
        ColumnHeader* col = getColumnHeader(colIndex);
        
        
        // 将当前列重新插入到列头链表中
        col->left->right = col;           // 左邻居指向当前列
        col->right->left = col;           // 右邻居指向当前列
    }

}

