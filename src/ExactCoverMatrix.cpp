#include "../include/DancingMatrix.h"

//构造函数
DancingMatrix::DancingMatrix( int rows, int cols, int** matrix )  
    : ROWS(rows), COLS(cols), count(0) {  
    ColIndex = new ColunmHeader[cols+1];  
    RowIndex = new RowNode[rows];  
    root = &ColIndex[0];  
    ColIndex[0].left = &ColIndex[COLS];  
    ColIndex[0].right = &ColIndex[1];  
    ColIndex[COLS].right = &ColIndex[0];  
    ColIndex[COLS].left = &ColIndex[COLS-1];  
    for( int i=1; i<cols; i++ )  
    {  
        ColIndex[i].left = &ColIndex[i-1];  
        ColIndex[i].right = &ColIndex[i+1];  
    }  

    for ( int i=0; i<=cols; i++ )  
    {  
        ColIndex[i].up = &ColIndex[i];  
        ColIndex[i].down = &ColIndex[i];  
        ColIndex[i].col = i;  
    }  
    ColIndex[0].down = &RowIndex[0];  
    

    for( int i = 0; i < rows; i++ ){
        for( int j = 0; j < cols ; j++ ) {  
            if(matrix[i][j] == 1){
                insert(  i , j+1 );  //行数与原矩阵相同，而列数加1
                ONE_COUNT++; // 统计矩阵中1的个数
                rowsSet.insert(i);
                colsSet.insert(j+1); // 列数加1
                rowToColsSet[i].insert(j+1);
                colToRowsSet[j+1].insert(i);
            }
        }
    } 
}

//析构函数，在 DancingMatrix 对象被销毁时，释放所有动态分配的内存，避免内存泄漏
DancingMatrix::~DancingMatrix()  
{  
    if (!ColIndex) return;
    for (int i = 1; i <= COLS; ++i) {
        auto* col = &ColIndex[i];
        if (!col) continue;

        std::vector<Node*> to_delete;
        for (auto* node = col->down; node && node != col; node = node->down) {
            to_delete.push_back(node);
        }
        for (auto* node : to_delete) delete node;
    }
    delete[] RowIndex;
    delete[] ColIndex;
    ColIndex = nullptr;
    RowIndex = nullptr;

}


