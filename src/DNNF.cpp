#include "../include/DancingMatrix.h"

void DancingMatrix::batchCover(const std::vector<int>& columns) {
    CoverOperation batchOp;
    batchOp.columns = columns; // 标记为批量操作
    
    for(int c : columns) {
        ColunmHeader* col = &ColIndex[c];
        
        // 列头节点连接信息
        batchOp.columnLinks.push_back({(ColunmHeader*)col->left, (ColunmHeader*)col->right});
        
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
                --ColIndex[curR->col].size;
                batchOp.coveredNodes.push_back(curR);
                curR = curR->right;
            }
            curC = curC->down;
        }
    }

    
    operationStack.push(batchOp);
}

void DancingMatrix::batchUncover() {
    if(operationStack.empty()) return;
    
    CoverOperation batchOp = operationStack.top();
    operationStack.pop();
    
    // 步骤1: 逆序恢复所有被覆盖的节点
    // 这一步必须按照覆盖的逆序进行，以正确恢复双向链表
    for(auto it = batchOp.coveredNodes.rbegin(); 
        it != batchOp.coveredNodes.rend(); ++it) {
        Node* curR = *it;
        ++ColIndex[curR->col].size;     // 恢复列的大小计数
        curR->down->up = curR;          // 恢复下方节点的上指针
        curR->up->down = curR;          // 恢复上方节点的下指针
    }
    
    // 步骤2: 逆序恢复列头的链接
    // 必须逆序，因为cover时是正序进行的
    for(int i = batchOp.columns.size() - 1; i >= 0; --i) {
        int colIndex = batchOp.columns[i];
        ColunmHeader* col = &ColIndex[colIndex];
        
        // 从保存的链接信息中恢复
        ColunmHeader* leftCol = batchOp.columnLinks[i].first;
        ColunmHeader* rightCol = batchOp.columnLinks[i].second;
        
        // 将当前列重新插入到列头链表中
        leftCol->right = col;           // 左邻居指向当前列
        rightCol->left = col;           // 右邻居指向当前列
        col->left = leftCol;            // 当前列指向左邻居
        col->right = rightCol;          // 当前列指向右邻居s
    }
    
}

std::shared_ptr<ORNode> DancingMatrix::make_node(int row) {
    auto left = std::make_shared<ANDNode>(row);
    auto right = std::make_shared<ANDNode>();
    return std::make_shared<ORNode>(row, left, right);
}

std::shared_ptr<ORNode> DancingMatrix::Search(Node* curC) {
    
    int c = curC->col;
    if(curC == &ColIndex[c]) {
        return std::make_shared<ORNode>(-2);
    }
    
    string state = getColumnState();
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
    if(root->right == root){
        // count++;
        X->left->next = std::make_shared<ORNode>(-1); // 找到解，返回T
    } else {
        ColunmHeader* choose = selectCol();  
        X->left->next = Search(choose->down); // 选择该行，递归搜索
    }
    
    // 恢复矩阵 回溯
    // 批量恢复
    batchUncover();


    X->right->next = Search(curC->down); // 不选择该行的情况
    
    
    Cache[state] = X;
    return X;
}

void DancingMatrix::startSearch(bool g)
{
    ColunmHeader* choose = selectCol();
    if( choose->size <= 0 ){
        std::cout << "没有可选列，无精确覆盖解，搜索结束。" << std::endl;
        return;  
    }
    auto start = std::chrono::high_resolution_clock::now();
    rootOR = Search(choose->down);
    auto end = std::chrono::high_resolution_clock::now();

    searchTimeSeconds = std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count();
    countSolutions(rootOR);
    std::cout << "搜索完成，耗时: " << searchTimeSeconds << " 秒。" << std::endl;
    std::cout << "搜索到的解个数: " << count << std::endl;
}

void DancingMatrix::printSolution() {
    std::cout << "搜索到的解为: " << std::endl;
    for(auto& solution : solutions) {
        for(auto& s : solution) {
            std::cout << s << " ";
        }
        std::cout << std::endl;
    }
}

// DXD单线程（要体现分解性）
shared_ptr<DNNFNode> DancingMatrix::singleDXD(DXD_Block& block) {
    
    if(block.cols.empty()) {
        return T; // 如果没有列，返回T
    } 

    // 先查缓存
    // string state = encodeBlockState(block.cols);
    size_t state = hashBlockState(block.cols); // 编码当前块状态
    auto cacheResult = getInSingleCache(state);
    if(cacheResult) {
        return cacheResult;
    }


    if(block.cols.size() < MAX_BLOCK_COLS && detect_records.find(state) == detect_records.end()) {

        vector<DXD_Block> blocks = detectBlocks(block);

        if(blocks.size() > 1){
            auto res_and_node = singleSearch(blocks); // 单线程实现不同块搜索
            setInSingleCache(state, res_and_node); // 缓存结果
            return res_and_node; // 返回分解节点
        }else{
            detect_records.insert(state);
        }
    }

    ColunmHeader* choose = selectColumnHeuristic(block.cols);  

    if(choose->size <= 0) {
        return F; // 如果没有可选列，返回F
    }

    // 将choose列下的行节点作为Decision节点加入children
    auto orNode = make_shared<DNNFNode>(NodeType::OR, choose->col, 0);

    coverInDXDBlock(choose->col, block); 
    Node* curC = choose->down;
    while(curC != choose) {
        // 递归处理每个Decision节点
        Node* noteR = curC; 
        Node* curR = noteR->right; 
        while(curR != noteR) {
            coverInDXDBlock(curR->col, block); 
            curR = curR->right; 
        }
        // batchCoverInBlock(curC, block);
 
        auto node = singleDXD(block); // 递归左分支

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
            uncoverInDXDBlock(curR->col, block); 
            curR = curR->left; 
        }
        // batchUncoverInBlock(block);
        curC = curC->down;
    }
    uncoverInDXDBlock(choose->col, block); 

    if(orNode->children.empty()) {
        orNode = F;
    }   
    // 插入缓存
    setInSingleCache(state, orNode);
    return orNode; 
}


