#ifndef DANCINGMATRIX_H
#define DANCINGMATRIX_H

#include "ThreadPool.h"
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

using namespace std;
using col_id = int;
using row_id = int;

namespace fs = std::filesystem;


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

struct Component{ 
    vector<int> rows; 
    vector<int> cols; 

    Component() = default;

    void printComponent() {
        cout<< "row_id: ";
        for(auto& r : rows) {
            cout<< r << " ";
        }
        cout << endl;
        cout << "col_id: ";
        for(auto& c : cols) {
            cout<< c << " ";
        }
        cout << endl; 
    }
};

struct ColumnComparator {
    bool operator()(const std::pair<int, ColunmHeader*>& a, const std::pair<int, ColunmHeader*>& b) const {
        if (a.first != b.first) {
            return a.first < b.first;  // 一级排序：size从小到大
        }
        return a.second->col < b.second->col;  // 二级排序：列id从小到大
    }
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

class ConnectedGraph;

class DancingMatrix 
{  
    public:  
        int ROWS, COLS; 
        int EXIST_ROWS;
        
        std::uint64_t count = 0;  // 统计精确覆盖解
        std::uint64_t ONE_COUNT = 0; // 统计矩阵中1的个数
        
        double searchTimeSeconds = 0.0;
        double countTimeSeconds = 0.0;
        std::vector<std::vector<int>> solutions; 
        set<int> rowsSet;  // 舞蹈链行id
        set<int> colsSet;  // 原始矩阵列
        unordered_map<int, set<int>> rowToColsSet;
        unordered_map<int, set<int>> colToRowsSet;
        // 列状态
        size_t currentColState = 0;
        unordered_map<int, size_t> colHash; // 每个列的固定哈希值
        size_t getColumnState() const;
        
        // 获取当前哈希
        size_t getCurColState() const {
            return currentColState;
        }
        
        // 初始计算一次 hash
        void computeInitialHash() {

            ColunmHeader* cur = (ColunmHeader*)root->right;
            while (cur != root) {
                currentColState ^= colHash[cur->col];
                cur = (ColunmHeader*)cur->right;
            }
        }
        
        //接收矩阵其及维度  
        DancingMatrix( int rows, int cols, int** matrix);  
        DancingMatrix( const string& file_path, int from, bool verbose = false );

        // 复制函数
        DancingMatrix(const DancingMatrix& other) 
            : root(other.root), ColIndex(other.ColIndex), RowIndex(other.RowIndex) {};
        //释放内存  
        ~DancingMatrix();  
        
        void build_mapping_from_cols(const unordered_set<int>& blockCols, unordered_map<int, set<int>>& rowToCols, unordered_map<int, set<int>>& colToRows);
        void insert( int r, int c );  
        void printMatrix() const; 
        void cover( int c );  
        void uncover( int c ); 
        shared_ptr<ConnectedGraph> getConnectedGraph(){
            return graph;
        };
        string encodeBlockState(const unordered_set<int>& cols);
        size_t hashBlockState(const unordered_set<int>& cols);
        size_t hashColState(unordered_set<int>& cols);
     
        ColunmHeader* selectCol();
        ColunmHeader* selectColumnHeuristic(const unordered_set<int>& cols);
        col_id getClosedSizeCol(const int expected_size);
        // ColunmHeader* fastSelect();

        std::mutex rowIndexMutex; // 保护 RowIndex 的互斥锁
        void removeCol(int r, int c) {
            set<int> colSet = RowIndex[r].cols;
            if(colSet.find(c) == colSet.end()) {
                return;
            }
            lock_guard<mutex> lock(rowIndexMutex);
            colSet.erase(c);
        }

        void restoreCol(int r, int c) {
            set<int> colSet = RowIndex[r].cols;
            // if(colSet.find(c) != colSet.end()) {
            //     return;
            // }
            // lock_guard<mutex> lock(rowIndexMutex);
            colSet.insert(c);
        }

        ColunmHeader* getRoot() const {
            return root;
        }

        ColunmHeader* getColIndex() const {
            return ColIndex;
        }

        RowNode* getRowIndex() const {
            return RowIndex;
        }

    private:  
        ColunmHeader* root;  
        ColunmHeader* ColIndex;  
        RowNode* RowIndex; 
        shared_ptr<ConnectedGraph> graph;
        
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