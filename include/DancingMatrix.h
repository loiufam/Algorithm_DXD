#ifndef DANCINGMATRIX_H
#define DANCINGMATRIX_H

#include "ThreadPool.h"
#include "../utils/ResProcessor.h"
#include "TreapETT.h"
#include <string>
#include <set>
#include <map>
#include <bitset>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>
#include <deque>
#include <stack>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <shared_mutex>
#include <utility>

using namespace std;
using col_id = int;
using row_id = int;

namespace fs = std::filesystem;

const unsigned int MAX_ROW = 250000;

struct Node  
{  
    Node* left, *right, *up, *down;  
    int col, row;  
    bool row_first_node;
    Node(int r = -1, int c = -1) 
        : row(r), col(c), left(nullptr), right(nullptr), 
          up(nullptr), down(nullptr), row_first_node(false) {} 
    
    virtual ~Node() = default;
};  

struct ColumnHeader : public Node  
{  
    int size;
    set<int> rows;
    ColumnHeader() : size(0) {
    }  
}; 

struct RowNode : public Node
{
    int size;
    set<int> cols;
    RowNode() :  size(0) {}

};

// 签名用于Memo Cache
struct Signature {
    vector<bool> covered;  // 哪些列已被覆盖
    
    bool operator==(const Signature& other) const {
        return covered == other.covered;
    }
};

// 哈希函数
struct SignatureHash {
    size_t operator()(const Signature& sig) const {
        size_t h = 0;
        for (size_t i = 0; i < sig.covered.size(); i++) {
            if (sig.covered[i]) h ^= (i + 1) * 2654435761u;
        }
        return h;
    }
};

struct Component{ 
    set<int> rows; 
    unordered_set<int> cols; 

    Component() = default;

    void printComponent() {
        cout<< "row_id: ";
        // sort(rows.begin(), rows.end());
        for(auto& r : rows) {
            cout<< r << " ";
        }
        cout << endl;
        vector<int> sortedCols(cols.begin(), cols.end());
        sort(sortedCols.begin(), sortedCols.end());
        cout << "col_id: ";
        for(auto& c : sortedCols) {
            cout<< c << " ";
        }
        cout << endl; 
    }
};


struct ColumnComparator {
    bool operator()(const std::pair<int, ColumnHeader*>& a, const std::pair<int, ColumnHeader*>& b) const {
        if (a.first != b.first) {
            return a.first < b.first;  // 一级排序：size从小到大
        }
        return a.second->col < b.second->col;  // 二级排序：列id从小到大
    }
};

// 并查集结构
class UnionFind {
private:
    std::vector<int> parent;
    std::vector<int> rank;
    int numComponents;
    
public:
    UnionFind(int n) : parent(n), rank(n, 0), numComponents(n) {
        for (int i = 0; i < n; ++i) {
            parent[i] = i;
        }
    }
    
    // 路径压缩的查找
    int find(int x) {
        if (parent[x] != x) {
            parent[x] = find(parent[x]); // 路径压缩
        }
        return parent[x];
    }
    
    // 按秩合并
    bool unite(int x, int y) {
        int rootX = find(x);
        int rootY = find(y);
        
        if (rootX == rootY) return false;
        
        if (rank[rootX] < rank[rootY]) {
            parent[rootX] = rootY;
        } else if (rank[rootX] > rank[rootY]) {
            parent[rootY] = rootX;
        } else {
            parent[rootY] = rootX;
            rank[rootX]++;
        }
        numComponents--;
        return true;
    }
    
    bool connected(int x, int y) {
        return find(x) == find(y);
    }
    
    int getNumComponents() const {
        return numComponents;
    }
};

class ConnectedGraph;
class IncrementalConnectedGraph;

class DancingMatrix 
{  
    public:  
        int ROWS, COLS; 
        int EXIST_ROWS;
        
        std::uint64_t count = 0;  // 统计精确覆盖解
        std::uint64_t ONE_COUNT = 0; // 统计矩阵中1的个数
        
        double searchTimeSeconds = 0.0;
        double countTimeSeconds = 0.0;
        bool useETT;
        std::vector<std::vector<int>> solutions; 
        unordered_set<int> rowsSet;  // 舞蹈链行id
        unordered_set<int> colsSet;  // 原始矩阵列
        Block InitBlock;

