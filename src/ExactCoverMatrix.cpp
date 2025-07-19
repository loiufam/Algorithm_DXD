#include "../include/DancingMatrix.h"

//构造函数
DancingMatrix::DancingMatrix( int rows, int cols, int** matrix )  
    : ROWS(rows), COLS(cols), count(0), DNNF_COUNT(0) {  
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
                rowsSet.insert(i);
                colsSet.insert(j+1); // 列数加1
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


void DancingMatrix::initBlock(Block& block){

    for(int col : block.cols){
        ColunmHeader* cur = &ColIndex[col];
        if(cur->size > 1){
            set<int> rows; // 用于存储连接的行
            Node* node = cur->down;
            while(node != cur) {
                rows.insert(node->row);
                node = node->down;
            }
            block.connectedRows.push_back(rows); // 将连接的行集合添加到块中
        }
    }

    // 使用并查集合并交集
    mergeIntersectingSets(block.connectedRows, block.rowToRowsSet);

}

void DancingMatrix::mergeIntersectingSets(vector<set<int>>& connectedRowSets, unordered_map<int, vector<int>>& rowToGroup){
    int n = connectedRowSets.size();
    UnionFind uf(n); // 初始化并查集

    unordered_map<int, int> seen; // 元素 -> 所属集合索引
    for (int i = 0; i < n; ++i) {
        for (int x : connectedRowSets[i]) {
            if (seen.count(x)) {
                uf.unite(i, seen[x]);
            } else {
                seen[x] = i;
            }
        }
    }

    // root -> 所有成员索引
    unordered_map<int, vector<int>> groupMembers;
    for (int i = 0; i < n; ++i) {
        groupMembers[uf.find(i)].push_back(i);
    }

    // 合并所有成员的 set 到 root set 中，清空其他 set
    for (const auto& [root, indices] : groupMembers) {
        if (indices.size() <= 1) continue;
        for (int i : indices) {
            if (i != root) {
                connectedRowSets[root].insert(connectedRowSets[i].begin(), connectedRowSets[i].end());
                connectedRowSets[i].clear(); // 清空已合并的集合
            }
        }
    }

    // 清除空集合
    vector<set<int>> filtered;
    for (const auto& s : connectedRowSets) {
        if (!s.empty()) filtered.push_back(s);
    }
    connectedRowSets = std::move(filtered);

    // 重新构建 rowToGroup
    rowToGroup.clear();
    for (int i = 0; i < connectedRowSets.size(); ++i) {
        for (int x : connectedRowSets[i]) {
            rowToGroup[x].push_back(i);
        }
    }
}

string DancingMatrix::encodeBlockState(const unordered_set<int>& cols){
    string state(COLS, '0'); // 初始化为全0字符串
    for(int col : cols) {
        state[col - 1] = '1'; // 将对应列设置为1   
    }
    return state;
}

// 必须是经过并查集之后，出现多个行冲突分组才可分列
vector<Block> DancingMatrix::spilitBlock(const Block& block){
    
    int n = block.connectedRows.size();
    vector<set<int>> coveredCols(n);

    for(int col : block.cols) {
        ColunmHeader* cur = &ColIndex[col];
        int first_row_id = cur->down->row; // 获取第一个行节点的行号
        vector<int> indexs = block.rowToRowsSet.find(first_row_id)->second; 
        coveredCols[indexs[0]].insert(col); // 将列添加到对应行的集合中
    }

    vector<Block> blocks;
    for(int i = 0; i < n; ++i) {
        Block newBlock(block.connectedRows[i], coveredCols[i]);
        newBlock.connectedRows = {block.connectedRows[i]}; // 只包含当前行的集合
        for(int row : block.connectedRows[i]) {
            newBlock.rowToRowsSet[row] = {0}; 
        }
        blocks.push_back(newBlock);
    }
    return blocks;
}

vector<Block> DancingMatrix::detectBlocks(const Block& currentBlock) {
        
        // 构建行-列关系映射
        unordered_map<int, set<int>> rowToCols;  // 行→列的映射
        unordered_map<int, set<int>> colToRows;  // 列→行的映射
        // 遍历Block的行和列，填充映射
        for(int col : currentBlock.cols) {
            ColunmHeader* cur = &ColIndex[col];
            Node* node = cur->down;
            while(node != cur) {
                if(find(currentBlock.rows.begin(), currentBlock.rows.end(), node->row) != currentBlock.rows.end()) {
                    rowToCols[node->row].insert(cur->col-1);  // 换成矩阵索引
                    colToRows[cur->col-1].insert(node->row);
                }
                node = node->down;
            }
        }

        // 使用BFS找连通分量
        unordered_set<int> visitedRows;
        unordered_set<int> visitedCols;
        vector<Block> blocks;

        for (const auto& [startRow, _] : rowToCols) {
            if (visitedRows.count(startRow)) continue;

            set<int> blockRows;
            set<int> blockCols;
            queue<pair<bool, int>> q; // true: row, false: col
            q.emplace(true, startRow);

            while (!q.empty()) {
                auto [isRow, id] = q.front();
                q.pop();

                if (isRow) {
                    if (visitedRows.count(id)) continue;
                    visitedRows.insert(id);
                    blockRows.insert(id);

                    for (int col : rowToCols[id]) {
                        if (!visitedCols.count(col)) {
                            q.emplace(false, col);
                        }
                    }
                } else {
                    if (visitedCols.count(id)) continue;
                    visitedCols.insert(id);
                    blockCols.insert(id + 1);

                    for (int row : colToRows[id]) {
                        if (!visitedRows.count(row)) {
                            q.emplace(true, row);
                        }
                    }
                }
            }

            if (!blockRows.empty()) {
                blocks.emplace_back(blockRows, blockCols);
            }
        }
        return blocks;
}

// 串行处理每个子块，组合为 分解 节点
shared_ptr<DNNFNode> DancingMatrix::singleSearch(vector<Block>& blocks) {

    // 收集结果并创建AND节点
    auto andNode = std::make_shared<DNNFNode>(NodeType::Decomposed, -1, 0);
    uint64_t totalCount = 1; 

    for (auto& block : blocks) {
        auto res_or_node = singleDXD(block);
        if (res_or_node->label == -2) { 
            return F; // 如果有一个子块返回F，直接返回F
        }
        andNode->children.push_back(res_or_node);
        totalCount *= res_or_node->count;
    }
    
    andNode->count = totalCount;
    return andNode;
}

// 并行处理每个子块，组合为 分解 节点
shared_ptr<DNNFNode> DancingMatrix::parallelSearch(vector<Block>& blocks) {
    isParallelSearch = true; // 标记为并行搜索
    p_count++; // 增加并行搜索计数
    // std::cout << "并行处理 " << blocks.size() << " 个子块..." << std::endl;
    
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

    // 先缓存当前block
    block_cache[std::make_pair(block.cols, c)] = block;

    ColunmHeader* col = &ColIndex[c];  
    col->right->left = col->left;  
    col->left->right = col->right;  
    
    block.cols.erase(c); // 从块中移除列


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
        block.rows.erase(curC->row); // 从块中移除行 
        vector<int> index = block.rowToRowsSet[curC->row];
        for(int idx : index) {
            block.connectedRows[idx].erase(curC->row); // 从连接行集合中移除列
        }
        block.rowToRowsSet.erase(curC->row); // 从行到连接行集合的映射中移除
        
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

        curC = curC->up;  
    }  

    col->right->left = col;  
    col->left->right = col;  

    // 收集需要恢复的行
    unordered_set<int> cols = block.cols; // 获取当前块的列集合
    cols.insert(c);
    auto it = block_cache.find(std::make_pair(cols, c)); // 恢复块状态
    if( it != block_cache.end()) {
        block = it->second; // 恢复块
    }
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