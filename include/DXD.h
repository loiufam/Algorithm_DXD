#ifndef DXD_H
#define DXD_H

#include "../include/DancingMatrix.h"
#include "../include/DXDTime.h"

const int UNCHOOSEN = -10;
const int MIN_BLOCK_ROWS = 5;
const int MAX_BLOCK_ROWS = 150;
const int TIME_LIMIT_SECONDS = 1000; 

struct ORNode;  

struct ANDNode {
    int row;
    std::shared_ptr<ORNode> next;
    ANDNode() : row(UNCHOOSEN), next(nullptr) {}
    ANDNode(int row) : row(row), next(nullptr) {}
};

struct ORNode {
    int label;  // -1代表T，-2代表F
    std::shared_ptr<ANDNode> left;
    std::shared_ptr<ANDNode> right;
    
    ORNode(int label) : label(label), left(nullptr), right(nullptr) {}
    ORNode(int label, std::shared_ptr<ANDNode> left, std::shared_ptr<ANDNode> right)
        : label(label), left(left), right(right) {}
};

// 操作记录用于批量回溯
struct CoverOperation {
    std::vector<int> columns;           // 被覆盖的列索引
    std::vector<Node*> coveredNodes;
    std::vector<std::pair<ColunmHeader*, ColunmHeader*>> columnLinks; // 列头的原始链接
};

enum class NodeType { OR, Decision, Decomposed, Variable, Terminal };  // 节点类型 AND node 分为Decision和Decomposed两种

struct DNNFNode {
    NodeType type;
    int label; // -1 for T, -2 for F
    uint64_t count;
    vector<std::shared_ptr<DNNFNode>> children; // 记录行id
    std::shared_ptr<DNNFNode> left;
    std::shared_ptr<DNNFNode> right;

    DNNFNode() = default;
    DNNFNode(NodeType t, int l) : type(t), label(l) {} // 构建变量节点和终端节点
    DNNFNode(NodeType t, int l, uint64_t c) : type(t), label(l), count(c) {} // 构造函数用于OR节点和 Decomposed类型的AND节点   
    DNNFNode(NodeType t, shared_ptr<DNNFNode> l, shared_ptr<DNNFNode> r) : type(t), left(l), right(r) {} // 构建Decision类型的AND节点

};

// 生成block hash key
struct SetIntHash {
    std::size_t operator()(const std::pair<std::unordered_set<int>, int>& p) const {
        const std::unordered_set<int>& s = p.first;
        int val = p.second;

        // 将 unordered_set 转为 vector 并排序，以确保哈希稳定性
        std::vector<int> sorted(s.begin(), s.end());
        std::sort(sorted.begin(), sorted.end());

        // 使用组合哈希
        std::size_t seed = 0;
        for (int x : sorted) {
            seed ^= std::hash<int>()(x) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        }

        // 加上 int 值
        seed ^= std::hash<int>()(val) + 0x9e3779b9 + (seed << 6) + (seed >> 2);

        return seed;
    }
};


struct Block {
        unordered_set<int> rows;  // 舞蹈链行id集合 
        unordered_set<int> cols;  // 从1开始编号,对应舞蹈链列数id
        bool is_spilited = false;
        
        Block() = default;

        Block(const unordered_set<int>& r, const unordered_set<int>& c) :  rows(r), cols(c) {}

        Block(const set<int>& r, const set<int>& c, bool is_spilited = true){
            rows.insert(r.begin(), r.end());
            cols.insert(c.begin(), c.end());
        }

        Block(const vector<int>& r, const vector<int>& c){
            rows.insert(r.begin(), r.end());
            cols.insert(c.begin(), c.end());
        }

};

struct DXD_Block {

        // row id from 0, column id from 1
        unordered_set<int> cols;  // 选择列
        unordered_map<int, set<int>> rowToCols;  // 行→列的映射
        unordered_map<int, set<int>> colToRows;  // 列→行的映射

        DXD_Block(const unordered_set<int>& r, const unordered_map<int, set<int>>& rToC, const unordered_map<int, set<int>>& cToR) 
            : cols(r), rowToCols(rToC), colToRows(cToR){}

        void print(const string& name = "Block") const {
            cout << "=== " << name << " ===" << endl;
            cout << "Rows: " << rowToCols.size() << ", Cols: " << cols.size() << endl;
            
            // 打印列集合
            vector<int> sortedCols(cols.begin(), cols.end());
            sort(sortedCols.begin(), sortedCols.end());
            cout << "Columns: ";
            for (size_t i = 0; i < sortedCols.size(); ++i) {
                cout << sortedCols[i];
                if (i < sortedCols.size() - 1) cout << ",";
            }
            cout << endl;
            
            // 打印行→列映射
            vector<int> sortedRows;
            for (const auto& [row, _] : rowToCols) {
                sortedRows.push_back(row);
            }
            sort(sortedRows.begin(), sortedRows.end());
            
            for (int row : sortedRows) {
                cout << "R" << row << ": ";
                const auto& colSet = rowToCols.at(row);
                vector<int> rowCols(colSet.begin(), colSet.end());
                sort(rowCols.begin(), rowCols.end());
                for (size_t i = 0; i < rowCols.size(); ++i) {
                    cout << rowCols[i];
                    if (i < rowCols.size() - 1) cout << ",";
                }
                cout << endl;
            }
            cout << endl;
        }
};

