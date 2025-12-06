/**
 * @file DynamicHypergraphCC.h
 * @brief 动态超图连通性算法 - 完整版
 * 
 * 功能特性：
 * 1. ETT (Euler Tour Tree) - O(log n)动态连通性
 * 2. 持久化数据结构 - 快速回滚到任意版本
 * 3. 并行分量求解 - 独立子问题并发处理
 * 4. DLX集成 - 无缝对接舞蹈链算法
 */

#ifndef DYNAMIC_HYPERGRAPH_CC_H
#define DYNAMIC_HYPERGRAPH_CC_H

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <functional>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <future>
#include <queue>
#include <stack>

namespace hypergraph {

// ==================== 前向声明 ====================
class ETTNode;
class EulerTourTree;
class PersistentState;
class ThreadPool;
struct ConnectedComponent;

// ==================== 类型定义 ====================
using RowID = int;
using ColID = int;
using CompID = int;
using VersionID = int;

enum class EdgeType {
    TREE,       // 生成树边
    NON_TREE,   // 非树边（回边）
    UNKNOWN     // 未分类
};

// ==================== ETT节点 ====================
/**
 * @brief Euler Tour Tree的Splay树节点
 * 用于O(log n)时间维护动态连通性
 */
class ETTNode {
public:
    enum class NodeType {
        VERTEX,     // 顶点节点
        EDGE_IN,    // 边进入节点
        EDGE_OUT    // 边离开节点
    };

    ETTNode(NodeType type, int id);
    ~ETTNode();

    // Splay树操作
    void splay();
    void rotate();
    
    // 查询操作
    ETTNode* getRoot();
    bool isConnected(ETTNode* other);
    
    // 树形维护
    void updateSize();
    int getSubtreeSize() const { return subtree_size; }

    friend class EulerTourTree;

public:
    NodeType type;
    int id;  // 顶点ID或边ID
    
    // Splay树结构
    ETTNode* parent;
    ETTNode* left;
    ETTNode* right;
    
    // 辅助信息
    int subtree_size;
    bool reversed;  // 懒惰标记
    
private:
    void pushDown();
    void pushUp();
};

// ==================== 欧拉游览树 ====================
/**
 * @brief Euler Tour Tree - 动态树连通性维护
 * 
 * 核心功能：
 * - link(u, v): 连接两个顶点 O(log n)
 * - cut(u, v): 断开两个顶点 O(log n)  
 * - isConnected(u, v): 判断连通性 O(log n)
 * - findReplacement(): 查找替代边 O(log n)
 */
class EulerTourTree {
public:
    EulerTourTree();
    ~EulerTourTree();

    // 顶点操作
    void addVertex(RowID row_id);
    void removeVertex(RowID row_id);
    
    // 边操作
    void link(RowID u, RowID v, ColID edge_id);
    void cut(RowID u, RowID v, ColID edge_id);
    
    // 查询操作
    bool isConnected(RowID u, RowID v);
    CompID getComponentID(RowID row_id);
    std::vector<RowID> getComponentVertices(RowID row_id);
    
    // 替代边查找
    ColID findReplacementEdge(RowID u, RowID v, 
                             const std::unordered_map<ColID, std::unordered_set<RowID>>& non_tree_edges);
    
    // 辅助功能
    void clear();
    size_t getComponentCount() const { return component_count; }

private:
    // ETT节点管理
    std::unordered_map<RowID, ETTNode*> vertex_nodes;
    std::unordered_map<ColID, std::pair<ETTNode*, ETTNode*>> edge_nodes;  // (in, out)
    
    // 连通分量管理
    std::unordered_map<RowID, CompID> vertex_to_comp;
    size_t component_count;
    int next_comp_id;
    
    // 辅助方法
    void insertEdgeInTour(RowID u, RowID v, ColID edge_id);
    void removeEdgeFromTour(ColID edge_id);
    void recomputeComponent(ETTNode* root);
    
    // Splay操作辅助
    void makeRoot(ETTNode* node);
    void expose(ETTNode* node);
    ETTNode* findFirst(ETTNode* node);
};

// ==================== 超边 ====================
/**
 * @brief 超边（矩阵的列）
 */
class HyperEdge {
public:
    explicit HyperEdge(ColID id);
    
    void addVertex(RowID row_id);
    void removeVertex(RowID row_id);
    
    bool isActive() const { return active; }
    void setActive(bool val) { active = val; }
    
    EdgeType getEdgeType() const { return edge_type; }
    void setEdgeType(EdgeType type) { edge_type = type; }
    
    const std::unordered_set<RowID>& getVertices() const { return vertices; }
    size_t getVertexCount() const { return vertices.size(); }
    