        // 列状态
        size_t getColumnState() const;
        Signature getColumnSignature() const;
        
        
        //接收矩阵其及维度  
        DancingMatrix( int rows, int cols, int** matrix, bool verbose = false);  
        DancingMatrix( const string& file_path, int from, bool verbose = false, bool use_ett = false);

        // 禁用拷贝和赋值
        DancingMatrix(const DancingMatrix&) = delete;
        DancingMatrix& operator=(const DancingMatrix&) = delete;

        DancingMatrix(DancingMatrix&&) = delete;
        DancingMatrix& operator=(DancingMatrix&&) = delete;
        //释放内存  
        ~DancingMatrix();  
        
        void build_mapping_from_cols(const unordered_set<int>& blockCols, unordered_map<int, set<int>>& rowToCols, unordered_map<int, set<int>>& colToRows);
        void insert( int r, int c );  
        void add_connection(int r, int c);
        void remove_connection(int r, int c);
        void printMatrix() const; 
        void cover( int c );  
        void uncover( int c ); 
        void coverInBlock(int c, Block& block);
        void uncoverInBlock(int c, Block& block);

        string encodeBlockState(const unordered_set<int>& cols);
        size_t hashBlockState(const unordered_set<int>& cols);
        size_t hashColState(unordered_set<int>& cols);
     
        ColumnHeader* selectCol();
        ColumnHeader* selectColumnHeuristic(const unordered_set<int>& cols);
        col_id getClosedSizeCol(const int expected_size);
        col_id getSmallestSizeCol();
        // ColumnHeader* fastSelect();

        inline ColumnHeader* getColumnHeader(int c) const {
            ColumnHeader* col = &ColIndex[c];
            return col;
        }

        inline void decColSize(int c) {
            ColIndex[c].size--;
        }

        inline void incColSize(int c) {
            ColIndex[c].size++;
        }

        inline RowNode* getRowHeader(int r) const {
            RowNode* row = &RowIndex[r];
            return row;
        }

        inline bool isSolved() const {
            return root->right == root;
        }

        // IBD: Independent Block Detection
        vector<vector<int>> getComponents(set<int>& rows);

        vector<Block> getComponents(const unordered_set<int> rows);

        vector<Block> getComponentsByETT(const unordered_set<int> rows);

        void printGraph() const;
        void build_graph();

    private:  
        ColumnHeader* root;  
        std::unique_ptr<ColumnHeader[]> ColIndex;
        std::unique_ptr<RowNode[]> RowIndex;
        std::vector<std::unique_ptr<Node>> dataNodes;

        std::unique_ptr<ConnectedGraph> graph;
        std::unique_ptr<IncrementalConnectedGraph> incrementalGraph;
        std::unique_ptr<ETTree> etTree = make_unique<ETTree>();  // 欧拉回路树

        // 行 -> 包含该行的列集合
        unordered_map<int, set<int>> row_to_cols;

        // 列到行的反向索引：col_to_rows[col] = {row1, row2, ...}
        map<int, set<int>> col_to_rows;
        
        // 记录两行之间共享的列：shared_cols[{row1, row2}] = {col1, col2, ...}
        std::map<std::pair<int, int>, std::set<int>> shared_cols;
        
        // 生成行对的规范化键
        std::pair<int, int> make_row_pair(int r1, int r2) {
            return r1 < r2 ? std::make_pair(r1, r2) : std::make_pair(r2, r1);
        }
        bool enableGraphSync; // 是否启用图同步
        
};

class Logger 
{
    private:
        std::ofstream logFile;
        bool enableConsole;
    
    public:
        Logger(const std::string& filename, bool console = true) 
            : enableConsole(console) {
            logFile.open(filename, std::ios::out | std::ios::trunc);
        }
        
        ~Logger() {
            if (logFile.is_open()) {
                logFile.close();
            }
        }
        
        template<typename T>
        void log(const T& message) {
            if (logFile.is_open()) {
                logFile << message;
                logFile.flush();
            }
            if (enableConsole) {
                std::cout << message;
            }
        }
        
        template<typename T>
        void logLine(const T& message) {
            log(message);
            if (logFile.is_open()) {
                logFile << std::endl;
            }
            if (enableConsole) {
                std::cout << std::endl;
            }
        }
        
        void enableConsoleOutput(bool enable) {
            enableConsole = enable;
        }
};

class PreProccess
{
    public:
        //构造函数
        PreProccess() {};
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