#ifndef DANCINGMATRIX_H
#define DANCINGMATRIX_H

#include "ThreadPool.h"
#include "DynamicConnectivity.h"
#include "common.h"

using col_id = int;
using row_id = int;

const unsigned int MAX_ROW = 300000;
const int TARGET_THRESHOLD = 5;
const int HEAP_THRESHOLD = 50;
const int MAX_TRIES = 200;
const int MAX_CONCURRENT_THREADS = 16;

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

struct ColumnInfo {
    int colId;
    int size;
    ColumnHeader* header;
    
    bool operator>(const ColumnInfo& other) const {
        return size > other.size;
    }
};

struct BlockMutationMetrics {
    bool enabled = false;
    double coverSeconds = 0.0;
    double uncoverSeconds = 0.0;
    double coverSetSeconds = 0.0;
    double uncoverSetSeconds = 0.0;
    double removedRowsClearSeconds = 0.0;
    uint64_t coverCalls = 0;
    uint64_t uncoverCalls = 0;
    uint64_t coverSetOps = 0;
    uint64_t uncoverSetOps = 0;
    uint64_t removedRowsClearCalls = 0;
};

class DancingMatrix 
{  
    public:
        int ROWS, COLS; 
        int EXIST_ROWS;
        std::uint64_t ONE_COUNT = 0;
        
        std::uint64_t count = 0;
        double searchTimeSeconds = 0.0;
        double countTimeSeconds = 0.0;
        AlgorithmMode algorithmMode;
        std::vector<std::vector<int>> solutions; 
        set<int> rowsSet;
        set<int> colsSet;
        unordered_set<int> active_rows;
        Block InitBlock;

        bool isDXZMode() const {
            return algorithmMode == AlgorithmMode::DXZ;
        }
        bool isDXDMode() const {
            return algorithmMode == AlgorithmMode::DXD;
        }
        bool isDynDXD() const {
            return algorithmMode == AlgorithmMode::DYNDXD;
        }
        bool isHybridDXD() const {
            return algorithmMode == AlgorithmMode::HYBRID_DXD;
        }
        bool usesDynamicConnectivity() const {
            return algorithmMode == AlgorithmMode::DYNDXD ||
                   algorithmMode == AlgorithmMode::HYBRID_DXD ||
                   algorithmMode == AlgorithmMode::MDLX_DYNAMIC;
        }

        struct CCExperimentStats {
            uint64_t bfsVertexSum = 0;
            uint64_t bfsEdgeSum = 0;
            uint64_t decCalls = 0;
            uint64_t activeVertexSum = 0;
            uint64_t deletedTreeEdgeSum = 0;
            uint64_t deletedVertexSum = 0;
            uint64_t replacementCheckSum = 0;

            void reset() { *this = {}; }
        };

        bool collectCCExperimentStats = false;
        CCExperimentStats ccExperimentStats;
        std::mutex ccExperimentStatsMutex;

        size_t getColumnState() const;
        Signature getColumnSignature() const;
        
        
        DancingMatrix(int rows, int cols, int** matrix, bool verbose = false,
                      AlgorithmMode mode = AlgorithmMode::DXD);
        DancingMatrix(const string& file_path,
                      AlgorithmMode mode = AlgorithmMode::DXZ);

        DancingMatrix(const DancingMatrix&) = delete;
        DancingMatrix& operator=(const DancingMatrix&) = delete;

        DancingMatrix(DancingMatrix&&) = delete;
        DancingMatrix& operator=(DancingMatrix&&) = delete;
        ~DancingMatrix();  

        unordered_map<int, set<int>> row_to_cols;
        unordered_map<int, vector<int>> col_to_rows;
        
        void build_mapping_from_cols(const unordered_set<int>& blockCols, unordered_map<int, set<int>>& rowToCols, unordered_map<int, set<int>>& colToRows);
        void insert( int r, int c );  

        void printMatrix() const; 
        void printBlocks( vector<Block>& blocks) const;
        void cover( int c );
        void uncover( int c );

