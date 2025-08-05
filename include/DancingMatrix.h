#ifndef DANCINGMATRIX_H
#define DANCINGMATRIX_H

#include "ThreadPool.h"
#include <iostream>
#include <vector>
#include <string>
#include <set>
#include <queue>
#include <bitset>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <deque>
#include <stack>
#include <algorithm>
#include <memory>
#include <numeric>
#include <functional>
#include <climits>
#include <execution>
using namespace std;

const int UNCHOOSEN = -10;
const int MIN_BLOCK_COLS = 1;
const int MAX_BLOCK_COLS = 80;

struct Node  
{  
    Node* left, *right, *up, *down;  
    int col, row;  
    Node(){  
        left = NULL; right = NULL;  
        up = NULL; down = NULL;  
        col = 0; row = 0;  
    }  
    Node( int r, int c )  
    {  
        left = NULL; right = NULL;  
        up = NULL; down  = NULL;  
        col = c; row = r;  
    }  
};  

struct ColunmHeader : public Node  
{  
    int size;
    set<int> rows;
    ColunmHeader() : size(0) {
    }  
}; 

struct RowNode : public Node
{
    int size;
    set<int> cols;
    RowNode() : size(0) {
    }
};

// ZDD相关
struct ZDDNode {
    int label;
    shared_ptr<ZDDNode> lo;
    shared_ptr<ZDDNode> hi;
    bool isTerminal;

    ZDDNode(int label, shared_ptr<ZDDNode> lo, shared_ptr<ZDDNode> hi, bool isTerminal = false)
        : label(label), lo(lo), hi(hi), isTerminal(isTerminal) {}
    
    // 拷贝构造函数
    ZDDNode(const ZDDNode& other) : label(other.label), isTerminal(other.isTerminal), lo(other.lo), hi(other.hi) {}

};

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

struct ColumnComparator {
    bool operator()(const std::pair<int, ColunmHeader*>& a, const std::pair<int, ColunmHeader*>& b) const {
        if (a.first != b.first) {
            return a.first < b.first;  // 一级排序：size从小到大
        }
        return a.second->col < b.second->col;  // 二级排序：列id从小到大
    }
};


enum class NodeType { OR, Decision, Decomposed, Variable, Terminal };  // 节点类型 AND node 分为Decision和Decomposed两种

struct DNNFNode {
    NodeType type;
    int label; // -1 for T, -2 for F
    uint64_t count;
    vector<std::shared_ptr<DNNFNode>> children; // 记录行id
    std::shared_ptr<DNNFNode> left;
    std::shared_ptr<DNNFNode> right; 

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

class FastBitSet {
private:
    std::vector<uint64_t> bits;
    size_t bit_count;
    
public:
    FastBitSet(size_t size = 0) : bit_count(size) {
        bits.resize((size + 63) / 64, 0);
    }
    
    void resize(size_t size) {
        bit_count = size;
        bits.resize((size + 63) / 64, 0);
    }
    
    void set(size_t pos) {
        if (pos < bit_count) {
            bits[pos / 64] |= (1ULL << (pos % 64));
        }
    }
    
    bool test(size_t pos) const {
        return pos < bit_count && (bits[pos / 64] & (1ULL << (pos % 64)));
    }
    
    bool intersects(const FastBitSet& other) const {
        size_t min_size = std::min(bits.size(), other.bits.size());
        for (size_t i = 0; i < min_size; ++i) {
            if (bits[i] & other.bits[i]) return true;
        }
        return false;
    }
    
    void clear_all() {
        std::fill(bits.begin(), bits.end(), 0);
    }
    
    size_t count() const {
        size_t result = 0;
        for (uint64_t b : bits) {
            result += __builtin_popcountll(b);
        }
        return result;
    }
    
    std::vector<int> get_set_bits() const {
        std::vector<int> result;
        for (size_t i = 0; i < bit_count; ++i) {
            if (test(i)) {
                result.push_back(i);
            }
        }
        return result;
    }
};

struct Block {
        unordered_set<int> rows;  // 从0开始编号
        unordered_set<int> cols;  // 从1开始编号,对应舞蹈链列数id
        // vector<set<int>> connectedRows; // 连接的行集合(set.size > 1)
        // unordered_map<int, vector<int>> rowToRowsSet; // 行到连接行集合的映射
        bool is_spilited = false;
        
        Block() = default;

        Block(const unordered_set<int>& r, const unordered_set<int>& c) :  rows(r), cols(c) {}

