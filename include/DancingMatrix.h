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
        
        std::uint64_t count;  // 统计精确覆盖解
        std::uint64_t ONE_COUNT = 0; // 统计矩阵中1的个数
        
        double searchTimeSeconds = 0.0;
        double countTimeSeconds = 0.0;
        std::vector<std::vector<int>> solutions; 
        unordered_set<int> rowsSet;  // 原始矩阵行
        unordered_set<int> colsSet;  // 原始矩阵列
        unordered_map<int, set<int>> rowToColsSet;
        unordered_map<int, set<int>> colToRowsSet;
        
        
        //接收矩阵其及维度  
        DancingMatrix( int rows, int cols, int** matrix);  
        //释放内存  
        ~DancingMatrix();  
        void initConnectedGraph();
        
        void build_mapping_from_cols(const unordered_set<int>& blockCols, unordered_map<int, set<int>>& rowToCols, unordered_map<int, set<int>>& colToRows);
        void insert( int r, int c );  
        void printMatrix() const; 
        void cover( int c );  
        void uncover( int c ); 
        string getColumnState() const;
        string encodeBlockState(const unordered_set<int>& cols);
        size_t hashBlockState(const unordered_set<int>& cols);
        size_t hashColState(unordered_set<int>& cols);
     
        ColunmHeader* selectCol();
        ColunmHeader* selectColumnHeuristic(const unordered_set<int>& cols);
        // ColunmHeader* fastSelect();
        void printSolution(); 

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
        unique_ptr<ConnectedGraph> connectedGraph;
        
};


namespace fs = std::filesystem;

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