void DancingMatrix::fastInitBlock(OptimizedBlock& block) {
        if (block.connectivity_cached) {   // && !block.needs_connectivity_update()
            return;
        }
        
        // 使用快速连通性检测器
        auto components = detect_components(block);
        
        block.connectedRows.clear();
        block.rowToRowsSet.clear();
        
        for (size_t i = 0; i < components.size(); ++i) {
            std::set<int> component_set(components[i].begin(), components[i].end());
            block.connectedRows.push_back(component_set);
            
            for (int row : components[i]) {
                block.rowToRowsSet[row].push_back(i);
            }
        }
        
        block.connectivity_cached = true;
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
vector<set<int>> DancingMatrix::mergeRowSets(Block& block){
    
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


string DancingMatrix::encodeBlockState(const unordered_set<int>& cols){
    string state(COLS, '0'); // 初始化为全0字符串
    for(int col : cols) {
        state[col - 1] = '1'; // 将对应列设置为1   
    }
    return state;
}

size_t DancingMatrix::hashBlockState(const unordered_set<int>& cols) {
    size_t hash = 0;
    for(int col : cols) {
        hash ^= std::hash<int>()(col) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
    }
    return hash;
}

size_t DancingMatrix::hashColState(unordered_set<int>& cols) {
    size_t hash = 0;
    cols.clear();
    ColunmHeader* curCol = (ColunmHeader *)root->right;
    while(curCol != root) {
        int col = curCol->col;
        hash ^= std::hash<int>()(col) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        cols.insert(col);
        curCol = (ColunmHeader *)curCol->right;
    }
    return hash;
}

vector<Block> DancingMatrix::spilitBlock(const vector<set<int>>& mergeRowSets){
    
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

vector<Block> DancingMatrix::spilitBlockParallel(const vector<set<int>>& mergeRowSets) {
   
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

std::vector<OptimizedBlock> DancingMatrix::intelligentSplitBlock(const OptimizedBlock& block) {
        std::vector<OptimizedBlock> result;
        
        if (block.connectedRows.size() <= 1) {
            result.push_back(block);
            return result;
        }
        
        decomposition_count.fetch_add(1, std::memory_order_relaxed);
        
        for (const auto& component : block.connectedRows) {
            std::set<int> component_cols;
            
            // 找到这些行涉及的列
            for (int row : component) {
                if (!block.rows.count(row)) continue;
                
                if (RowIndex[row].right) {
                    Node* rowHead = RowIndex[row].right;
                    Node* cur = rowHead;
                    do {
                        if (block.cols.count(cur->col)) {
                            component_cols.insert(cur->col);
                        }
                        cur = cur->right;
                    } while (cur != rowHead);
                }
            }
            
            if (!component_cols.empty()) {
                OptimizedBlock sub_block(component, component_cols);
                sub_block.invalidate_connectivity(); // 标记需要重新计算
                result.push_back(std::move(sub_block));
            }
        }
        
        return result;
}

vector<DXD_Block> DancingMatrix::detectBlocks(const DXD_Block& currentBlock) 
{

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

            if (blockCols.size() >= MIN_BLOCK_COLS) {
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

void DancingMatrix::build_mapping_from_cols(const unordered_set<int>& blockCols, unordered_map<int, set<int>>& rowToCols, unordered_map<int, set<int>>& colToRows)
{
    for (auto col : blockCols) {
        ColunmHeader* c = &ColIndex[col];
        Node* curR = c->down;
        while (curR != c) {
            rowToCols[curR->row].insert(col);
            colToRows[col].insert(curR->row);
            curR = curR->down;
        }
    }
}

std::vector<std::vector<int>> DancingMatrix::detect_components(const OptimizedBlock& block) {
        if (block.cols.empty()) {
            return {};
        }
        
        size_t max_row = *std::max_element(block.rows.begin(), block.rows.end()) + 1;
        
        
        // 初始化并查集
        UnionFind uf(max_row);
        
        // 使用BitSet加速连通性检测
        std::unordered_map<int, FastBitSet> col_rows;
        
        for (int col : block.cols) {
            FastBitSet& row_bitset = col_rows[col];
            row_bitset.resize(max_row);
            
            ColunmHeader* cur = &ColIndex[col];  
            Node* node = cur->down;
            while (node != cur) {
                if (block.rows.count(node->row)) {
                    row_bitset.set(node->row);
                }
                node = node->down;
            }
        }
        
        // 快速合并相交的行
        std::vector<int> col_list(block.cols.begin(), block.cols.end());
        for (size_t i = 0; i < col_list.size(); ++i) {
            const auto& bitset_i = col_rows[col_list[i]];
            auto rows_i = bitset_i.get_set_bits();
            
            for (size_t j = 0; j < rows_i.size(); ++j) {
                for (size_t k = j + 1; k < rows_i.size(); ++k) {
                    uf.unite(rows_i[j], rows_i[k]);
                }
            }
        }
        
        // 收集连通分量
        std::unordered_map<int, std::vector<int>> components;
        for (int row : block.rows) {
            components[uf.find(row)].push_back(row);
        }
        
        std::vector<std::vector<int>> result;
        for (auto& [root, component] : components) {
            if (component.size() > 0) {
                result.push_back(std::move(component));
            }
        }
        
        return result;
}

// 串行处理每个子块，组合为 分解 节点
shared_ptr<DNNFNode> DancingMatrix::dxdSearch(vector<Block>& blocks) {

    std::vector<std::future<std::shared_ptr<DNNFNode>>> futures;

    for (auto& block : blocks) {
       futures.push_back(getThreadPool().enqueue([this](Block block) {
            // 使用值传递避免引用问题
            ThreadLocalBatchOp localOp;  // 每个线程独立的操作栈
            return DXD(block, localOp);
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

// 串行搜索辅助方法
std::shared_ptr<DNNFNode> DancingMatrix::serialSearch(std::vector<OptimizedBlock>& blocks) {
    auto and_node = std::make_shared<DNNFNode>(NodeType::Decomposed, -1, 0);
    uint64_t total_count = 1;
    
    for (auto& block : blocks) {
        auto result = optimizedDXD(block);
        if (result->label == -2) {
            return F;
        }
        and_node->children.push_back(result);
        total_count *= result->count;
    }
    
    and_node->count = total_count;
    return and_node;
}

// 并行处理每个子块，组合为 分解 节点
shared_ptr<DNNFNode> DancingMatrix::parallelSearch(vector<Block>& blocks) {
    isParallelSearch = true; // 标记为并行搜索
    
    std::vector<std::future<std::shared_ptr<DNNFNode>>> futures;
        
    for (auto& block : blocks) {
        futures.push_back(getThreadPool().enqueue([this, block]() mutable {
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

std::shared_ptr<DNNFNode> DancingMatrix::adaptiveParallelSearch(std::vector<OptimizedBlock>& blocks) {
        // 评估是否值得并行
        size_t total_complexity = 0;
        for (const auto& block : blocks) {
            total_complexity += block.cols.size();
        }
        
        bool should_parallel = (blocks.size() >= MIN_PARALLEL_BLOCKS) && 
                              (total_complexity > MIN_BLOCK_COMPLEXITY * blocks.size()) &&
                              (parallel_search_count.load() < MAX_P_COUNT);
        
        if (!should_parallel) {
            return serialSearch(blocks);
        }
        
        parallel_search_count.fetch_add(1, std::memory_order_relaxed);
        
        // 使用父类的线程池
        std::vector<std::future<std::shared_ptr<DNNFNode>>> futures;
        
        for (auto& block : blocks) {
            futures.push_back(getThreadPool().enqueue([this, block]() mutable {

                return optimizedDXD(block);
            }));
        }
        
        // 收集结果
        auto and_node = std::make_shared<DNNFNode>(NodeType::Decomposed, -1, 0);
        uint64_t total_count = 1;
        
        for (auto& future : futures) {
            auto result = future.get();
            if (result->label == -2) {
                return F;  // 短路求值
            }
            and_node->children.push_back(result);
            total_count *= result->count;
        }
        
        and_node->count = total_count;
        return and_node;
}

std::shared_ptr<DNNFNode> DancingMatrix::optimizedDXD(Block& block) {
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
        vector<int> cols(block.cols.begin(), block.cols.end());
        sort(cols.begin(), cols.end());
        ColunmHeader* selected_col = nullptr;
        int MIN_SIZE = INT_MAX;
        for(auto it = cols.rbegin(); it != cols.rend(); ++it) {
            ColunmHeader* curCol = &ColIndex[*it];
            if (curCol->size < MIN_SIZE) {
                selected_col = curCol;
                MIN_SIZE = curCol->size;
            }
        }
        
        
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

void DancingMatrix::printBlocks(const vector<Block>& blocks) {
        cout << "找到 " << blocks.size() << " 个独立块:\n";
        for (size_t i = 0; i < blocks.size(); i++) {
            cout << "块 " << i + 1 << ":\n";
            printBlock(blocks[i]);
        }
}

void DancingMatrix::printBlock(const Block& block) {
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

//插入元素到双向十字链表中
void DancingMatrix::insert( int r, int c )  
{  
    Node* cur = &ColIndex[c];  
    ColIndex[c].size++;  
    RowIndex[r].size++;
    // ColIndex[c].rows.insert(r);
    // RowIndex[r].cols.insert(c);
    Node* newNode = new Node( r, c );  
    while( cur->down != &ColIndex[c] && cur->down->row < r )  
        cur = cur->down;  
    newNode->down = cur->down;  
    newNode->up = cur;  
    cur->down->up = newNode;  
    cur->down = newNode;  
    if( RowIndex[r].right == NULL )  
    {  
        RowIndex[r].right = newNode;  
        newNode->left = newNode;  
        newNode->right = newNode;  
    }  
    else  
    {  
        Node* rowHead = RowIndex[r].right;  
        cur = rowHead;  
        while( cur->right != rowHead && cur->right->col < c )  
            cur = cur->right;  
        newNode->right = cur->right;  
        newNode->left = cur;  
        cur->right->left = newNode;  
        cur->right = newNode;  
    }   
}


void DancingMatrix::printMatrix() const
{
    std::cout<< "Remain Matrix Nodes: " << std::endl;
    ColunmHeader* current = (ColunmHeader*)root->right;
    while(current != root)
    {
        std::cout << "Column " << current->col << " size: " << current->size << " ";
        if(current->size > 0){
            Node* cur = current->down;
            std::cout << "{ Rows: ( ";
            while(cur != current)
            {
                std::cout << cur->row + 1;
                cur = cur->down;
                if(cur != current)
                    std::cout << ", ";
            }
            std::cout << " ) } " << std::endl;
        }
        current = (ColunmHeader*)current->right;
    }
    std::cout << std::endl;
}

void DancingMatrix::cover( int c )  
{  
    ColunmHeader* col = &ColIndex[c];  
    col->right->left = col->left;  
    col->left->right = col->right; 
    
    
    Node* curR, *curC;  
    curC = col->down;  
    while( curC != col )  
    {   
        Node* noteR = curC;  
        curR = noteR->right;  
        while( curR != noteR )  
        {  
            curR->down->up = curR->up;  
            curR->up->down = curR->down;  
            --ColIndex[curR->col].size;  

            curR = curR->right;  
        }  
        curC = curC->down;  
    }  
}

void DancingMatrix::uncover( int c )  
{  
    Node* curR, *curC;  
    ColunmHeader* col = &ColIndex[c];  
    curC = col->up;  
    while( curC != col )  
    {  
        Node* noteR = curC;  
        curR = curC->left;  
        while( curR != noteR )  
        {  
            ++ColIndex[curR->col].size;  
            curR->down->up = curR;  
            curR->up->down = curR;  

            curR = curR->left;  
        }  
        curC = curC->up;  
    }  
    col->right->left = col;  
    col->left->right = col;  

}

void DancingMatrix::coverInBlock(int c, Block& block){


    ColunmHeader* col = &ColIndex[c];  
    col->right->left = col->left;  
    col->left->right = col->right;  
    
    block.cols.erase(c); // 从块中移除列


    Node* curR, *curC;  
    curC = col->down;  
    while( curC != col )  
    {    

        curR = curC->right;  
        while( curR != curC )  
        {  
            curR->down->up = curR->up;  
            curR->up->down = curR->down;  
            --ColIndex[curR->col].size;  

            curR = curR->right;  
        }  
        // block.rows.erase(curC->row);
        curC = curC->down;  
    }  
}

void DancingMatrix::uncoverInBlock(int c, Block& block){ 
    
    Node* curR, *curC;  
    ColunmHeader* col = &ColIndex[c];  

    curC = col->up;  

    while( curC != col )  
    {  
        Node* noteR = curC;  
        curR = curC->left;  

        while( curR != noteR )  
        {  
            ++ColIndex[curR->col].size; 
            curR->down->up = curR;  
            curR->up->down = curR;  

            
            curR = curR->left;  
        }  
        // block.rows.insert(curC->row);
        curC = curC->up;  
    }  

    col->right->left = col;  
    col->left->right = col;  

    block.cols.insert(c);
}

// 批量处理舞蹈链，并更新block状态
void DancingMatrix::batchCoverInBlock(Node* curC, Block& block) 
{
    BatchOperation batchOp;
    batchOp.oldBlock = block; // 保存当前块状态

    Node* curNode = curC;
    Node* startNode = curNode;
    do {  // 遍历该行所有列
        ColunmHeader* col = &ColIndex[curNode->col];
        batchOp.columnLinks.push_back({(ColunmHeader*)col->left, (ColunmHeader*)col->right}); // 保存列的链接信息

        // 从链表中移除列头
        col->right->left = col->left;
        col->left->right = col->right;
        batchOp.columns.push_back(col->col);
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

                batchOp.coveredNodes.push_back(curR);
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
void DancingMatrix::batchUncoverInBlock(Block& block)
{ 
    if(batchOpStack.empty()) return;

    BatchOperation batchOp = batchOpStack.back();
    batchOpStack.pop_back();

    for(auto it = batchOp.coveredNodes.rbegin(); 
    it != batchOp.coveredNodes.rend(); ++it) {
        Node* curR = *it;
        ++ColIndex[curR->col].size;     // 恢复列的大小计数
        curR->down->up = curR;          // 恢复下方节点的上指针
        curR->up->down = curR;          // 恢复上方节点的下指针

        // 恢复列头和行头的元素集合
        // ColIndex[curR->col].rows.insert(curR->row);  // 恢复列的行集合
        // RowIndex[curR->row].cols.insert(curR->col);  // 恢复行的列集合
    }

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

    block = batchOp.oldBlock;
}

// 遍历Decision-DNNF，收集所有解（每个解是若干行号的集合）
void DancingMatrix::traverseDNNF(const std::shared_ptr<DNNFNode>& node, std::vector<int>& currentSolution) {
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

ColunmHeader* DancingMatrix::selectColumnHeuristic(const unordered_set<int>& cols) {
    ColunmHeader* chosen = nullptr;
    int minSize = INT_MAX;
    for (int col : cols) {
        int sz = ColIndex[col].size;
        if (sz == 1) return &ColIndex[col]; // 启发式剪枝
        if (sz < minSize) {
            minSize = sz;
            chosen = &ColIndex[col];
        }
    }
    return chosen;
}

ColunmHeader* DancingMatrix::selectCol()
{
    ColunmHeader* choose = (ColunmHeader*)root->right, *cur=choose;  
    while( cur != root )  
    {   //选择元素最少的列
        if( choose->size > cur->size )  
            choose = cur;  
        cur = (ColunmHeader*)cur->right;  
    } 
    return choose;
}

void DancingMatrix::DLX(std::vector<int>& solution)
{
    if( root->right == root ) {
        count++;
        solutions.push_back(solution);  // 找到解，存入解集中
        return;
    } 

    ColunmHeader* choose = selectCol();
    if( choose->size <= 0 ){
        return;  
    }

    cover( choose->col );
    Node* curC = choose->down;  
    while( curC != choose )  // curC遍历选中列非零行
    {  
        
        Node* noteR = curC;  
        Node* curR = curC->right;  
        while( curR != noteR )  
        {  
            cover( curR->col );  
            curR = curR->right;  
        }  

        solution.push_back(curC->row + 1);  // 将当前行加入解集中
        // 递归搜索
        DLX(solution);
       
        solution.pop_back();  // 回溯，移除当前行
        curR = noteR->left;  
        while( curR != noteR )  
        {  
            uncover( curR->col );  
            curR = curR->left;  
        }  
        curC = curC->down;  
    }  
    uncover( choose->col );  
    return; 
}

void DancingMatrix::X(uint64_t& count)  
{  
    if( root->right == root ) {
        count++;
        return;
    } 
    ColunmHeader* choose = (ColunmHeader*)root->right, *cur=choose;  
    while( cur != root )  
    {   //选择元素最少的列
        if( choose->size > cur->size )  
            choose = cur;  
        cur = (ColunmHeader*)cur->right;  
    }  
    if( choose->size <= 0 ){
        return;  
    } 
          
    cover( choose->col );  
    Node* curC = choose->down;  
    while( curC != choose )  // curC遍历选中列非零行
    {  

        Node* noteR = curC;  
        Node* curR = curC->right;  
        while( curR != noteR )  
        {  
            cover( curR->col );  
            curR = curR->right;  
        }  

        X(count);  // 递归搜索
        
        curR = noteR->left;  
        while( curR != noteR )  
        {  
            uncover( curR->col );  
            curR = curR->left;  
        }  
        curC = curC->down;  
    }  
    uncover( choose->col );  
    return;  
}

//获取当前列的状态
std::string DancingMatrix::getColumnState() const {
    std::string columnState(COLS, '0'); // 初始化为全0字符串
    ColunmHeader* cur = (ColunmHeader*)root->right;
    while (cur != root) {
        columnState[cur->col - 1] = '1'; // 将能遍历到的列设置为1
        cur = (ColunmHeader*)cur->right;
    }
    return columnState;
}

void DancingMatrix::countSolutions(shared_ptr<ORNode> node) {
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

size_t DancingMatrix::hashFunction(int r, ZDDNode* x, ZDDNode* y)
{
    return std::hash<int>()(r) ^ (std::hash<int>()(x->label) << 1) ^ (std::hash<int>()(y->label) << 2);
}

shared_ptr<ZDDNode> DancingMatrix::unique(int r, shared_ptr<ZDDNode> x, shared_ptr<ZDDNode> y)
{

    std::size_t key = hashFunction(r, x.get(), y.get());
    if (Z.find(key) == Z.end()) {  //如果没有找到解
        shared_ptr<ZDDNode> lo = x;
        shared_ptr<ZDDNode> hi = y;

        //先检查下x和y是否都为终端节点
        if(x->isTerminal && y->isTerminal){
           Z[key] = make_shared<ZDDNode>(r, lo, hi); 
           return Z[key];
        }

        //如果x，y都为分支节点,x指向的是已找到的解, 并且x，y都存在Z中
        if(!x->isTerminal && !y->isTerminal){
            //如果y存在缓存, 说明也存在Z中（DXZ）
            if (Z_a.find(getColumnState()) != Z_a.end()) {
                hi = Z_a[getColumnState()];    
            }

            
           Z[key] = make_shared<ZDDNode>(r, lo, hi); 
           return Z[key]; 
        }
        
        // 如果x或y是终端节点，尝试在另一个节点中找到匹配的终端节点
        if (x->isTerminal) {
            lo = F_ZDD;
        } else if (y->isTerminal) { // 此时说明r覆盖了当前矩阵的所有列
            hi = T_ZDD;
        }

        // 创建新的ZDDNode
        Z[key] = make_shared<ZDDNode>(r, lo, hi);
    }
    return Z[key];
}

shared_ptr<ZDDNode> DancingMatrix::DXZ()
{
    if(root->right == root){
        return T_ZDD;
    }

    std::string columnState = getColumnState();
    // 查找缓存
    if (Z_a.find(columnState) != Z_a.end()) {
        return Z_a[columnState]; 
    }

    ColunmHeader* choose = (ColunmHeader*)root->right;  
    ColunmHeader* cur = choose;
    while( cur != root )  
    {   //选择元素最少的列
        if( choose->size > cur->size )  
            choose = cur;  
        cur = (ColunmHeader*)cur->right;  
    }  

    if( choose->size <= 0 ){
        return F_ZDD;  
    } 
    shared_ptr<ZDDNode> x = F_ZDD;


    cover(choose->col);  //将选中列移出列表
    Node* curC = choose->up;  //curC用来遍历选中列的所有非零行(从下往上)
    while(curC != choose)  //相当于for r such that A[r, c]=1 do
    {
        //printColumnHeaders();
        Node* noteR = curC;  
        Node* curR = curC->right;  
        while( curR != noteR )  
        {  
            cover( curR->col );  
            curR = curR->right;  
        }
        shared_ptr<ZDDNode> y = DXZ();
        if(y->label != -2){
            x = unique(curC->row, x, y);
        }
       
        //printColumnHeaders();
        curR = noteR->left;  
        while( curR != noteR )  
        {  
            uncover( curR->col );  
            curR = curR->left;  
        }  

        curC = curC->up;
    }
    uncover(choose->col);  //回溯
    Z_a[columnState] = x; // 结果存入缓存，实际上在Z中
    return x;
}

void DancingMatrix::startDLX(){

    std::cout << "DLX开始搜索..." << std::endl;
    uint64_t sol = 0;
    auto start = std::chrono::high_resolution_clock::now();
    X(sol);
    auto end = std::chrono::high_resolution_clock::now();
    searchTimeSeconds = std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count();
    std::cout << "搜索完成，找到 " << sol << " 个解。" << std::endl;
    count = sol; // 更新计数
    std::cout << "DLX搜索耗时: " << searchTimeSeconds << " 秒。" << std::endl;
    std::cout<<std::endl;

}

void DancingMatrix::startDXZ(){

    std::cout << "DXZ开始搜索..." << std::endl;
    auto startDXZ = std::chrono::high_resolution_clock::now();
    shared_ptr<ZDDNode> result = DXZ();
    auto endDXZ = std::chrono::high_resolution_clock::now();
    searchTimeSeconds = std::chrono::duration_cast<std::chrono::duration<double>>(endDXZ - startDXZ).count();
    std::cout << "搜索完成，找到 " << count << " 个解。" << std::endl;
    std::cout << "DXZ搜索完成, 耗时: " << searchTimeSeconds << " 秒。" << std::endl;
    std::cout << std::endl;

}

PreProccess::PreProccess() {
   
}

void PreProccess::extractNM(const std::string& line, int& n, int& m) {
    std::istringstream iss(line);
    std::string token;

    // 读取 "c"
    iss >> token;  // 读取 "c"

    iss >> token;  // 读取 "n"
    if (token == "n") {
        iss >> token;  // 读取 "="
        iss >> n;      // 读取 n 的值
    } else {
        throw std::runtime_error("格式错误: 未找到 'n'");
    }

    // 读取 "m" 和其值
    iss >> token;  // 读取 ","
    iss >> token;  // 读取 "m"
    if (token == "m") {
        iss >> token;  // 读取 "="
        iss >> m;      // 读取 m 的值
    } else {
        throw std::runtime_error("格式错误: 未找到 'm'");
    }
}


int** PreProccess::processFileToMatrix1(const std::string& filename, int& r, int& c) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("无法打开文件");
    }

    std::string line;
    std::getline(file, line);  // 读取第一行

    int n, m;
    extractNM(line, n, m);  // 读取第一行的 n 和 m

    r = m;
    c = n;

    std::getline(file, line);  // 跳过第二行(第二个数据集不用跳过)

    // 动态分配二维数组
    int** matrix = new int*[r];
    for (int i = 0; i < r; ++i) {
        matrix[i] = new int[c];
        for (int j = 0; j < c; ++j) {
            matrix[i][j] = 0;  // 初始化为0
        }
    }

    int row = 0;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] != 's') continue;  // 忽略不是以 's' 开头的行
        std::istringstream iss(line.substr(2));  // 跳过前两个字符's '
        //std::istringstream iss(line);
        int col;
        while (iss >> col) {
            if (col > 0 && col <= n) {  // 确保列索引在有效范围内
                matrix[row][col - 1] = 1;  // 由于数组索引从0开始，所以减1
            }
        }
        ++row;
    }

    file.close();
    return matrix;
}


int** PreProccess::processFileToMatrix2(const fs::path& filename, int& r, int& c) {
    std::ifstream file(filename.string());
    if (!file.is_open()) {
        throw std::runtime_error("无法打开文件");
    }

    std::string line;
    std::getline(file, line);  // 读取第一行

    int n, m;
    std::istringstream iss(line);
    iss >> n >> m;  // 假设第一行格式为 "n m"  
    c = n;
    r = m;

    // 动态分配二维数组
    vector<vector<int>> tmp_matrix;

    while (std::getline(file, line)) {
        vector<int> tmp_row(c, 0);  // 初始化当前行
        std::istringstream iss(line);
        int tmp;
        iss >> tmp;   // 跳过开头的数字
        iss >> tmp;  // 读取该行“1”的个数

        int col;
        for (int i = 0; i < tmp; ++i) {
            iss >> col;  // 读取列索引
            if (col > 0 && col <= n) {  // 确保列索引在有效范围内
                tmp_row[col - 1] = 1;  // 由于数组索引从0开始，所以减1
            }
        }
        tmp_matrix.push_back(tmp_row);
    }
    file.close();
    
    r = tmp_matrix.size();
    // 动态分配二维数组
    int** matrix = new int*[r];
    for(int i = 0; i < r; ++i) {
        matrix[i] = new int[c];
        for(int j = 0; j < c; ++j) {
            matrix[i][j] = tmp_matrix[i][j];  // 将1填入矩阵
        }
    }
    return matrix;
}

int** PreProccess::processFileToMatrix3(const std::string& filename, int& r, int& c) { 
    std::ifstream file(filename);
    if(!file.is_open()) {
        throw std::runtime_error("无法打开文件");
    }

    std::string line;
    std::getline(file, line);  // 读取第一行
    std::istringstream iss(line);
    int n, m;
    iss >> n >> m;  // 假设第一行格式为 "n m"
    c = n;
    r = m;

    // 动态分配二维数组
    int** matrix = new int*[r];
    for(int i = 0; i < r; ++i) {
        matrix[i] = new int[c];
        for(int j = 0; j < c; ++j) {
            matrix[i][j] = 0;
        }
    }

    int row = 0;
    while (std::getline(file, line))
    {
        std::istringstream rowStream(line);
        int col, count;

        // 读取该行“1”的个数
        rowStream >> count;

        for (int i = 0; i < count; ++i) {
            rowStream >> col;  // 读取列索引
            if (col > 0 && col <= n) {  // 确保列索引在有效范围内
                matrix[row][col - 1] = 1;  // 由于数组索引从0开始，所以减1
            }
        }
        row++;
    }
    
    file.close();
    return matrix;
}

void PreProccess::freeMatrix(int** matrix, int rows) {
    for (int i = 0; i < rows; ++i) {
        delete[] matrix[i];
    }
    delete[] matrix;
}