struct BatchOperation {
    // size_t state;            // 覆盖前的矩阵状态哈希值
    vector<int> coveredRows;           // 被覆盖的列索引   
    vector<int> coveredCols;       // 被覆盖的列索引  

    BatchOperation() = default;
};

struct DXDFrame {
    enum Stage { START, DECOMPOSED_HANDLED, CHOOSE_ASSIGNED, ITER_ROWS, AFTER_CHILD, FINISH } stage;
    Block block; // local copy of the Block for this frame
    size_t stateHash = 0; // cached state hash
    std::shared_ptr<DNNFNode> cachedResult = nullptr; // if looked up from cache


    // for decomposition handling
    std::vector<Component> comps; // if decomposition is found
    std::vector<std::shared_ptr<DNNFNode>> collectedChildren; // for serialSearch


    // for normal choose/OR processing
    ColunmHeader* choose = nullptr;
    std::shared_ptr<DNNFNode> orNode = nullptr;
    // iteration cursor over rows under chosen column
    ColunmHeader* choosePtr = nullptr; 


    std::vector<int> rowsUnderChoose; // snapshot of row ids under choose at time of selection
    size_t rowCursor = 0; // next row to process


    // after child returns, childResult will be placed here by the driver
    std::shared_ptr<DNNFNode> childResult;


    DXDFrame(const Block &b): stage(START), block(b) {}
};

// 设计线程安全的栈
class ThreadSafeStack {

public:
    ThreadSafeStack() = default;

    void push(const BatchOperation& operation) {
        operationStack.push_back(operation);
    }

    BatchOperation pop() {
        BatchOperation operation = operationStack.back();
        operationStack.pop_back();
        return operation;
    }

    bool empty() const {
        return operationStack.empty();
    }

private:
    thread_local static vector<BatchOperation> operationStack;
};

class DanceDNNF : DancingMatrix { 

    public:
        DanceDNNF(int rows, int cols, int** matrix) 
            : DancingMatrix(rows, cols, matrix) {
            root = getRoot();
            ColIndex = getColIndex();
            RowIndex = getRowIndex();
            connectedGraph = getConnectedGraph();
            timer.setTimeBound(TIME_LIMIT_SECONDS);

            cout<< "初始化DanceDNNF完成." << endl;
        }

        DanceDNNF(const string& file_path, int from, bool useMultiThread = false)
            : DancingMatrix(file_path, from) {
            root = getRoot();
            ColIndex = getColIndex();
            RowIndex = getRowIndex();
            connectedGraph = getConnectedGraph();
            // timer.setTimeBound(TIME_LIMIT_SECONDS);

            cout<< "初始化DanceDNNF完成." << endl;
        }

        ~DanceDNNF() {
            clearSingleCache();
            clearCache();
            Cache.clear();
            V_Table.clear();
            block_cache.clear();
        }

        shared_ptr<ConnectedGraph> connectedGraph;
        CStopWatch timer;   // 计时器

        int MAX_P_COUNT = 2; // 最大并行搜索次数   
        int p_count = 0; // 记录并行搜索的次数
        size_t MAX_B_COUNT = 1;
        bool isParallelSearch = false; // 是否已分解

        vector<set<int>> mergeRowSets(Block& block);
        vector<Block> spilitBlock(const vector<set<int>>& mergeRowSets);
        vector<Block> spilitBlockParallel(const vector<set<int>>& mergeRowSets);
        vector<Block> spilit(const vector<vector<int>>& rows);
        void printBlocks(const vector<Block>& blocks);
        void printBlock(const Block& block);

        vector<DXD_Block> detectBlocks(const DXD_Block& currentBlock);
        void batchCover(const std::vector<int>& columns);
        void batchUncover();
        std::shared_ptr<ORNode> make_node(int row);
        std::shared_ptr<ORNode> Search(Node* curC);
        void startSearch(bool verbose = false);

        void coverInBlock(int c, Block& block);
        void uncoverInBlock(int c, Block& block);
        void batchCoverInBlock(Node* curC, Block& block);
        void batchUncoverInBlock(Block& block);
        vector<int> collectColsInRow(int row, const Block &block);
        vector<int> collectRowsUnderColumn(int col, const Block &block);
        shared_ptr<DNNFNode> DXD(Block& block);
        shared_ptr<DNNFNode> DXD_iterative(Block&& rootBlock);
        shared_ptr<DNNFNode> serialSearch(vector<Component>& components);
        shared_ptr<DNNFNode> serialSearch_iterative(const vector<Component>& components);
        shared_ptr<DNNFNode> dxdSearch(vector<Block>& blocks);
        shared_ptr<DNNFNode> parallelDXD(Block& blocks);
        shared_ptr<DNNFNode> parallelSearch(vector<Block>& blocks);