        Block(const set<int>& r, const set<int>& c, bool is_spilited = true){
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

struct OptimizedBlock : public Block {
    // 预计算的连通性信息
    mutable std::vector<FastBitSet> col_row_bitsets;  // 每列的行BitSet
    mutable bool connectivity_cached = false;
    mutable std::chrono::steady_clock::time_point last_update;
    vector<set<int>> connectedRows;
    unordered_map<int, vector<int>> rowToRowsSet;
    
    OptimizedBlock() : Block() {
        last_update = std::chrono::steady_clock::now();
    }
    
    OptimizedBlock(const std::set<int>& r, const std::set<int>& c) : Block(r, c) {
        last_update = std::chrono::steady_clock::now();
    }

    OptimizedBlock(const std::unordered_set<int>& r, const std::unordered_set<int>& c) : Block(r, c) {
        last_update = std::chrono::steady_clock::now();
    }
    
    // 标记需要重新计算连通性
    void invalidate_connectivity() const {
        connectivity_cached = false;
        last_update = std::chrono::steady_clock::now();
    }
    
    // 检查连通性是否需要更新
    bool needs_connectivity_update() const {
        auto now = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_update);
        return !connectivity_cached || duration.count() > 100; // 100ms缓存过期  
    }
};

struct BatchOperation {
        std::vector<int> columns;           // 被覆盖的列索引
        Block oldBlock; 
        std::vector<Node*> coveredNodes;
        std::vector<std::pair<ColunmHeader*, ColunmHeader*>> columnLinks;
};

struct ThreadLocalBatchOp {
    vector<BatchOperation> operationStack;
};

// 并查集结构
struct UnionFind {
    vector<int> parent, size;  
    
    UnionFind(int n) : parent(n), size(n, 1) {
        iota(parent.begin(), parent.end(), 0);
    }
    
    int find(int x) {
        if (parent[x] != x) {
            parent[x] = find(parent[x]);  // 路径压缩
        }
        return parent[x];
    }
    
    bool unite(int a, int b) {
        int pa = find(a), pb = find(b);
        if (pa == pb) return false;
        
        // 按size合并，保持树的平衡
        if (size[pa] < size[pb]) swap(pa, pb);
        parent[pb] = pa;
        size[pa] += size[pb];
        return true;
    }
};

class DancingMatrix  
{  
    public:  
        int ROWS, COLS; 
        int MAX_P_COUNT = 5; // 最大并行搜索次数
        std::uint64_t count;  // 统计范围更大 
        std::uint64_t ONE_COUNT = 0; // 统计矩阵中1的个数
        int p_count = 0; // 记录并行搜索的次数
        double searchTimeSeconds = 0.0;
        double countTimeSeconds = 0.0;
        std::vector<std::vector<int>> solutions; 
        unordered_set<int> rowsSet;  // 原始矩阵行
        unordered_set<int> colsSet;  // 原始矩阵列
        unordered_map<int, set<int>> rowToColsSet;
        unordered_map<int, set<int>> colToRowsSet;
        bool isParallelSearch = false; // 是否已分解
        void fastInitBlock(OptimizedBlock& block);
        vector<set<int>> mergeRowSets(Block& block);
        vector<Block> spilitBlock(const vector<set<int>>& mergeRowSets);
        vector<Block> spilitBlockParallel(const vector<set<int>>& mergeRowSets);
        std::vector<OptimizedBlock> intelligentSplitBlock(const OptimizedBlock& block);
        void printBlocks(const vector<Block>& blocks);
        void printBlock(const Block& block);


        //接收矩阵其及维度  
        DancingMatrix( int rows, int cols, int** matrix);  
        //释放内存  
        ~DancingMatrix();  
        vector<DXD_Block> detectBlocks(const DXD_Block& currentBlock);
        void build_mapping_from_cols(const unordered_set<int>& blockCols, unordered_map<int, set<int>>& rowToCols, unordered_map<int, set<int>>& colToRows);
        std::vector<std::vector<int>> detect_components(const OptimizedBlock& block);
        //在行r列c的位置上插入一个元素  
        void insert( int r, int c );  
        void printMatrix() const; 
        void cover( int c );  
        void uncover( int c ); 
        string getColumnState() const;
        string encodeBlockState(const unordered_set<int>& cols);
        size_t hashBlockState(const unordered_set<int>& cols);
     
        void batchCover(const std::vector<int>& columns);
        void batchUncover();
        std::shared_ptr<ORNode> make_node(int row);
        std::shared_ptr<ORNode> Search(Node* curC);
        void startSearch(bool verbose = false);

        void DLX(std::vector<int>& solution);
        void X(uint64_t& count);  // 搜索但不记录解
        // ZDD 相关方法
        size_t hashFunction(int r, ZDDNode* x, ZDDNode* y);
        shared_ptr<ZDDNode> unique(int r, shared_ptr<ZDDNode> x, shared_ptr<ZDDNode> y);
        shared_ptr<ZDDNode> DXZ();
        void startDLX();
        void startDXZ();
        // DNNF 相关方法
        ColunmHeader* selectCol();
        ColunmHeader* selectColumnHeuristic(const unordered_set<int>& cols);
        // ColunmHeader* fastSelect();

