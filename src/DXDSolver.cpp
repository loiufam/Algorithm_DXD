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

shared_ptr<DNNFNode> DanceDNNF::serialSearch_iterative(const std::vector<Component>& components) {

    auto andNode = make_shared<DNNFNode>(NodeType::Decomposed, -1, 0);
    andNode->children.reserve(components.size());
    uint64_t totalCount = 1;
    for (const auto& comp : components) {
        Block childBlock(comp.rows, comp.cols);
        
        auto childRes = DXD_iterative(std::move(childBlock));

        if(!childRes || childRes->label == -2 ) {
            return F;
        }
        andNode->children.push_back(childRes);
        totalCount *= childRes->count;
    }

    andNode->count = totalCount;
    return andNode;
}

// 串行处理每个子块，组合为 分解 节点
shared_ptr<DNNFNode> DanceDNNF::serialSearch(vector<Block>& blocks) {

    auto andNode = std::make_shared<DNNFNode>(NodeType::Decomposed, -1, 1);
    
    andNode->children.reserve(blocks.size());

    for (auto& block : blocks) {
    
        auto result = DXD(block);

        if (!result || result->label == -2) { 
            return F;
        }

        andNode->children.push_back(result);
        // cout << "分解节点子分支解计数: " << result->count << endl;
        andNode->count *= result->count;
    }
    
    return andNode;
}


