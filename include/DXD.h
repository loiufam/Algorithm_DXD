#ifndef DXD_H
#define DXD_H

#include "../include/DancingMatrix.h"
#include "../include/DXDTime.h"
#include <omp.h>

const int UNCHOOSEN = -10;
const int MIN_BLOCK_ROWS = 2;
const int MAX_BLOCK_ROWS = 150;
const int TIME_LIMIT_SECONDS = 1200; 
const int TIME_LIMIT_BUILDING_SECONDS = 1200;
using namespace std;

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
    std::vector<std::pair<ColumnHeader*, ColumnHeader*>> columnLinks; // 列头的原始链接
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
    DNNFNode(int l, shared_ptr<DNNFNode> r, uint64_t c)  : type(NodeType::Decision), label(l), right(r), count(c) {} // 简化版决策节点

};

// 轻量级结果结构
struct DNNFResult {
    uint64_t count;          // 精确计数（未溢出时使用）
    bool overflowed;         // 是否溢出
    ScientificCount sciCount; // 科学计数法（溢出后使用）
    DNNFNode* node;              // 原有的节点指针
    
    // 从精确值构造
    DNNFResult(uint64_t c) : count(c), overflowed(false), sciCount(c), node(nullptr) {}
    
    // 从科学计数法构造
    DNNFResult(const ScientificCount& sc) : count(0), overflowed(true), sciCount(sc), node(nullptr) {}
    
    // 默认构造（零值）
    DNNFResult() : count(0), overflowed(false), sciCount(0), node(nullptr) {}
    
    bool isZero() const {
        return overflowed ? sciCount.isZero() : (count == 0);
    }
    
    bool isFailure() const { 
        return isZero() && node && node->label == -2; 
    }
    
    std::string toString() const {
        if (overflowed) {
            return sciCount.toString();
        } else {
            return std::to_string(count);
        }
    }
    
    // 乘法操作
    DNNFResult operator*(const DNNFResult& other) const {
        if (this->isZero() || other.isZero()) {
            return DNNFResult(0);
        }
        
        // 如果任意一方已经溢出，使用科学计数法
        if (this->overflowed || other.overflowed) {
            ScientificCount result = this->sciCount * other.sciCount;
            return DNNFResult(result);
        }
        
        // 检查是否会溢出
        if (this->count <= ULLONG_MAX / other.count) {
            return DNNFResult(this->count * other.count);
        } else {
            // 溢出，转换为科学计数法
            ScientificCount result = this->sciCount * other.sciCount;
            return DNNFResult(result);
        }
    }
    
    // 加法操作
    DNNFResult operator+(const DNNFResult& other) const {
        if (this->isZero()) return other;
        if (other.isZero()) return *this;
        
        // 如果任意一方已经溢出，使用科学计数法
        if (this->overflowed || other.overflowed) {
            ScientificCount result = this->sciCount + other.sciCount;
            return DNNFResult(result);
        }
        
        // 检查是否会溢出
        if (this->count <= ULLONG_MAX - other.count) {
            return DNNFResult(this->count + other.count);
        } else {
            // 溢出，转换为科学计数法
            ScientificCount result = this->sciCount + other.sciCount;
            return DNNFResult(result);
        }
    }
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
        DanceDNNF(int rows, int cols, int** matrix, Logger& l, bool verbose = false, int pool_size = 8) 
            : DancingMatrix(rows, cols, matrix, verbose), logger(l) {
            
 
            timer.setTimeBound(TIME_LIMIT_SECONDS);

            std::cout<< "初始化DanceDNNF完成." << endl;
        }

        DanceDNNF(const string& file_path, int from, Logger& l, 
                        bool useIG = false, bool useETT = false, int pool_size = 8, bool debug = false)
            : DancingMatrix(file_path, from, useIG, useETT), logger(l), debug(debug) {

            if(pool_size > 1) {
                omp_set_num_threads(pool_size); // 设置并行线程数
            }
            timer.setTimeBound(TIME_LIMIT_SECONDS);
        }

        ~DanceDNNF() = default;

        CStopWatch timer;   // 计时器

        const int MAX_P_COUNT = 1; // 最大并行搜索次数   
        int p_count; // 记录并行搜索的次数
        int record_detect_count = 0; // 记录第几次检测
        size_t MAX_B_COUNT;
        string cur_instance = ""; // 当前处理的实例名
        double searchTime = 0.0;
        string solutionCount; // 记录解的数量
        bool timeout = false; // 是否超时
        bool isParallelSearch = false; // 是否并行搜索
        double decomposeTime = 0.0;
        bool debug = false;