void DancingMatrix::startSingleDXD() {

    std::cout << "开始单线程DXD搜索..." << std::endl;
    // Block block(rowsSet, colsSet);
    // initBlock(block); // 初始化块 

    DXD_Block dxd_block(colsSet, rowToColsSet, colToRowsSet);

    auto start = std::chrono::high_resolution_clock::now();
    rootDNNF = singleDXD(dxd_block); // 执单线程DXD搜索
    auto end = std::chrono::high_resolution_clock::now();

    std::cout << "搜索到的解个数: " << rootDNNF->count << std::endl;
    
    searchTimeSeconds = std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count();
    std::cout << "单线程DXD搜索完成, 耗时: " << searchTimeSeconds << " 秒。" << std::endl;
    std::cout << std::endl; 

    // vector<int> solution;
    // traverseDNNF(rootDNNF, solution);
    // printSolution();
}

// 主搜索函数(多线程版本)
shared_ptr<DNNFNode> DancingMatrix::parallelDXD(DXD_Block& block) {
    if (block.cols.empty()) {
        return T; 
    }

    // string stateHash = encodeBlockState(block.cols); // 编码当前块状态
    size_t state = hashBlockState(block.cols); // 编码当前块状态

    // 检查缓存
    auto cachedResult = getCachedResult(state);
    // auto cachedResult = getCachedResult(hashBlockState(block.cols));
    if (cachedResult) {
        return cachedResult;
    }

    if(block.cols.size() < MAX_BLOCK_COLS && detect_records.find(state) == detect_records.end()) {

        vector<DXD_Block> blocks = detectBlocks(block);

        if(blocks.size() > 1){
            p_count++; // 分解成功次数
            auto res_and_node = parallelSearch(blocks); // 多线程实现不同块搜索
            setCachedResult(state, res_and_node); // 缓存结果
            return res_and_node; // 返回分解节点
        }else{
            detect_records.insert(state);
        }
    }
    
    ColunmHeader* selected_col = selectColumnHeuristic(block.cols);  

    if(selected_col->size <= 0) {
        return F; // 如果没有可选列，返回F
    }

    auto orNode = make_shared<DNNFNode>(NodeType::OR, selected_col->col, 0);

    coverInDXDBlock(selected_col->col, block); 
    Node* curC = selected_col->down;
    while(curC != selected_col) {

        // batchCoverInBlock(curC, block);
        Node* noteR = curC; 
        Node* curR = noteR->right; 
        while(curR != noteR) {
            coverInDXDBlock(curR->col, block); 
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
        
        // batchUncoverInBlock(block);
        curR = noteR->left; 
        while(curR != noteR) {
            uncoverInDXDBlock(curR->col, block); 
            curR = curR->left; 
        }
        curC = curC->down;
    }
    uncoverInDXDBlock(selected_col->col, block);

    if(orNode->children.empty()) {
        orNode = F;
    }   
    // 插入缓存
    setCachedResult(state, orNode);
    return orNode;
}


void DancingMatrix::startMultiThreadDXD() {

    std::cout << "开始多线程DXD搜索..." << std::endl;
    // Block block(rowsSet, colsSet);
    // initBlock(block); // 初始化块 

    DXD_Block dxd_block(colsSet, rowToColsSet, colToRowsSet);
    
    auto start = std::chrono::high_resolution_clock::now();
    rootDNNF = parallelDXD(dxd_block); // 多线程DXD搜索
    auto end = std::chrono::high_resolution_clock::now();

    std::cout << "搜索到的解个数: " << rootDNNF->count << std::endl;
    
    searchTimeSeconds = std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count();
    std::cout << "多线程DXD搜索完成, 耗时: " << searchTimeSeconds << " 秒。" << std::endl;
    if(isParallelSearch) {
        std::cout << "并行搜索模式已启用。" << std::endl;
        std::cout << "并行搜次数: " << p_count << std::endl;  
    } else {
        std::cout << "并行搜索模式未启用。" << std::endl;
    }
    std::cout << std::endl;
}

void DancingMatrix::startOptimizedDXD() {
        std::cout << "开始优化版DXD搜索..." << std::endl;
        
        OptimizedBlock initial_block(rowsSet, colsSet);
        
        auto start = std::chrono::high_resolution_clock::now();
        rootDNNF = optimizedDXD(initial_block);
        auto end = std::chrono::high_resolution_clock::now();
        
        searchTimeSeconds = std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count();
        
        std::cout << "搜索到的解个数: " << rootDNNF->count << std::endl;
        std::cout << "优化版DXD搜索完成, 耗时: " << searchTimeSeconds << " 秒" << std::endl;
        
        std::cout << std::endl;
}