        // 启动搜索函数
        void startDXD();
        void startMultiThreadDXD();

        void traverseDNNF(const std::shared_ptr<DNNFNode>& node, std::vector<int>& solution);
        void countSolutions(shared_ptr<ORNode> node);

        Block getBlock() {
            Block fullBlock(rowsSet, colsSet);
            return fullBlock;
        };

        std::shared_ptr<DNNFNode> getInSingleCache(const size_t& key){
            if(CacheST.find(key) != CacheST.end()){
                return CacheST[key];
            }
            return nullptr;
        }
        void setInSingleCache(const size_t& key, std::shared_ptr<DNNFNode> node){
            CacheST[key] = node;
        }
        
        void clearSingleCache(){
            CacheST.clear();
        }
        // 多线程安全的缓存访问
        std::shared_ptr<DNNFNode> getCachedResult(const size_t& key) {
            // std::lock_guard<std::mutex> lock(cacheMutex);
            auto it = CacheMT.find(key);
            return (it != CacheMT.end()) ? it->second : nullptr;
        }
        void setCachedResult(const size_t& key, std::shared_ptr<DNNFNode> node) {
            // std::lock_guard<std::mutex> lock(cacheMutex);
            CacheMT[key] = node;
        }
        void clearCache(){
            CacheMT.clear();
        }

        bool shouldDecompose(const Block& block) const {
            return block.rows.size() >= MIN_BLOCK_ROWS && block.rows.size() <= MAX_BLOCK_ROWS && p_count < MAX_P_COUNT;
        }

        void incrementPCount() {
            std::lock_guard<std::mutex> lock(countMutex);
            p_count += 1;
        }

    private:
        // 父类成员
        ColunmHeader* root;  
        ColunmHeader* ColIndex;  
        RowNode* RowIndex; 
        ThreadPool pool;
        
        // DNNF相关
        std::shared_ptr<ORNode> rootOR;
        vector<string> cache_input_order; // 记录缓存的输入顺序，便于输出
        std::shared_ptr<DNNFNode> rootDNNF;
        std::unordered_set<size_t> detect_records; // 用于记录无法分解的矩阵状态
        std::shared_ptr<DNNFNode> T = std::make_shared<DNNFNode>(NodeType::Terminal, -1, 1);
        std::shared_ptr<DNNFNode> F = std::make_shared<DNNFNode>(NodeType::Terminal, -2, 0);
          
        
        std::unordered_map<size_t, std::shared_ptr<ORNode>> Cache;
        // DNNF缓存
        unordered_map<size_t, shared_ptr<DNNFNode>> CacheST;  // 单线程
        unordered_map<size_t, shared_ptr<DNNFNode>> CacheMT;  // 多线程
        unordered_map<int, shared_ptr<DNNFNode>> V_Table; // 变量节点缓存

        // Block缓存
        std::unordered_map<std::pair<std::unordered_set<int>, int>, Block, SetIntHash> block_cache;; // 用于存储Block的缓存，key为行集合和列数的组合，value为Block对象

        std::mutex cacheMutex; // 缓存访问的互斥锁
        std::mutex countMutex; // 计数的互斥锁
        // 操作栈用于批量回溯
        std::stack<CoverOperation> operationStack;
        vector<BatchOperation> batchOpStack;
};

class DecisionDNNF {
    private:
        vector<unique_ptr<DancingMatrix>> matrices;
        ThreadPool& pool;

    public:
        DecisionDNNF(vector<unique_ptr<DancingMatrix>>&& matrices)
            : matrices(std::move(matrices)), pool(getThreadPool()) {
            
            cout<< "初始化DecisionDNNF完成." << endl;
        }

        ~DecisionDNNF() {
            matrices.clear();
        }

        shared_ptr<DNNFNode> solveSingle(DancingMatrix& matrix, unordered_map<size_t, shared_ptr<DNNFNode>>& localCache);
        shared_ptr<DNNFNode> solve();

        shared_ptr<DNNFNode> T = std::make_shared<DNNFNode>(NodeType::Terminal, -1, 1);
        shared_ptr<DNNFNode> F = std::make_shared<DNNFNode>(NodeType::Terminal, -2, 0);


    private:

        ThreadPool& getThreadPool(int maxTheads = thread::hardware_concurrency()) {
            return ThreadPoolManager::get_instance(maxTheads);
        }

};

class ExactCoverSolver {
    public:
        ExactCoverSolver(const string& input_file, int from) : input_file(input_file), from(from) {}
        ~ExactCoverSolver() = default;

        void searchEC();
    
    private:
        string input_file;
        int from;
};

#endif