    std::vector<std::pair<RowID, RowID>> getVertexPairs() const;

public:
    ColID col_id;
    
private:
    std::unordered_set<RowID> vertices;
    bool active;
    EdgeType edge_type;
    double weight;  // 用于替代边选择
};

// ==================== 顶点 ====================
/**
 * @brief 顶点（矩阵的行）
 */
class Vertex {
public:
    explicit Vertex(RowID id);
    
    void addEdge(ColID col_id, EdgeType type = EdgeType::UNKNOWN);
    void removeEdge(ColID col_id);
    void reclassifyEdge(ColID col_id, EdgeType new_type);
    
    bool isActive() const { return active; }
    void setActive(bool val) { active = val; }
    
    CompID getComponentID() const { return component_id; }
    void setComponentID(CompID id) { component_id = id; }
    
    const std::unordered_set<ColID>& getTreeEdges() const { return tree_edges; }
    const std::unordered_set<ColID>& getNonTreeEdges() const { return non_tree_edges; }
    const std::unordered_set<ColID>& getAllEdges() const { return all_edges; }
    
    bool hasNonTreeEdges() const { return !non_tree_edges.empty(); }

public:
    RowID row_id;
    
private:
    std::unordered_set<ColID> tree_edges;
    std::unordered_set<ColID> non_tree_edges;
    std::unordered_set<ColID> all_edges;  // 所有边的并集
    bool active;
    CompID component_id;
};

// ==================== 连通分量 ====================
/**
 * @brief 连通分量信息
 */
struct ConnectedComponent {
    CompID id;
    std::unordered_set<RowID> rows;
    std::unordered_set<ColID> cols;
    bool active;
    
    // 统计信息
    size_t size() const { return rows.size(); }
    double density;
    
    ConnectedComponent(CompID comp_id) 
        : id(comp_id), active(true), density(0.0) {}
};

// ==================== 持久化状态 ====================
/**
 * @brief 持久化数据结构 - 支持快速回滚
 * 
 * 采用写时复制(Copy-on-Write)策略
 * 支持回滚到任意历史版本
 */
class PersistentState {
public:
    struct Operation {
        enum class Type {
            COVER_COLUMN,
            REMOVE_ROW,
            MARK_EDGE
        };
        
        Type type;
        ColID col_id;
        std::vector<RowID> affected_rows;
        std::unordered_map<ColID, EdgeType> edge_types;  // 边类型变化
        
        Operation(Type t, ColID c) : type(t), col_id(c) {}
    };
    
    struct Version {
        VersionID id;
        std::vector<Operation> operations;
        VersionID parent_version;
        std::chrono::system_clock::time_point timestamp;
        
        explicit Version(VersionID vid, VersionID parent = -1) 
            : id(vid), parent_version(parent), 
              timestamp(std::chrono::system_clock::now()) {}
    };

public:
    PersistentState();
    
    // 版本管理
    VersionID createVersion(VersionID parent_id = -1);
    void addOperation(VersionID ver_id, const Operation& op);
    bool rollbackToVersion(VersionID target_version);
    
    // 查询
    VersionID getCurrentVersion() const { return current_version; }
    const std::vector<Operation>& getOperations(VersionID ver_id) const;
    std::vector<VersionID> getVersionHistory() const;
    
    // 清理
    void pruneOldVersions(size_t keep_count = 100);

private:
    std::unordered_map<VersionID, std::shared_ptr<Version>> versions;
    VersionID current_version;
    VersionID next_version_id;
    std::mutex version_mutex;
};

// ==================== 线程池 ====================
/**
 * @brief 线程池 - 用于并行处理独立分量
 */
class ThreadPool_ {
public:
    explicit ThreadPool_(size_t num_threads = std::thread::hardware_concurrency());
    ~ThreadPool_();
    
    // 提交任务
    template<typename F, typename... Args>
    auto enqueue(F&& f, Args&&... args) 
        -> std::future<typename std::result_of<F(Args...)>::type>;
    
    // 等待所有任务完成
    void waitAll();
    
    size_t getThreadCount() const { return workers.size(); }

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    
    std::mutex queue_mutex;
    std::condition_variable condition;
    std::atomic<bool> stop;
    std::atomic<size_t> active_tasks;
};

// ==================== 主类 ====================
/**
 * @brief 动态超图连通性维护器 - 完整版
 * 
 * 集成功能：
 * 1. ETT动态树 - O(log n)连通性查询
 * 2. 持久化 - 版本控制和回滚
 * 3. 并行求解 - 独立分量并发处理
 */
class DynamicHypergraphCC {
public:
    struct Statistics {
        size_t quick_path_hits = 0;
        size_t full_recompute = 0;
        size_t replacement_found = 0;
        size_t splits_detected = 0;
        size_t parallel_solves = 0;
        
