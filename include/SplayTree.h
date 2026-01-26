#ifndef SPLAYTREE_H
#define SPLAYTREE_H

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <algorithm>

namespace splaytree {

// Splay节点：表示顶点在欧拉回路中的出现
struct Node {
    int u;                         // 起始顶点
    int v;                         // 结束顶点（-1表示单个顶点节点）
    Node *left, *right, *parent;
    int size;
    
    // 构造单个顶点节点
    Node(int vertex) : u(vertex), v(-1), left(nullptr), 
          right(nullptr), parent(nullptr), size(1) {}
    
    // 构造边节点
    Node(int _u, int _v) : u(_u), v(_v), left(nullptr), 
          right(nullptr), parent(nullptr), size(1) {}
    
    bool isEdge() const { return v != -1; }
};

// 边的表示（标准化：u < v）
struct Edge {
    int u, v;
    Edge(int _u, int _v) : u(std::min(_u, _v)), v(std::max(_u, _v)) {}
    
    bool operator==(const Edge& other) const {
        return u == other.u && v == other.v;
    }
    
    bool operator<(const Edge& other) const {
        if (u != other.u) return u < other.u;
        return v < other.v;
    }
};

// 边的哈希函数
struct EdgeHash {
    size_t operator()(const Edge& e) const {
        return std::hash<long long>()((long long)e.u << 32 | e.v);
    }
};

// 欧拉回路树类（无锁版本，每个线程独立处理）
class EulerTourTree {
private:
    Node* root = nullptr;
    int treeId;
    
    // 连通分量信息
    std::unordered_set<int> vertices;                          // 顶点集合
    std::unordered_set<Edge, EdgeHash> nonTreeEdges;           // 非树边集合
    
    // 每个顶点的occurrence节点列表（用于O(1)查找）
    std::unordered_map<int, std::vector<Node*>> vertexOccurrences;
    std::unordered_map<int, std::unordered_map<int, Node*>> edgeNodes;
    
    // Splay树基本操作
    void updateSize(Node* x);
    void rotate(Node* n);
    void splay(Node* n);

    Node* findMax(Node* x);
    Node* findMin(Node* x);
    Node* findRoot(Node* x);

    Node* split(Node* x);
    Node* join(Node* leftTree, Node* rightTree);
    
    // 辅助函数
    Node* getRepresentative(int u);
    void collectNodes(Node* x, std::vector<Node*>& nodes) const;
    Node* findEdgeNode(Node* subtree, int u, int v);
    Node* findEdgeNodeDFS(Node* subtree, int u, int v);
    Node* buildFromNodes(std::vector<Node*>& nodes, int start, int end);
    void deleteTree(Node* x);
    
    // 寻找替代边
    Edge findReplacementEdge(const std::unordered_set<int>& component1,
                            const std::unordered_set<int>& component2);
    
public:
    explicit EulerTourTree(int id);
    ~EulerTourTree();

    // 禁止拷贝，只能移动
    EulerTourTree(const EulerTourTree&) = delete;
    EulerTourTree& operator=(const EulerTourTree&) = delete;
    
    // 基本操作
    int getTreeId() const { return treeId; }
    Node* getRoot() const { return root; }
    void addVertex(int v);
    void reroot(int u);
    void link(int u, int v, EulerTourTree* otherTree);
    std::pair<std::unique_ptr<EulerTourTree>, std::unique_ptr<EulerTourTree>> cut(int u, int v, int newTreeId1, int newTreeId2);
    
    // 查询操作
    bool isConnected(int u, int v) const;
    void setVertices(const std::unordered_set<int>& verts) { vertices = verts; }
    std::unordered_set<int> getVertices() const { return vertices; }
    void setNonTreeEdges(const std::unordered_set<Edge, EdgeHash>& edges) { nonTreeEdges = edges; }
    std::unordered_set<Edge, EdgeHash> getNonTreeEdges() const { return nonTreeEdges; }
    bool isEmpty() const { return vertices.empty(); }
    bool hasVertex(int v) const { return vertices.count(v) > 0; }
    
    // 非树边操作
    void addNonTreeEdge(const Edge& e) { nonTreeEdges.insert(e); }
    void removeNonTreeEdge(const Edge& e) { nonTreeEdges.erase(e); }
    bool hasNonTreeEdge(const Edge& e) const { return nonTreeEdges.count(e) > 0; }
    
    // 动态更新相关
    void removeVertex(int v);
    
    // 调试
    void printEulerTour() const;
    int getVertexDegree(int v) const;
};

}
#endif // SPLAYTREE_H