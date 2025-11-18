#ifndef DANCINGMATRIX_H
#define DANCINGMATRIX_H

#include "ThreadPool.h"
#include "ComponentDetector.h"
#include "common.h"

using col_id = int;
using row_id = int;

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

class IncrementalConnectedGraph;

class DancingMatrix 
{  
    public:  
        int ROWS, COLS; 
        int EXIST_ROWS;
        
        std::uint64_t count = 0;  // 统计精确覆盖解
        std::uint64_t ONE_COUNT = 0; // 统计矩阵中1的个数
        uint64_t main_thread_id;  // 主线程ID
        
        double searchTimeSeconds = 0.0;
        double countTimeSeconds = 0.0;
        bool useETT;
        bool useIG; // 使用增量图
        std::vector<std::vector<int>> solutions; 
        set<int> rowsSet;  // 舞蹈链行id
        set<int> colsSet;  // 原始矩阵列
        unordered_set<int> active_rows;
        Block InitBlock;

        // 列状态
        size_t getColumnState() const;
        Signature getColumnSignature() const;
        
        
        //接收矩阵其及维度  
        DancingMatrix( int rows, int cols, int** matrix, bool verbose = false);  
        DancingMatrix( const string& file_path, int from, bool use_ig = false, bool use_ett = false);

        // 检测器，用于检测矩阵中的连通性
        std::unique_ptr<ComponentDetector> detector;

        // 禁用拷贝和赋值
        DancingMatrix(const DancingMatrix&) = delete;
        DancingMatrix& operator=(const DancingMatrix&) = delete;

        DancingMatrix(DancingMatrix&&) = delete;
        DancingMatrix& operator=(DancingMatrix&&) = delete;
        //释放内存  
        ~DancingMatrix();  
        
        void build_mapping_from_cols(const unordered_set<int>& blockCols, unordered_map<int, set<int>>& rowToCols, unordered_map<int, set<int>>& colToRows);
        void insert( int r, int c );  

        void printMatrix() const; 
        void printBlocks( vector<Block>& blocks) const;
        void cover( int c );  
        void uncover( int c ); 
        void coverInBlock(int c, Block& block);
        void uncoverInBlock(int c, Block& block);

        string encodeBlockState(const unordered_set<int>& cols);
        size_t hashBlockState(const set<int>& cols);
     
        ColumnHeader* selectCol();
        ColumnHeader* selectColumnHeuristic(const set<int>& cols);
        col_id getClosedSizeCol(const int expected_size);
        col_id getSmallestSizeCol();
        // ColumnHeader* fastSelect();

        inline ColumnHeader* getColumnHeader(int c) const {
            // std::shared_lock lock(mutex_);
            ColumnHeader* col = &ColIndex[c];
            return col;
        }

        inline int getColSize(int c) const {
            // std::shared_lock lock(mutex_);
            return ColIndex[c].size;
        }

        inline void decColSize(int c) {
            // std::unique_lock lock(mutex_);
            ColIndex[c].size--;
        }

        inline void incColSize(int c) {
            // std::unique_lock lock(mutex_);
            ColIndex[c].size++;
        }

        inline RowNode* getRowHeader(int r) const {
            std::shared_lock lock(mutex_);
            RowNode* row = &RowIndex[r];
            return row;
        }

        inline bool isSolved() const {
            return root->right == root;
        }

        // IBD: Independent Block Detection
        vector<Block> getComponentsByIG(const set<int> rows);

        vector<Block> findComponents(const set<int>& block_rows);

        void dumpETTState() const;

        void turnOnGraphSync() {
            enableGraphSync = true;
        }

        void turnOffGraphSync() {
            enableGraphSync = false;
        }

    private:  
        ColumnHeader* root;  
        std::unique_ptr<ColumnHeader[]> ColIndex;
        std::unique_ptr<RowNode[]> RowIndex;
        std::vector<std::unique_ptr<Node>> dataNodes;

        std::unique_ptr<IncrementalConnectedGraph> incrementalGraph;

        std::shared_ptr<ETTree> etTree;  // 欧拉回路树
        mutable std::shared_mutex mutex_;  // 读写锁

        // 行 -> 包含该行的列集合
        unordered_map<int, set<int>> row_to_cols;

        // 追踪每行当前被cover的列（用于判断是否完全被删除）
        unordered_map<int, set<int>> row_covered_cols;

        // 关键数据结构：列 -> 激活行集合的反向索引
        unordered_map<int, set<int>> col_to_rows;

        // 生成行对的规范化键
        std::pair<int, int> make_row_pair(int r1, int r2) {
            return r1 < r2 ? std::make_pair(r1, r2) : std::make_pair(r2, r1);
        }

        bool enableGraphSync = true; // 是否启用图同步
        
};

#endif