        double avg_component_size = 0.0;
        double avg_query_time_ms = 0.0;
    };
    
    struct Config {
        size_t num_threads = std::thread::hardware_concurrency();
        bool enable_parallel = true;
        bool enable_persistence = true;
        size_t small_component_threshold = 10;
        size_t large_component_threshold = 100;
        size_t max_versions = 100;
        
        Config() = default;
    };

public:
    explicit DynamicHypergraphCC(const Config& config = Config());
    ~DynamicHypergraphCC();
    
    // 初始化
    void addRow(RowID row_id, const std::vector<ColID>& col_ids);
    void buildInitialComponents();
    
    // DLX操作接口
    std::vector<CompID> coverColumn(ColID col_id);
    void uncoverColumn(ColID col_id);
    std::vector<CompID> batchCover(const std::vector<ColID>& col_ids);
    
    // 查询接口
    bool isConnected(RowID u, RowID v);
    CompID getComponentID(RowID row_id);
    std::shared_ptr<ConnectedComponent> getComponent(CompID comp_id);
    std::vector<std::shared_ptr<ConnectedComponent>> getAllComponents();
    std::vector<RowID> getComponentRows(CompID comp_id);
    
    // 版本管理
    VersionID saveVersion();
    bool restoreVersion(VersionID version_id);
    std::vector<VersionID> getVersionHistory();
    
    // 并行求解
    using ComponentSolver = std::function<void(const ConnectedComponent&)>;
    void solveComponentsParallel(ComponentSolver solver);
    
    // 统计信息
    const Statistics& getStatistics() const { return stats; }
    void printStatistics() const;
    void resetStatistics();
    
    // 配置
    const Config& getConfig() const { return config; }
    void setConfig(const Config& new_config);

private:
    // 核心数据
    std::unordered_map<RowID, std::unique_ptr<Vertex>> vertices;
    std::unordered_map<ColID, std::unique_ptr<HyperEdge>> hyperedges;
    std::unordered_map<CompID, std::shared_ptr<ConnectedComponent>> components;
    
    // ETT动态树
    std::unique_ptr<EulerTourTree> ett;
    
    // 持久化
    std::unique_ptr<PersistentState> persistent_state;
    
    // 线程池
    std::unique_ptr<ThreadPool_> thread_pool;
    
    // 配置和统计
    Config config;
    Statistics stats;
    
    // ID生成器
    std::atomic<CompID> next_comp_id;
    
    // 锁（用于线程安全）
    mutable std::shared_mutex global_mutex;
    
    // 核心算法实现
    std::shared_ptr<ConnectedComponent> buildComponentBFS(RowID start_row);
    std::vector<CompID> incrementalUpdate(CompID comp_id, ColID removed_col, 
                                         const std::vector<RowID>& removed_rows);
    
    bool quickConnectivityCheck(const std::unordered_set<RowID>& vertices);
    ColID findReplacementEdge(const std::unordered_set<RowID>& subtree1,
                             const std::unordered_set<RowID>& subtree2);
    
    std::vector<CompID> recomputeComponents(const std::unordered_set<RowID>& scope_rows);
    
    void classifyEdges(std::shared_ptr<ConnectedComponent> component);
    void updateComponentMetadata(std::shared_ptr<ConnectedComponent> component);
    
    std::unordered_set<RowID> getNeighbors(RowID row_id);
    std::pair<std::unordered_set<RowID>, std::unordered_set<RowID>> 
        splitIntoSubtrees(const std::unordered_set<RowID>& vertices, ColID removed_edge);
    
    // 持久化辅助
    void recordOperation(PersistentState::Operation::Type type, ColID col_id,
                        const std::vector<RowID>& affected_rows);
};

} // namespace hypergraph

// ==================== 模板实现 ====================
namespace hypergraph {

template<typename F, typename... Args>
auto ThreadPool_::enqueue(F&& f, Args&&... args) 
    -> std::future<typename std::result_of<F(Args...)>::type> {
    
    using return_type = typename std::result_of<F(Args...)>::type;
    
    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );
    
    std::future<return_type> res = task->get_future();
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        if (stop) {
            throw std::runtime_error("ThreadPool已停止");
        }
        tasks.emplace([task]() { (*task)(); });
    }
    condition.notify_one();
    return res;
}

} // namespace hypergraph

#endif // DYNAMIC_HYPERGRAPH_CC_H