        vector<set<int>> mergeRowSets(Block& block);
        vector<Block> spilitBlock(const vector<set<int>>& mergeRowSets);
        vector<Block> spilitBlockParallel(const vector<set<int>>& mergeRowSets);

        vector<DXD_Block> detectBlocks(const DXD_Block& currentBlock);
        void batchCover(const std::vector<int>& columns);
        void batchUncover();
        std::shared_ptr<ORNode> make_node(int row);
        std::shared_ptr<ORNode> Search(Node* curC);
        void startSearch(bool verbose = false);

        void batchCoverInBlock(Node* curC, Block& block);
        void batchUncoverInBlock(Block& block);

        // 多线程DLX
        uint64_t parallelSearchMDLX(vector<Block>& blocks);
        void MDLX(vector<int>& sols, uint64_t& count, Block& block);

        DNNFResult DXD(Block& block);
        DNNFResult serialSearch(vector<Block>& blocks);
        shared_ptr<DNNFNode> parallelSearch(vector<Block>& blocks);
        DNNFResult parallelSearchUseOmp(vector<Block>& blocks);
        shared_ptr<DNNFNode> parallelDXD(Block& blocks);

        // 启动搜索函数
        void startDXD();
        void startMultiThreadDXD();
        void start_MDLX_Search();

        bool queryRecord (size_t key) {
            std::shared_lock<std::shared_mutex> readLock(recordMutex);
            return records.find(key) != records.end();
        }

        void insertRecord (size_t key) {
            std::unique_lock<std::shared_mutex> writeLock(recordMutex);
            records.insert(key);
        }

        Block getBlock() {
            Block fullBlock(rowsSet, colsSet);
            return fullBlock;
        };

        std::shared_ptr<DNNFNode> getCache(const size_t& key){
            std::shared_lock<std::shared_mutex> readLock(cacheMutex);
            if(C.find(key) != C.end()){
                return C[key];
            }
            return nullptr;
        }

        void setCache(const size_t& key, std::shared_ptr<DNNFNode> node){
            std::unique_lock<std::shared_mutex> writeLock(cacheMutex);
            if(C.find(key) == C.end()){
                C[key] = node;
            }
        }

        uint64_t getCacheCount(const size_t& key, bool& success){
            std::shared_lock<std::shared_mutex> readLock(cacheMutex);
            auto it = countCache.find(key);
            if(it != countCache.end()){
                return it->second;
            }
            success = false;
            return 0;
        }

        void setCacheCount(const size_t& key, uint64_t count){
            std::unique_lock<std::shared_mutex> writeLock(cacheMutex);
            countCache[key] = count;
        }


    private:

        // ThreadPool& pool;
        Logger& logger;
        shared_ptr<DXDResult> cur_result = make_shared<DXDResult>(); // 当前实例结果
        
        // DNNF相关
        std::shared_ptr<ORNode> rootOR;
        vector<string> cache_input_order; // 记录缓存的输入顺序，便于输出
        std::shared_ptr<DNNFNode> rootDNNF;
        std::unordered_set<size_t> records; // 用于记录无法分解的矩阵状态
        std::shared_ptr<DNNFNode> T = std::make_shared<DNNFNode>(NodeType::Terminal, -1, 1);
        std::shared_ptr<DNNFNode> F = std::make_shared<DNNFNode>(NodeType::Terminal, -2, 0);
        
        
        std::unordered_map<size_t, std::shared_ptr<ORNode>> Cache;
        // DNNF缓存
        mutable std::shared_mutex cacheMutex;
        mutable std::shared_mutex recordMutex; // 记录互斥锁
        unordered_map<size_t, shared_ptr<DNNFNode>> C;
        unordered_map<int, shared_ptr<DNNFNode>> V_Table; // 变量节点缓存

        // 轻量级缓存：只存计数
        unordered_map<size_t, uint64_t> countCache;

        // 操作栈用于批量回溯
        std::stack<CoverOperation> operationStack;
        vector<BatchOperation> batchOpStack;

        ThreadPool& getThreadPool(int poolSize) {
            return ThreadPoolManager::get_instance(poolSize);
        }
};

struct SubMatrixTask {
        row_id selectedRow;
        string input_file;
        int from;
        
        SubMatrixTask(row_id row, const string& file, int f) 
            : selectedRow(row), input_file(file), from(f) {}
};

class DecisionDNNF {
    private:
        vector<SubMatrixTask> tasks;
        ThreadPool& pool;
        int maxConcurrentMatrices;  // 限制同时存在的矩阵数量

    public:

        DecisionDNNF(vector<SubMatrixTask>&& tasks, int maxConcurrent = 4)
            : tasks(std::move(tasks)), pool(getThreadPool()), maxConcurrentMatrices(maxConcurrent) {
            cout << "初始化DecisionDNNF完成, 任务数: " << this->tasks.size() << endl;
        }

        ~DecisionDNNF() {
            tasks.clear();
        }

        shared_ptr<DNNFNode> solveSingle(DancingMatrix& matrix, unordered_map<size_t, shared_ptr<DNNFNode>>& localCache);
        shared_ptr<DNNFNode> solve();

        shared_ptr<DNNFNode> T = std::make_shared<DNNFNode>(NodeType::Terminal, -1, 1);
        shared_ptr<DNNFNode> F = std::make_shared<DNNFNode>(NodeType::Terminal, -2, 0);


    private:
        void applyRowSelection(DancingMatrix& matrix, row_id selectedRow) {
            // 根据选中的行应用覆盖操作
            RowNode* rowHead = matrix.getRowHeader(selectedRow);
            Node* startNode = rowHead->right;
            matrix.cover(startNode->col);
            Node* curC = startNode->right;
            while (curC != startNode) {
                matrix.cover(curC->col);
                curC = curC->right;
            }
        }

        shared_ptr<DNNFNode> mergeResults(vector<future<shared_ptr<DNNFNode>>>& futures) {

            if (futures.empty()) return F;

            auto merged = std::make_shared<DNNFNode>(NodeType::OR, -1, 0);
            
            for (auto& future : futures) {
                try {
                    auto result = future.get();
                    merged->count += result->count;
                    merged->children.push_back(result);
                } catch (const std::exception& e) {
                    // 处理异常
                    cout << "子任务异常: " << e.what() << endl;
                }
            }
            
            return merged;
        }

        ThreadPool& getThreadPool(int maxTheads = thread::hardware_concurrency()) {
            return ThreadPoolManager::get_instance(maxTheads);
        }

};

class ExactCoverSolver {
    public:
        ExactCoverSolver(const string& input_file, int from, Logger& l, int p = thread::hardware_concurrency()) 
            : input_file(input_file), from(from), logger(l) {
                timer.setTimeBound(1800);
                if (p > std::thread::hardware_concurrency()) {
                    poolSize = std::thread::hardware_concurrency();
                } else {
                    poolSize = p;
                }

                std::ifstream ifs(input_file);

                if (!ifs.is_open()) {
                    throw std::runtime_error("无法打开文件: " + input_file);
                }
                
                std::string line, token;
                std::getline(ifs, line);
                std::istringstream iss(line);
                int rows, cols;
                
                if (from == 1) {
                    iss >> token >> token >> token >> cols;
                    iss >> token >> token >> token >> rows;
                    std::getline(ifs, line);
                } else {
                    iss >> cols >> rows;
                }
                
                if (rows > MAX_ROW) {
                    throw std::runtime_error("矩阵行数过大: " + std::to_string(rows));
                }
                
                matrix.resize(cols + 1);

                int cur_row = 0;
                while (std::getline(ifs, line) && cur_row < rows) {
                    if (line.empty()) continue;
                    
                    std::istringstream row_iss(line);
                    
                    if (from == 1 || from == 3) {
                        row_iss >> token;
                    } else if (from == 2) {
                        row_iss >> token >> token;
                    }
                    
                    int col;
                    while (row_iss >> col) {
                        if (col > 0 && col <= cols) {
                            matrix[col].push_back(cur_row);
                        }
                    }
                    cur_row++;
                }
            }
        ~ExactCoverSolver() = default;
        string cur_instance = ""; // 当前处理的实例名

        shared_ptr<ExperimentResult> searchEC();

        const std::vector<row_id>& getAssignCol(col_id& selectedCol);
    
    private:
        string input_file;
        int from;
        std::vector<std::vector<row_id>> matrix;
        int poolSize;
        Logger& logger;
        CStopWatch timer;   // 计时器
        shared_ptr<ExperimentResult> cur_result = make_shared<ExperimentResult>(); // 当前实例结果
};

// 信号量类，用于控制并发数
class Semaphore {
    private:
        std::mutex mtx;
        std::condition_variable cv;
        int count;
        
    public:
        explicit Semaphore(int count) : count(count) {}
        
        void acquire() {
            std::unique_lock<std::mutex> lock(mtx);
            cv.wait(lock, [this] { return count > 0; });
            --count;
        }
        
        void release() {
            std::unique_lock<std::mutex> lock(mtx);
            ++count;
            cv.notify_one();
        }
};

#endif