        void coverInBlock(int c, Block& block, set<int>& covered_rows,
                          bool maintainRows);
        void uncoverInBlock(int c, Block& block, bool maintainRows);

        size_t encodeColState(vector<int>* exactCols = nullptr);
        size_t hashBlockState(const set<int>& cols,
                              vector<int>* exactCols = nullptr);
     
        ColumnHeader* selectCol();
        ColumnHeader* selectColumnHeuristic(const set<int>& cols);
        ColumnHeader* selectColumnByLinear(const set<int>& cols, int threshold);
        ColumnHeader* selectColumnByMinHeap(const set<int>& cols, int threshold);
        ColumnHeader* selectOptimalColumn(const set<int>& cols);
        col_id getClosedSizeCol(const int expected_size);
        col_id getSmallestSizeCol();
        // ColumnHeader* fastSelect();

        inline ColumnHeader* getColumnHeader(int c) const {
            ColumnHeader* col = &ColIndex[c];
            return col;
        }

        inline int getColSize(int c) const {
            return ColIndex[c].size;
        }

        inline void decColSize(int c) {
            ColIndex[c].size--;
        }

        inline void incColSize(int c) {
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

        struct BFSScanMetrics {
            uint64_t vertices = 0;
            uint64_t graphEdges = 0;
        };
        vector<Block> getComponentsByBFS(
            const set<int>& target_cols, BFSScanMetrics* metrics = nullptr);
        vector<Block> getComponentsByETT(const set<int>& target_cols);

        void turnOnGraphSync() {
            std::unique_lock lock(graph_sync_mutex);
            enableGraphSync = true;
        }

        void turnOffGraphSync() {
            std::unique_lock lock(graph_sync_mutex);
            enableGraphSync = false;
        }

        bool isGraphSyncEnabled() const {
            std::shared_lock lock(graph_sync_mutex);
            return enableGraphSync;
        }

        void initialize();
        void buildGraphFromMatrix();
        void prepareDynamicCCForBlock(const Block& block);
        void releaseDynamicCCStorage();
        void prepareThreadLocalDynamicCCForBlock(
            const Block& block, bool explicitProjectedEdges = false);
        void releaseThreadLocalDynamicCCStorage();
        std::vector<std::unique_ptr<splaytree::EulerTourTree>>
        cloneThreadLocalDynamicCCForest() const;
        void restoreThreadLocalDynamicCCSnapshot(
            const Block& block,
            const std::vector<std::unique_ptr<
                splaytree::EulerTourTree>>& snapshot);
        bool usesImplicitProjectedGraph() const {
            return graph && graph->usesImplicitNeighbors();
        }
        
        // 动态更新接口
        void DecUpdateCC(const std::set<int>& deletedVertices);
        void IncUpdateCC(const std::set<int>& restoredVertices);
        
        // 获取连通分量
        std::vector<std::unordered_set<int>> getConnectedComponents() const;
        int getNumComponents() const { return components.size(); }
        
        void printComponents();

        void testCutEdge(int u, int v);
        void testReRoot(int v);
        void testSplay(int v);
        void testDynamicUpdateCC(const std::vector<int>& deletedVertices);

    private:  
        ColumnHeader* root;  
        std::unique_ptr<ColumnHeader[]> ColIndex;
        std::unique_ptr<RowNode[]> RowIndex;
        std::vector<std::unique_ptr<Node>> dataNodes;

        std::shared_ptr<ETTree> etTree;
        std::unordered_set<int> dynamicBlockRows_;
        mutable std::shared_mutex mutex_;
        mutable std::shared_mutex graph_sync_mutex;

        bool enableGraphSync = true;

        vector<int> implicitProjectedNeighborsInRows(
            int row, const unordered_set<int>& candidateRows,
            const Graph* graph) const;
        
        
    public:
        std::unique_ptr<Graph> graph;
        std::vector<std::unique_ptr<splaytree::EulerTourTree>> components;
        splaytree::EulerTourTree* findEulerTourTree(int v);
        int nextTreeId = 0;

        struct TreeGraphContext {
            std::unique_ptr<Graph> graph;
            unordered_set<int> blockRows;
        };

        // === 线程局部数据结构 ===
        struct ThreadLocalState {
            std::vector<std::unique_ptr<splaytree::EulerTourTree>> components;
            // The worker and its escaped ETT trees share ownership of this
            // context, so replacement-edge providers never point at a freed
            // worker graph. subgraph/activeSubgraph are routes into it.
            std::shared_ptr<TreeGraphContext> graphContext;

            SubGraph* subgraph  = nullptr;
            SubGraph* activeSubgraph = nullptr;
            int nextTreeId = 0;
            int decompose_depth = 0;
            DecompositionPolicy decompositionPolicy;
            std::set<int> ettEligibleRows;
            bool initialized = false;
            
            ThreadLocalState() = default;
            
            ~ThreadLocalState() {
                components.clear();
                graphContext.reset();
            }
        };
        
        // thread_local 存储
        static thread_local std::unique_ptr<ThreadLocalState> tlsState;

        // 初始化线程局部状态
        void initThreadLocalState(const Block& block, 
                                    std::unique_ptr<splaytree::EulerTourTree> tree);

        // 清理线程局部状态
        static void cleanupThreadLocalState();

        // 判断是否使用线程局部状态
        bool isThreadLocal() const {
            return tlsState && tlsState->initialized;
        }

        // 获取当前线程的 components
        std::vector<std::unique_ptr<splaytree::EulerTourTree>>& getComponents() {
            return isThreadLocal() ? tlsState->components : components;
        }

        SubGraph* activeSubgraph_ = nullptr;

        SubGraph*& currentSubgraphRoute() {
            return isThreadLocal() ? tlsState->activeSubgraph
                                   : activeSubgraph_;
        }

        Graph* currentGraph() {
            if (isThreadLocal()) {
                return tlsState->graphContext
                           ? tlsState->graphContext->graph.get()
                           : nullptr;
            }
            return graph.get();
        }

        SubGraph* getGraph() {
            if (isThreadLocal()) return tlsState->activeSubgraph;

            if (activeSubgraph_) return activeSubgraph_;
            
            // 从第一个非空分量取任意顶点做路由回查
            for (auto& comp : components) {
                if (!comp || comp->isEmpty()) continue;
                int v = comp->getAnyVertex();
                Graph* current = currentGraph();
                if (v >= 0 && current) {
                    SubGraph* sg = current->subgraphOf(v);
                    if (sg) return sg;
                }
            }
            return nullptr;
        }

        // 获取当前线程的 nextTreeId
        int& getNextTreeId() {
            return isThreadLocal() ? tlsState->nextTreeId : nextTreeId;
        }

        void bindTreeReplacementProvider(
            splaytree::EulerTourTree* tree,
            const std::shared_ptr<TreeGraphContext>& context);

        void bindGlobalTreeReplacementProvider(
            splaytree::EulerTourTree* tree);

        void rebindTreeReplacementProvider(
            splaytree::EulerTourTree* tree, ThreadLocalState* targetState);

        // 从图构建生成森林
        void buildSpanningForest();
        void buildSpanningForestForRows(const std::set<int>& rows);
        std::vector<int> implicitProjectedNeighbors(int row) const;
        void buildStatsSpanningForest();

        std::vector<std::unordered_set<int>> statsForest;
        std::unordered_set<splaytree::Edge, splaytree::EdgeHash> statsNonTreeEdges;
        uint64_t statsActiveEdgeCount = 0;
        bool statsForestReady = false;
        std::vector<splaytree::Edge> bfsSpanningTree(int start, std::unordered_set<int>& visited, std::unordered_set<int>& componentVertices);
        bool processBoundaryVertex(int v, splaytree::EulerTourTree* tree, SubGraph* g);
};

#endif
