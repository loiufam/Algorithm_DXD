#include "../include/DancingMatrix.h"
#include "../include/ConnectedGraph.h"

//构造函数
DancingMatrix::DancingMatrix( int rows, int cols, int** matrix )  
    : ROWS(rows), COLS(cols) {
    EXIST_ROWS = rows;  
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
    

    // graph = std::make_shared<ConnectedGraph>(ROWS, COLS);
    for( int i = 0; i < rows; i++ ){
        for( int j = 0; j < cols ; j++ ) {  
            if(matrix[i][j] == 1){
                insert(  i , j+1 );  //行数与原矩阵相同，而列数加1
                ONE_COUNT++; // 统计矩阵中1的个数
                rowsSet.insert(i);
                colsSet.insert(j+1); // 列数加1
                // rowToColsSet[i].insert(j+1);
                // colToRowsSet[j+1].insert(i);
            }
        }
    }

    cout<< "初始化舞蹈链完成." << endl;
}

// 从文件构造舞蹈链矩阵
DancingMatrix::DancingMatrix( const string& file_path, int from, bool verbose )
{
    ifstream file(file_path);
    if (!file.is_open()) {
        cerr << "无法打开文件: " << file_path << endl;
        throw runtime_error("无法打开文件");
    }

    string line;
    getline(file, line);  // 读取第一行 

    int rows, cols;
    if( from == 1 ) {
        PreProccess::extractNM( line, cols, rows );
        getline(file, line); 
    } else {
        istringstream iss(line);
        iss >> cols >> rows;
    }

    ROWS = rows;
    COLS = cols;
    if (ROWS > MAX_ROW) {
        cerr << "矩阵行数过大，无法处理: " << ROWS << " 行." << endl;
        throw runtime_error("矩阵行数过大");
    }
    // 初始化列哈希表
    for(int col = 1; col <= COLS; col++){
        colHash[col] = std::hash<int>()(col);
    }
    // graph = std::make_shared<ConnectedGraph>(ROWS, COLS); 

    // cout << "矩阵维度: " << rows << " 行, " << cols << " 列." << endl;

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

    int currentRow = 0;
    while (getline(file, line)) {
        if (line.empty()) continue; // 跳过空行
        istringstream iss(line);

        string token;
        if (from == 1 || from == 3) {
            iss >> token;
        } else if (from == 2) {
            iss >> token;
            iss >> token;
        }

        int currentCol; // 列索引从1开始
        while(iss >> currentCol) {
            if (currentCol < 1 || currentCol > cols) {
                cerr << "无效的列索引: " << currentCol << " 在行 " << currentRow + 1 << endl;
                exit(1);
            }
            insert(currentRow, currentCol); // 插入节点
            ONE_COUNT++; // 统计矩阵中1的个数
            rowsSet.insert(currentRow);
            colsSet.insert(currentCol); 
        }
        currentRow++;
        if (currentRow >= rows) break; // 防止超过预期行数
    }

    // cout<< "初始化舞蹈链完成." << endl;
    computeInitialHash();
    if(verbose) graph = make_shared<ConnectedGraph>(*this);
    file.close();
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

//插入元素到双向十字链表中
void DancingMatrix::insert( int r, int c )  
{  
    Node* cur = &ColIndex[c];  
    ColIndex[c].size++;  
    RowIndex[r].size++;
    // ColIndex[c].rows.insert(r);
    RowIndex[r].cols.insert(c);
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
    // graph->insertEdge(r, c-1); 
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

//获取当前列的状态
size_t DancingMatrix::getColumnState() const {
    size_t hash = 0;
    ColunmHeader* cur = (ColunmHeader*)root->right;
    while (cur != root) {
        hash ^= std::hash<int>()(cur->col) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        cur = (ColunmHeader*)cur->right;
    }
    return hash;
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
    // colsSet.erase(c); // 从当前矩阵移除该列
    currentColState ^= colHash[c];
    
    Node* curR, *curC;  
    curC = col->down;  
    while( curC != col )  
    {   
        Node* noteR = curC;  
        // RowIndex[noteR->row].cols.erase(noteR->col);
        // removeCol(noteR->row, noteR->col);
        curR = noteR->right;  
        while( curR != noteR )  
        {  
            curR->down->up = curR->up;  
            curR->up->down = curR->down;  
            --ColIndex[curR->col].size;  

            // RowIndex[noteR->row].cols.erase(curR->col);
            // removeCol(noteR->row, curR->col);
            curR = curR->right;  
        }  
        rowsSet.erase(curC->row);
        // connectedGraph->remove(curC->row);
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
        // RowIndex[noteR->row].cols.insert(noteR->col);
        // restoreCol(noteR->row, noteR->col);
        curR = curC->left;  
        while( curR != noteR )  
        {  
            ++ColIndex[curR->col].size;  
            curR->down->up = curR;  
            curR->up->down = curR;  

            // RowIndex[noteR->row].cols.insert(curR->col);
            // restoreCol(noteR->row, curR->col);
            curR = curR->left;  
        }  
        rowsSet.insert(curC->row);
        // connectedGraph->restore(curC->row);
        curC = curC->up;  
    }  
    col->right->left = col;  
    col->left->right = col;  
    currentColState ^= colHash[c];
    // colsSet.insert(col->col);

}

ColunmHeader* DancingMatrix::selectColumnHeuristic(const unordered_set<int>& cols) {
    ColunmHeader* chosen = nullptr;
    int minSize = INT_MAX;
    // vector<int> colVec(cols.begin(), cols.end());
    // sort(colVec.begin(), colVec.end()); // 对列进行排序，确保每次选择的顺序一致
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

col_id DancingMatrix::getClosedSizeCol(const int expected_size) {
    ColunmHeader* choose = (ColunmHeader*)root->right, *cur=choose;  
    while( cur != root )  
    {   //选择接近预期大小的列
        if( abs(choose->size - expected_size) > abs(cur->size - expected_size) )  
            choose = cur;  
        cur = (ColunmHeader*)cur->right;  
    } 
    return choose->col;
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