        void coverInBlock(int c, Block& block);
        void uncoverInBlock(int c, Block& block);
        void batchCoverInBlock(Node* curC, Block& block, ThreadLocalBatchOp& localOp);
        void batchUncoverInBlock(Block& block, ThreadLocalBatchOp& localOp);
        std::shared_ptr<DNNFNode> DXD(Block& block, ThreadLocalBatchOp& localOp);
        shared_ptr<DNNFNode> dxdSearch(vector<Block>& blocks);
        std::shared_ptr<DNNFNode> serialSearch(std::vector<OptimizedBlock>& blocks);
        shared_ptr<DNNFNode> parallelDXD(Block& blocks);
        shared_ptr<DNNFNode> parallelSearch(vector<Block>& blocks);
        std::shared_ptr<DNNFNode> adaptiveParallelSearch(std::vector<OptimizedBlock>& blocks);
        std::shared_ptr<DNNFNode> optimizedDXD(OptimizedBlock& block, ThreadLocalBatchOp& localOp);
        // 启动搜索函数
        void startDXD();
        void startMultiThreadDXD();
        void startOptimizedDXD();

        void traverseDNNF(const std::shared_ptr<DNNFNode>& node, std::vector<int>& solution);
        void countSolutions(shared_ptr<ORNode> node);
        void printSolution(); 

        std::shared_ptr<DNNFNode> getInSingleCache(const size_t& key){
            if(C.find(key) != C.end()){
                return C[key];
            }
            return nullptr;
        }
        void setInSingleCache(const size_t& key, std::shared_ptr<DNNFNode> node){
            C[key] = node;
        }
        
        void clearSingleCache(){
            C.clear();
        }
        // 线程安全的缓存访问
        std::shared_ptr<DNNFNode> getCachedResult(const size_t& key) {
            std::lock_guard<std::mutex> lock(cacheMutex);
            auto it = C_a.find(key);
            return (it != C_a.end()) ? it->second : nullptr;
        }
        void setCachedResult(const size_t& key, std::shared_ptr<DNNFNode> node) {
            std::lock_guard<std::mutex> lock(cacheMutex);
            C_a[key] = node;
        }
        void clearCache(){
            C_a.clear();
        }

    private:  
        ColunmHeader* root;  
        ColunmHeader* ColIndex;  
        RowNode* RowIndex; 
        friend class OptimizedDancingMatrix;
        std::atomic<size_t> decomposition_count{0};
        std::atomic<size_t> parallel_search_count{0};
        
        // 性能阈值
        static constexpr size_t MIN_PARALLEL_BLOCKS = 3;
        static constexpr size_t MIN_BLOCK_COMPLEXITY = 20;
        static constexpr size_t MAX_SERIAL_DEPTH = 5;
        // DNNF相关
        std::shared_ptr<ORNode> rootOR;
        vector<string> cache_input_order; // 记录缓存的输入顺序，便于输出
        std::shared_ptr<DNNFNode> rootDNNF;
        std::unordered_set<size_t> detect_records; // 用于记录无法分解的矩阵状态
        std::shared_ptr<DNNFNode> T = std::make_shared<DNNFNode>(NodeType::Terminal, -1, 1);
        std::shared_ptr<DNNFNode> F = std::make_shared<DNNFNode>(NodeType::Terminal, -2, 0);
          
        // ZDD
        shared_ptr<ZDDNode> T_ZDD = make_shared<ZDDNode>(-1, nullptr, nullptr, true); // ZDD的T节点
        shared_ptr<ZDDNode> F_ZDD = make_shared<ZDDNode>(-2, nullptr, nullptr, true); // ZDD的F节点
        std::unordered_map<size_t, shared_ptr<ZDDNode>> Z; // ZDD缓存
        std::unordered_map<std::string, shared_ptr<ZDDNode>> Z_a; // ZDD缓存（线程安全）

        std::unordered_map<std::string, std::shared_ptr<ORNode>> Cache;
        // DNNF缓存
        unordered_map<size_t, shared_ptr<DNNFNode>> C;  // 单线程
        unordered_map<size_t, shared_ptr<DNNFNode>> C_a;  // 多线程
        unordered_map<int, shared_ptr<DNNFNode>> V_Table; // 变量节点缓存

        // Block缓存
        std::unordered_map<std::pair<std::unordered_set<int>, int>, Block, SetIntHash> block_cache;; // 用于存储Block的缓存，key为行集合和列数的组合，value为Block对象
        // 使用单例模式获取线程池
        static ThreadPool& getThreadPool() {
            static ThreadPool instance; // 局部静态变量，线程安全
            return instance;
        }
        std::mutex cacheMutex; // 缓存访问的互斥锁
        // 操作栈用于批量回溯
        std::stack<CoverOperation> operationStack;
        
};


namespace fs = std::filesystem;

class PreProccess
{
    public:
        //构造函数
        PreProccess();
        //处理exact_cover_benchmark文件
        static int** processFileToMatrix1(const std::string& filename, int& r, int& c);
        //释放内存
        static void freeMatrix(int** matrix, int rows);
        //从字符串中提取n和m的值
        static void extractNM(const std::string& line, int& n, int& m);
        //处理set_partitioning_benchmarks文件
        static int** processFileToMatrix2(const fs::path& filename, int& r, int& c);
        // 处理d3x数据集
        static int** processFileToMatrix3(const std::string& filename, int& r, int& c);
};
#endif