shared_ptr<DNNFNode> DanceDNNF::dxdSearch(vector<Block>& blocks) {

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
        // RowIndex[curC->row].cols.clear();
        connectedGraph->remove(curC->row);
        block.rows.erase(curC->row);

        curR = curC->right;  
        while( curR != curC )  
        {          

            curR->down->up = curR->up;  
            curR->up->down = curR->down;  
            --ColIndex[curR->col].size;  
            // RowIndex[curR->row].cols.erase(curR->col);

            curR = curR->right;  
        }  

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
        curR = curC->left;  

        while( curR != noteR )  
        {  
            // RowIndex[curR->row].cols.insert(curR->col);
            ++ColIndex[curR->col].size; 
            curR->down->up = curR;  
            curR->up->down = curR;  

            
            curR = curR->left;  
        }  
        // RowIndex[curC->row].cols.insert(curC->col);
        block.rows.insert(curC->row);
        connectedGraph->restore(curC->row);

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

vector<int> DanceDNNF::collectColsInRow(int row, const Block &block){
    vector<int> cols;
    cols.reserve(block.cols.size());

    Node* rowHead = RowIndex[row].right;
    if(!rowHead) return cols;
    Node* cur = rowHead;
    do {
        if(block.cols.count(cur->col)){
            cols.push_back(cur->col);
        }
        cur = cur->right;
    } while(cur != rowHead);

    return cols;
}

vector<int> DanceDNNF::collectRowsUnderColumn(int col, const Block &block) {
    vector<int> rows;
    rows.reserve(block.rows.size());

    ColunmHeader* colHeader = &ColIndex[col];

    Node* cur = colHeader->down;
    while(cur != colHeader){
        if(block.rows.count(cur->row)){
            rows.push_back(cur->row);
        }
        cur = cur->down;
    }
    return rows;
}

shared_ptr<DNNFNode> DanceDNNF::DXD_iterative(Block&& rootBlock) {
    // Work stack of frames
    vector<DXDFrame> stack;
    stack.emplace_back(rootBlock);


    shared_ptr<DNNFNode> lastResult = nullptr;

    while(!stack.empty()) {
        DXDFrame &frame = stack.back();

        switch(frame.stage) {
            case DXDFrame::START: {
                // base-case: empty cols
                if(frame.block.cols.empty()){
                     lastResult = T; 
                     stack.pop_back(); 
                     break; 
                }

                // try cache
                frame.stateHash = hashBlockState(frame.block.cols);
                frame.cachedResult = getCachedResult(frame.stateHash);
                if(frame.cachedResult) {
                     lastResult = frame.cachedResult; 
                     stack.pop_back(); 
                     break; 
                }

                // try decomposition if within thresholds
                if(frame.block.rows.size() >= (size_t)MIN_BLOCK_ROWS && frame.block.rows.size() <= (size_t)MAX_BLOCK_ROWS){
                    frame.comps = connectedGraph->getComponents();
                    if(frame.comps.size() > 1){
                        MAX_B_COUNT = max(MAX_B_COUNT, frame.comps.size());
                        // handle decomposition inline by calling serialSearch_iterative
                        auto decompositionNode = serialSearch_iterative(frame.comps);
                        setCachedResult(frame.stateHash, decompositionNode);
                        lastResult = decompositionNode;
                        stack.pop_back();
                        break;
                    }
                }

                // no decomposition: select column
                frame.choose = selectColumnHeuristic(frame.block.cols);
                if(!frame.choose || frame.choose->size <= 0){ 
                    lastResult = F; 
                    stack.pop_back(); 
                    break; 
                }

                // create OR node and prepare to iterate rows under choose
                frame.orNode = make_shared<DNNFNode>(NodeType::OR, frame.choose->col, 0);
                frame.rowsUnderChoose = collectRowsUnderColumn(frame.choose->col, frame.block);
                frame.rowCursor = 0;


                // mark that we go into ITER_ROWS processing
                frame.stage = DXDFrame::ITER_ROWS;
                break;
            }

            case DXDFrame::ITER_ROWS: {
                // If all rows processed, finalize OR node
                if(frame.rowCursor >= frame.rowsUnderChoose.size()) {
                    // cleanup: no columns remain specifically to uncover here because
                    // each child frame does its own cover/uncover on its Block copy.
                    if(frame.orNode->children.empty()){
                        lastResult = F;
                    } else {
                        setCachedResult(frame.stateHash, frame.orNode);
                        lastResult = frame.orNode;
                    }
                    stack.pop_back();
                    break;
                }

                // Process the next row: snapshot row id
                int rowId = frame.rowsUnderChoose[frame.rowCursor];

                // For this child we need to produce reduced block:
                // Copy the frame.block into childBlock, then perform the coverings
                Block childBlock = frame.block; 

                // cover the chosen column in childBlock
                // coverInBlock(frame.choose->col, childBlock);
                                
                auto colsInRow = collectColsInRow(rowId, childBlock);
                for(int cc : colsInRow) coverInBlock(cc, childBlock);

                // push a continuation frame (AFTER_CHILD) with current cursor state
                DXDFrame cont(frame.block);
                cont.stage = DXDFrame::AFTER_CHILD;
                cont.choose = frame.choose;
                cont.orNode = frame.orNode;
                cont.stateHash = frame.stateHash;
                cont.rowsUnderChoose = frame.rowsUnderChoose;
                cont.rowCursor = frame.rowCursor; // the current child index
                // push cont
                stack.push_back(std::move(cont));

                // push child frame (ENTRY) to compute DXD on childBlock
                stack.emplace_back(std::move(childBlock));
                break;
            }

            case DXDFrame::AFTER_CHILD: {
                // result of child computation is in lastResult
                auto childRes = lastResult;
                // restore lastResult to null for next child
                lastResult = nullptr;

                // If childRes exists and is not F, append Decision node under OR
                if(childRes && childRes->label != -2){
                    // create variable node and decision AND node
                    auto varNode = make_shared<DNNFNode>(NodeType::Variable, frame.rowsUnderChoose[frame.rowCursor]);
                    auto decisionNode = make_shared<DNNFNode>(NodeType::Decision, varNode, childRes);
                    frame.orNode->children.push_back(decisionNode);
                    frame.orNode->count += childRes->count;
                }

                // advance cursor in the parent frame (which is below this frame on stack)
                if(!stack.empty()){
                    // pop the AFTER_CHILD frame (current frame)
                    stack.pop_back();
                    // parent frame is now at stack.back(); we need to increment its cursor
                    if(!stack.empty()){
                        DXDFrame &parent = stack.back();
                        // parent is the frame that set this AFTER_CHILD; increment its rowCursor
                        parent.rowCursor = parent.rowCursor + 1;
                        // ensure parent's orNode is updated (shared_ptr remains valid)
                        parent.orNode = frame.orNode;
                        // do not set lastResult here; continue processing parent in next loop
                    }
                }
                break;
            }

            default:
                // fallthrough safe-guard
                stack.pop_back();
                break;
        }
    }
    if(lastResult) return lastResult;
    return F; // fallback
}

// DXD单线程（要体现分解性）
shared_ptr<DNNFNode> DanceDNNF::DXD(Block& block) {
    
    if(block.cols.empty()) {
        return T; // 如果没有列，返回T
    } 

    // 先查缓存
    size_t state = hashBlockState(block.cols); 
    if(CacheST.find(state) != CacheST.end()){
        return CacheST[state];
    }

    //shouldDecompose(block) 
    if(block.rows.size() >= MIN_BLOCK_ROWS ) {

        set<int> rows(block.rows.begin(), block.rows.end());
        vector<vector<int>> comps = connectedGraph->getComponents(rows);

        MAX_B_COUNT = max(MAX_B_COUNT, comps.size());

        // vector<Component> comps = connectedGraph->getComponents();
        // if(comps.size() > 1){
                
        //     auto blocks = spilit(comps); // 分解为多个块
        //     auto res_and_node = serialSearch(blocks); 
        //     cacheST[state] = res_and_node; // 缓存结果
        //     return res_and_node; // 返回分解节点
        // }
    }

    ColunmHeader* choose = selectColumnHeuristic(block.cols);  

    if(choose->size <= 0) {
        return F; // 如果没有可选列，返回F
    }

    // 将choose列下的行节点作为Decision节点加入children
    auto orNode = make_shared<DNNFNode>(NodeType::OR, choose->col, 0);

    coverInBlock(choose->col, block);
    // cover( choose->col );
    Node* curC = choose->down;
    while(curC != choose) {
        
        // batchCoverInBlock(curC, block, localOp);
        Node* curR = curC->right;
        while (curR != curC) {
            coverInBlock(curR->col, block);
            // cover(curR->col);
            curR = curR->right;
        }
 
        auto node = DXD(block); // 递归左分支

        if(node->label != -2){
            // shared_ptr<DNNFNode> var = make_shared<DNNFNode>(NodeType::Variable, curC->row + 1);
            auto and_node = make_shared<DNNFNode>(curC->row + 1, node, node->count);
            // and_node->count = node->count;
            orNode->count += node->count; // 累加当前Decision节点的计数
            orNode->children.push_back(and_node);
        }
        
        // batchUncoverInBlock(block, localOp);
        curR = curC->left;
        while (curR != curC) {
            uncoverInBlock(curR->col, block);
            // cover(curR->col);
            curR = curR->left;
        }
        curC = curC->down;
    }
    uncoverInBlock(choose->col, block);
    // uncover(choose->col);

    if(orNode->children.empty()) {
        orNode = F;
    }   
    // 插入缓存
    // setCachedResult(state, orNode);
    CacheST[state] = orNode;
    return orNode; 
}


void DanceDNNF::startDXD() {

    // std::cout << "开始单线程DXD搜索..." << std::endl;
    logger.logLine("开始单线程DXD搜索...");
    Block block(rowsSet, colsSet);

    // clearCache();
    auto start = std::chrono::high_resolution_clock::now();
    shared_ptr<DNNFNode> rootDNNF = DXD(block);  
    auto end = std::chrono::high_resolution_clock::now();

    // std::cout << "搜索到的解个数: " << rootDNNF->count << std::endl;
    logger.logLine("搜索到的解个数: " + std::to_string(rootDNNF->count));
    
    searchTimeSeconds = std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count();
    // std::cout << "单线程DXD搜索完成, 耗时: " << searchTimeSeconds << " 秒。" << std::endl;
    logger.logLine("单线程DXD搜索完成, 耗时: " + std::to_string(searchTimeSeconds) + " 秒。");
   
    if( MAX_B_COUNT > 1 ) {
        // std::cout << "本次搜索最大分块数为: " << MAX_B_COUNT << std::endl;
        logger.logLine("本次搜索最大分块数为: " + std::to_string(MAX_B_COUNT));
    }
    cout << endl; 
}

shared_ptr<DNNFNode> DecisionDNNF::solveSingle(DancingMatrix& matrix, unordered_map<size_t, shared_ptr<DNNFNode>>& localCache)
{
    if (matrix.getRoot()->right == matrix.getRoot()) {
        return T; // 所有列都被覆盖，找到一个解
    }

    size_t state = matrix.getColumnState();
    auto cacheIt = localCache.find(state);
    if (cacheIt != localCache.end()) {
        return cacheIt->second; 
    }

    ColunmHeader* chosenCol = matrix.selectCol();

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
    futures.reserve(matrices.size());

    for (auto& matrixPtr : matrices) {
        // 使用move避免拷贝，每个线程获得独立所有权
        futures.push_back(pool.enqueue([this](unique_ptr<DancingMatrix> matrix) {
            unordered_map<size_t, shared_ptr<DNNFNode>> localCache;
            return solveSingle(*matrix, localCache);
        }, std::move(matrixPtr)));
    }

    auto rootNode = make_shared<DNNFNode>();
    rootNode->count = 0;

    for (auto& future : futures) {
        auto result = future.get();
        rootNode->children.push_back(result);
        rootNode->count += result->count;
    }

    return rootNode;
}

void ExactCoverSolver::searchEC() {
    DancingMatrix dm(input_file, from);

    // cout << "最大线程数: " << poolSize << endl;
    logger.logLine("最大线程数: " + to_string(poolSize));

    col_id selectCol = dm.getClosedSizeCol(poolSize);
    ColunmHeader* colHead = &dm.getColIndex()[selectCol];

    cout << "选择列: " << selectCol << " size: " << colHead->size << endl;

    vector<unique_ptr<DancingMatrix>> subMatrices;
    subMatrices.reserve(colHead->size);
    
    Node* curR = colHead->down;
    while (curR != colHead) {
        auto subMatrix = make_unique<DancingMatrix>(input_file, from);
        
        RowNode* rowHead = &subMatrix->getRowIndex()[curR->row];
        Node* startNode = rowHead->right;
        subMatrix->cover(startNode->col);

        Node* curC = startNode->right;
        while (curC != startNode) {
            subMatrix->cover(curC->col);
            curC = curC->right;
        }
        subMatrices.push_back(std::move(subMatrix));
        curR = curR->down;
    }  

    DecisionDNNF solver(std::move(subMatrices));

    auto start = std::chrono::high_resolution_clock::now();
    shared_ptr<DNNFNode> result = solver.solve();
    auto end = std::chrono::high_resolution_clock::now();

    double elapsedSeconds = std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count();
    // cout << "搜索完成，耗时: " << elapsedSeconds << " 秒。" << endl;
    logger.logLine("多线程DXD搜索完成, 耗时: " + to_string(elapsedSeconds) + " 秒。");
    // cout << "搜索到的解个数: " << result->count << endl;
    logger.logLine("搜索到的解个数: " + to_string(result->count));
    cout << endl;
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
    auto start = std::chrono::high_resolution_clock::now();
    rootDNNF = DXD(block);  // 多线程DXD搜索
    auto end = std::chrono::high_resolution_clock::now();

    searchTimeSeconds = std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count();
    cout<< "多线程算法计时: " << searchTimeSeconds - timer.getElapsedTime() << " 秒。" << endl;

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
