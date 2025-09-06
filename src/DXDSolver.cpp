#include "../include/DXD.h"
#include "../include/ConnectedGraph.h"

void DanceDNNF::batchCover(const std::vector<int>& columns) {
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

void DanceDNNF::batchUncover() {
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

std::shared_ptr<ORNode> DanceDNNF::make_node(int row) {
    auto left = std::make_shared<ANDNode>(row);
    auto right = std::make_shared<ANDNode>();
    return std::make_shared<ORNode>(row, left, right);
}

std::shared_ptr<ORNode> DanceDNNF::Search(Node* curC) {
    
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

void DanceDNNF::startSearch(bool g)
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
        ColunmHeader* colHead = &ColIndex[col];
        
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
            const RowNode* rowHead = &RowIndex[row];
            cols.insert(rowHead->cols.begin(), rowHead->cols.end());
        }

        // 过滤：只保留在当前行集合中实际存在的列
        set<int> validCols;
        for(int col : cols) {
            const ColunmHeader* colHead = &ColIndex[col];
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
                const RowNode* rowHead = &RowIndex[row];
                cols.insert(rowHead->cols.begin(), rowHead->cols.end());
            }
            
            // 过滤：只保留在当前行集合中实际存在的列
            set<int> validCols;
            for(int col : cols) {
                const ColunmHeader* colHead = &ColIndex[col];
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
shared_ptr<DNNFNode> DanceDNNF::serialSearch(vector<Block>& blocks) {

    auto andNode = std::make_shared<DNNFNode>(NodeType::Decomposed, -1, 0);
    uint64_t totalCount = 1; 

    for (auto& block : blocks) {
        auto result = DXD(block);
        if (result->label == -2) { 
            return F;
        }
        andNode->children.push_back(result);
        totalCount *= result->count;
    }
    
    andNode->count = totalCount;
    return andNode;
}


shared_ptr<DNNFNode> DanceDNNF::dxdSearch(vector<Block>& blocks) {

    if (!isParallelSearch) {
        return serialSearch(blocks);
    }

    timer.markStopTime();


    std::vector<std::future<std::shared_ptr<DNNFNode>>> futures;

    for (auto& block : blocks) {
       futures.push_back(pool.enqueue([this](Block block) {
            // 使用值传递避免引用问题
            return DXD(block);
        }, block)); 
    }

    // 收集结果并创建Decomposed节点
    auto andNode = std::make_shared<DNNFNode>(NodeType::Decomposed, -1, 0);
    uint64_t totalCount = 1; 

    
    for (auto& future : futures) {
        auto result = future.get();
        if (result->label == -2) { 
            return F;
        }
        andNode->children.push_back(result);
        totalCount *= result->count;
    }
    
    andNode->count = totalCount;
    timer.markStartTime();
    return andNode;
}


// 并行处理每个子块，组合为 分解 节点
shared_ptr<DNNFNode> DanceDNNF::parallelSearch(vector<Block>& blocks) {
    isParallelSearch = true; // 标记为并行搜索
    
    std::vector<std::future<std::shared_ptr<DNNFNode>>> futures;
        
    for (auto& block : blocks) {
        futures.push_back(pool.enqueue([this, block]() mutable {
            return parallelDXD(block);
        }));
    }
    
    // 收集结果并创建AND节点
    auto andNode = std::make_shared<DNNFNode>(NodeType::Decomposed, -1, 0);
    uint64_t totalCount = 1; 
    
    for (auto& future : futures) {
        auto result = future.get();
        if (result->label == -2) { 
            return F;
        }
        andNode->children.push_back(result);
        totalCount *= result->count;
    }
    
    andNode->count = totalCount;
    return andNode;
}

std::shared_ptr<DNNFNode> DanceDNNF::optimizedDXD(Block& block) {
        if (root->right == root) {
            return T;
        }
        
        size_t state_hash = hashColState(block.cols);
        
        // 使用智能缓存
        auto cached = getCachedResult(state_hash);
        if (cached) {
            return cached;
        }
        
        // // 快速连通性检测
        // fastInitBlock(block);
        // // 检查是否可分解
        // if (block.connectedRows.size() > 1) {
        //     auto sub_blocks = intelligentSplitBlock(block);
        //     auto result = adaptiveParallelSearch(sub_blocks);
        //     setCachedResult(state_hash, result);
        //     return result;
        // }
        
        // 单块搜索
        // vector<int> cols(block.cols.begin(), block.cols.end());
        // sort(cols.begin(), cols.end());
        // ColunmHeader* selected_col = nullptr;
        // int MIN_SIZE = INT_MAX;
        // for(auto it = cols.rbegin(); it != cols.rend(); ++it) {
        //     ColunmHeader* curCol = &ColIndex[*it];
        //     if (curCol->size < MIN_SIZE) {
        //         selected_col = curCol;
        //         MIN_SIZE = curCol->size;
        //     }
        // }
        ColunmHeader* selected_col = selectColumnHeuristic(block.cols);
        
        
        if (selected_col->size <= 0) {
            setCachedResult(state_hash, F);
            return F;
        }
        
        auto or_node = std::make_shared<DNNFNode>(NodeType::OR, selected_col->col, 0);


        Node* cur_node = selected_col->down;
        cover(selected_col->col);
        while (cur_node != selected_col) { // 遍历不同行
            
            // batchCoverInBlock(cur_node, block);
            Node* curRow = cur_node->right;
            while(curRow != cur_node){
                cover(curRow->col);
                curRow = curRow->right;
            }
            
            auto sub_result = optimizedDXD(block);
            
            if (sub_result->label != -2) {
                // 使用父类的变量表
                auto var_it = V_Table.find(cur_node->row);
                std::shared_ptr<DNNFNode> var;
                if (var_it != V_Table.end()) {
                    var = var_it->second;
                } else {
                    var = std::make_shared<DNNFNode>(NodeType::Variable, cur_node->row + 1);
                    V_Table[cur_node->row] = var;
                }
                
                auto and_node = std::make_shared<DNNFNode>(NodeType::Decision, var, sub_result);
                and_node->count = sub_result->count;
                or_node->count += and_node->count;
                or_node->children.push_back(and_node);
            }
            
            // batchUncoverInBlock(block);
            curRow = cur_node->left;
            while(curRow != cur_node) {
                uncover(curRow->col);
                curRow = curRow->left;
            }
            cur_node = cur_node->down;
        }
        uncover(selected_col->col);
        
        if (or_node->children.empty()) {
            or_node = F;
        }
        
        setCachedResult(state_hash, or_node);
        return or_node;
}


void DanceDNNF::printBlocks(const vector<Block>& blocks) {
        cout << "找到 " << blocks.size() << " 个独立块:\n";
        for (size_t i = 0; i < blocks.size(); i++) {
            cout << "块 " << i + 1 << ":\n";
            printBlock(blocks[i]);
        }
}

void DanceDNNF::printBlock(const Block& block) {
        cout << "  行: [";
        int j = 0;
        for (int row : block.rows) {
            cout << row + 1; // 转为1基索引显示
            if (j < block.rows.size() - 1) cout << ", ";
            j++;
        }
        cout << "]\n";
        cout << "  列: [";
        j = 0;
        for (int col : block.cols) {
            cout << col; 
            if (j < block.cols.size() - 1) cout << ", ";
            j++;
        }
        cout << "]\n\n"; 
}

void DanceDNNF::coverInBlock(int c, Block& block){

    ColunmHeader* col = &ColIndex[c];  
    col->right->left = col->left;  
    col->left->right = col->right;  
    
    block.cols.erase(c); // 从块中移除列


    Node* curR, *curC;  
    curC = col->down;  
    while( curC != col )  
    {    
        // RowIndex[curC->row].cols.erase(curC->col); // 从行映射中移除该列
        removeCol(curC->row, curC->col);
        curR = curC->right;  
        while( curR != curC )  
        {  
            curR->down->up = curR->up;  
            curR->up->down = curR->down;  
            --ColIndex[curR->col].size;  

            // RowIndex[curC->row].cols.erase(curR->col); // 从行映射中移除该列
            removeCol(curR->row, curR->col);
            curR = curR->right;  
        }  
        block.rows.erase(curC->row);
        // connectedGraph->remove(curC->row);
        curC = curC->down;  
    }  
}

void DanceDNNF::uncoverInBlock(int c, Block& block){ 
    
    Node* curR, *curC;  
    ColunmHeader* col = &ColIndex[c];  

    curC = col->up;  

    while( curC != col )  
    {  
        Node* noteR = curC;  
        // RowIndex[curC->row].cols.insert(curC->col); // 恢复行映射中的该列
        restoreCol(curC->row, curC->col);
        curR = curC->left;  

        while( curR != noteR )  
        {  
            ++ColIndex[curR->col].size; 
            curR->down->up = curR;  
            curR->up->down = curR;  

            // RowIndex[curC->row].cols.insert(curR->col);
            restoreCol(curR->row, curR->col);
            curR = curR->left;  
        }  
        block.rows.insert(curC->row);
        // connectedGraph->restore(curC->row);
        curC = curC->up;  
    }  

    col->right->left = col;  
    col->left->right = col;  

    block.cols.insert(c);
}

vector<Block> DanceDNNF::spilit(const vector<vector<int>>& rows) {
    vector<Block> blocks;
    for(int i = 0; i < rows.size(); ++i) {
        const vector<int>& rowVec = rows[i];
        set<int> rowSet(rowVec.begin(), rowVec.end());
        set<int> cols;
        // 获取所有行涉及的列的并集
        for(int row : rowSet) {
            const RowNode* rowHead = &RowIndex[row];
            cols.insert(rowHead->cols.begin(), rowHead->cols.end());
        }

        Block newBlock(rowSet, cols);
        blocks.push_back(newBlock);
    }
    return blocks;
}

// DXD单线程（要体现分解性）
shared_ptr<DNNFNode> DanceDNNF::DXD(Block& block) {
    
    if(block.cols.empty()) {
        return T; // 如果没有列，返回T
    } 

    // 先查缓存
    size_t state = hashBlockState(block.cols); // 编码当前块状态
    auto cacheResult = getCachedResult(state);
    if(cacheResult) {
        return cacheResult;
    }

    //shouldDecompose(block) 
    if(block.rows.size() >= MIN_BLOCK_ROWS && detect_records.find(state) == detect_records.end()) {

        // vector<set<int>> row_sets = mergeRowSets(block);
        timer.markStopTime();
        set<int> rows(block.rows.begin(), block.rows.end());
        vector<vector<int>> components = connectedGraph->getComponents(rows);

        if(components.size() > 1){
             
            // cout<< "块分解成功，块数: " << components.size() << endl;
            //incrementPCount(); 
            MAX_B_COUNT = max(MAX_B_COUNT, components.size());
            // block.is_spilited = true; // 标记为已分解
            auto blocks = spilit(components); // 分解为多个块
            auto res_and_node = dxdSearch(blocks); // 并行搜索
            setCachedResult(state, res_and_node); // 缓存结果
            timer.markStartTime(); // 继续计时
            return res_and_node; // 返回分解节点
        }else{
            timer.markStartTime(); // 继续计时
            detect_records.insert(state);
        }
    }

    ColunmHeader* choose = selectColumnHeuristic(block.cols);  

    if(choose->size <= 0) {
        return F; // 如果没有可选列，返回F
    }

    // 将choose列下的行节点作为Decision节点加入children
    auto orNode = make_shared<DNNFNode>(NodeType::OR, choose->col, 0);

    coverInBlock(choose->col, block);
    Node* curC = choose->down;
    while(curC != choose) {
        
        // batchCoverInBlock(curC, block, localOp);
        Node* curR = curC->right;
        while (curR != curC) {
            coverInBlock(curR->col, block);
            curR = curR->right;
        }
 
        auto node = DXD(block); // 递归左分支

        if(node->label != -2){
            shared_ptr<DNNFNode> var = make_shared<DNNFNode>(NodeType::Variable, curC->row + 1);
            auto and_node = make_shared<DNNFNode>(NodeType::Decision, var, node);
            and_node->count = node->count;
            orNode->count += and_node->count; // 累加当前Decision节点的计数
            orNode->children.push_back(and_node);
        }
        
        // batchUncoverInBlock(block, localOp);
        curR = curC->left;
        while (curR != curC) {
            uncoverInBlock(curR->col, block);
            curR = curR->left;
        }
        curC = curC->down;
    }
    uncoverInBlock(choose->col, block);

    if(orNode->children.empty()) {
        orNode = F;
    }   
    // 插入缓存
    setCachedResult(state, orNode);
    return orNode; 
}


void DanceDNNF::startDXD() {

    std::cout << "开始DXD搜索..." << std::endl;
    Block block(rowsSet, colsSet);

    clearCache();
    auto start = std::chrono::high_resolution_clock::now();
    timer.markStartTime();
    rootDNNF = DXD(block);  // 先单线程DXD搜索
    timer.markStopTime();
    auto end = std::chrono::high_resolution_clock::now();

    std::cout << "搜索到的解个数: " << rootDNNF->count << std::endl;
    
    searchTimeSeconds = std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count();
    std::cout << "DXD搜索完成, 耗时: " << searchTimeSeconds << " 秒。" << std::endl;
    
    std::cout << std::endl; 
}

// 主搜索函数(多线程版本)
shared_ptr<DNNFNode> DanceDNNF::parallelDXD(Block& block) {
    if (block.cols.empty()) {
        return T; 
    }

    size_t state = hashBlockState(block.cols); // 编码当前块状态

    // 检查缓存
    auto cachedResult = getCachedResult(state);
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
    
    ColunmHeader* selected_col = selectColumnHeuristic(block.cols);  

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
    setCachedResult(state, orNode);
    return orNode;
}


void DanceDNNF::startMultiThreadDXD() {

    std::cout << "多线程DXD搜索..." << std::endl;
    Block block(rowsSet, colsSet);
    
    clearCache();
    isParallelSearch = true;  // 开启多线程搜索标志

    timer.reset();
    timer.markStartTime();
    rootDNNF = DXD(block);  // 多线程DXD搜索
    timer.markStopTime();

    cout<< "多线程算法计时: " << timer.getElapsedTime() << " 秒。" << endl;

    cout << "多线程DXD搜索解个数: " << rootDNNF->count << endl;
    if( MAX_B_COUNT > 1 ) {
        std::cout << "本次搜索开启多线程并行搜索, 最大分块数为: " << MAX_B_COUNT << std::endl;
    } else {
        std::cout << "本次搜索为串行搜索。" << std::endl;
    }
    std::cout << std::endl;
}
// 批量处理舞蹈链，并更新block状态
void DanceDNNF::batchCoverInBlock(Node* curC, Block& block) 
{
    BatchOperation batchOp;

    Node* curNode = curC;
    Node* startNode = curNode;
    do {  // 遍历该行所有列
        ColunmHeader* col = &ColIndex[curNode->col];
        
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
                --ColIndex[curR->col].size;

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
        Node* curR = &RowIndex[*it];
        ++ColIndex[curR->col].size;     // 恢复列的大小计数
        curR->down->up = curR;          // 恢复下方节点的上指针
        curR->up->down = curR;          // 恢复上方节点的下指针

        // 恢复列头和行头的元素集合
        // ColIndex[curR->col].rows.insert(curR->row);  // 恢复列的行集合
        // RowIndex[curR->row].cols.insert(curR->col);  // 恢复行的列集合
    }

    for(int i = batchOp.coveredCols.size() - 1; i >= 0; --i) {
        int colIndex = batchOp.coveredCols[i];
        ColunmHeader* col = &ColIndex[colIndex];
        
        
        // 将当前列重新插入到列头链表中
        col->left->right = col;           // 左邻居指向当前列
        col->right->left = col;           // 右邻居指向当前列
    }

}

// 遍历Decision-DNNF，收集所有解（每个解是若干行号的集合）
void DanceDNNF::traverseDNNF(const std::shared_ptr<DNNFNode>& node, std::vector<int>& currentSolution) {
    if (!node || node->label == -2) return; // F 节点
    if (node->label == -1) {
        // T 节点，表示当前路径是一种完整解
        solutions.push_back(currentSolution);
        return;
    }

    switch (node->type) {
        case NodeType::OR:
            // OR节点：多个子分支，分别尝试
            for (const auto& child : node->children) {
                traverseDNNF(child, currentSolution);
            }
            break;
        
        case NodeType::Decision:
            if (node->left && node->left->type == NodeType::Variable) {
                currentSolution.push_back(node->left->label); // 加入当前变量（行号）
                traverseDNNF(node->right, currentSolution); // 递归右子树
                currentSolution.pop_back(); // 回溯
            }
            break;

        case NodeType::Decomposed:
            {
                // Decomposed 是 AND 节点，所有子节点必须都满足
                std::vector<std::vector<int>> partialResults(1);
                for (const auto& child : node->children) {
                    std::vector<std::vector<int>> temp;
                    std::vector<int> empty;
                    traverseDNNF(child, empty);

                    std::vector<std::vector<int>> newResults;
                    for (const auto& prefix : partialResults) {
                        for (const auto& suffix : temp) {
                            std::vector<int> merged = prefix;
                            merged.insert(merged.end(), suffix.begin(), suffix.end());
                            newResults.push_back(std::move(merged));
                        }
                    }
                    partialResults = std::move(newResults);
                }
                for (auto& solution : partialResults) {
                    solutions.push_back(std::move(solution));
                }
            }
            break;
        // case NodeType::Variable:
        //     currentSolution.push_back(node->label);
        //     solutions.push_back(currentSolution);
        //     currentSolution.pop_back();
        //     break;

        // case NodeType::Terminal:
        //     if (node->label == -1) solutions.push_back(currentSolution); // T
        //     break;
        default:
            break;
    }
}

void DanceDNNF::countSolutions(shared_ptr<ORNode> node) {
    if (!node) return;
    
    if (node->label == -1) { // T节点
        count++;
        return;
    }
    
    if (node->label == -2) { // F节点
        return;
    }
    
    countSolutions(node->left->next);
    countSolutions(node->right->next);

}
