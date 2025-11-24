#ifndef SPLAY_ETT_H
#define SPLAY_ETT_H

#include <vector>
#include <unordered_map>
#include <functional>
#include <iostream>
#include <climits>

/**
 * Splay-based Euler Tour Tree (ETT) 实现
 * 
 * 时间复杂度：所有操作 O(log n) 摊还
 */
class SplayETT {
private:
    struct Node {
        Node *left, *right, *parent;
        int vertex;      // 对应的顶点编号（-1表示边节点）
        unsigned long long edgeKey;  // 边的唯一标识 (u,v) -> key
        bool reversed;
        
        Node(int v = -1, unsigned long long key = 0) 
            : left(nullptr), right(nullptr), parent(nullptr),
              vertex(v), edgeKey(key), reversed(false) {}
    };
    
    struct Edge {
        Node *first, *second;
        int u, v;
    };
    
    std::vector<Node*> vertexNodes;
    std::unordered_map<unsigned long long, Edge> edges;
    std::vector<Node*> nodePool;
    
    // 辅助函数：将边(u,v)编码为唯一键
    inline unsigned long long makeEdgeKey(int u, int v) const {
        if (u > v) std::swap(u, v);
        return (static_cast<unsigned long long>(u) << 32) | static_cast<unsigned long long>(v);
    }
    
    inline void pushDown(Node* x) {
        if (!x || !x->reversed) return;
        std::swap(x->left, x->right);
        if (x->left) x->left->reversed ^= true;
        if (x->right) x->right->reversed ^= true;
        x->reversed = false;
    }
    
    void rotate(Node* x) {
        Node* p = x->parent;
        Node* g = p->parent;
        
        pushDown(p);
        pushDown(x);
        
        if (p->left == x) {
            p->left = x->right;
            if (x->right) x->right->parent = p;
            x->right = p;
        } else {
            p->right = x->left;
            if (x->left) x->left->parent = p;
            x->left = p;
        }
        
        x->parent = g;
        p->parent = x;
        
        if (g) {
            if (g->left == p) g->left = x;
            else g->right = x;
        }
    }
    
    void splay(Node* x) {
        while (x->parent) {
            Node* p = x->parent;
            Node* g = p->parent;
            
            if (g) pushDown(g);
            pushDown(p);
            pushDown(x);
            
            if (!g) {
                rotate(x);
            } else if ((g->left == p) == (p->left == x)) {
                rotate(p);
                rotate(x);
            } else {
                rotate(x);
                rotate(x);
            }
        }
        pushDown(x);
    }
    
    inline Node* getRoot(Node* x) {
        while (x->parent) x = x->parent;
        return x;
    }
    
    Node* split(Node* x) {
        splay(x);
        pushDown(x);
        Node* right = x->right;
        if (right) {
            right->parent = nullptr;
            x->right = nullptr;
        }
        return right;
    }
    
    Node* merge(Node* left, Node* right) {
        if (!left) return right;
        if (!right) return left;
        
        Node* curr = left;
        while (curr->right) {
            pushDown(curr);
            curr = curr->right;
        }
        splay(curr);
        pushDown(curr);
        
        curr->right = right;
        right->parent = curr;
        return curr;
    }
    
    inline Node* allocNode(int v = -1, unsigned long long key = 0) {
        Node* node = new Node(v, key);
        nodePool.push_back(node);
        return node;
    }
    
public:
    explicit SplayETT(int n) {
        vertexNodes.resize(n);
        nodePool.reserve(n * 4);
        edges.reserve(n * 2);
        
        for (int i = 0; i < n; i++) {
            vertexNodes[i] = allocNode(i);
        }
    }
    
    ~SplayETT() {
        for (Node* node : nodePool) {
            delete node;
        }
    }
    
    /**
     * 连接两个顶点 - 返回void以提升性能
     * @param u 顶点u
     * @param v 顶点v
     * @return 是否成功连接（false表示已连通）
     */
    bool link(int u, int v) {
        if (u == v) return false;
        if (connected(u, v)) return false;
        
        unsigned long long edgeKey = makeEdgeKey(u, v);
        
        Node* e1 = allocNode(-1, edgeKey);
        Node* e2 = allocNode(-1, edgeKey);
        
        Node* rootU = getRoot(vertexNodes[u]);
        Node* rootV = getRoot(vertexNodes[v]);
        
        Node* rightU = split(vertexNodes[u]);
        
        Node* newRoot = merge(vertexNodes[u], e1);
        newRoot = merge(newRoot, rootV);
        newRoot = merge(newRoot, e2);
        newRoot = merge(newRoot, rightU);
        
        edges[edgeKey] = {e1, e2, u, v};
        return true;
    }
    
    /**
     * 删除一条边 - 直接通过顶点对删除
     * @param u 顶点u
     * @param v 顶点v
     * @return 是否成功删除
     */
    bool cut(int u, int v) {
        if (u == v) return false;
        
        unsigned long long edgeKey = makeEdgeKey(u, v);
        auto it = edges.find(edgeKey);
        if (it == edges.end()) return false;
        
        Edge& edge = it->second;
        Node* e1 = edge.first;
        Node* e2 = edge.second;
        
        splay(e1);
        splay(e2);
        if (e2->parent) {
            std::swap(e1, e2);
            splay(e1);
        }
        
        Node* right = split(e2);
        Node* middle = split(e1);
        
        Node* left = e1->left;
        if (left) left->parent = nullptr;
        
        merge(left, middle);
        
        edges.erase(it);
        return true;
    }
    
    /**
     * 快速连通性查询 - 内联优化
     */
    inline bool connected(int u, int v) {
        if (u == v) return true;
        return getRoot(vertexNodes[u]) == getRoot(vertexNodes[v]);
    }

    /**
     * 获取节点所在连通分量节点id
     */
    int getComponentId(int u) {
        if (u < 0 || u >= static_cast<int>(vertexNodes.size())) {
            return -1;
        }
        
        Node* root = getRoot(vertexNodes[u]);
        return getComponentIdFromRoot(root);
    }

    /**
     * 批量获取连通分量ID - 优化版本
     * 利用访问局部性，缓存已访问的根节点
     * 适合查询大量可能在同一连通分量的节点
     */
    std::vector<int> batchGetComponentId(const std::vector<int>& vertices) {
        std::vector<int> result;
        result.reserve(vertices.size());
        
        std::unordered_map<Node*, int> root_cache;
        
        for (int v : vertices) {
            Node* root = getRoot(vertexNodes[v]);
            
            auto it = root_cache.find(root);
            if (it != root_cache.end()) {
                result.push_back(it->second);
            } else {
                int comp_id = getComponentIdFromRoot(root);
                result.push_back(comp_id);
                root_cache[root] = comp_id;
            }
        }
        
        return result;
    }

    /**
     * 批量查询连通性 - 返回分组信息
     * 直接返回 {comp_id -> vertices} 的映射
     */
    std::unordered_map<int, std::vector<int>> batchGroupByComponent(
        const std::vector<int>& vertices) {
        std::unordered_map<int, std::vector<int>> groups;
        std::unordered_map<Node*, int> root_cache;
        
        for (int v : vertices) {
            Node* root = getRoot(vertexNodes[v]);
            
            int comp_id;
            auto it = root_cache.find(root);
            if (it != root_cache.end()) {
                comp_id = it->second;
            } else {
                comp_id = getComponentIdFromRoot(root);
                root_cache[root] = comp_id;
            }
            
            groups[comp_id].push_back(v);
        }
        
        return groups;
    }
    
    /**
     * 批量link操作 - 减少函数调用开销
     */
    void batchLink(const std::vector<std::pair<int, int>>& edges_to_add) {
        for (const auto& [u, v] : edges_to_add) {
            link(u, v);
        }
    }
    
    /**
     * 批量cut操作
     */
    void batchCut(const std::vector<std::pair<int, int>>& edges_to_remove) {
        for (const auto& [u, v] : edges_to_remove) {
            cut(u, v);
        }
    }

private:
    /**
     * 从根节点找到连通分量的代表元
     * 遍历整个欧拉回路，找到第一个（最小的）顶点节点
     */
    int getComponentIdFromRoot(Node* root) {
        int min_vertex = INT_MAX;
        
        std::function<void(Node*)> findMinVertex = [&](Node* node) {
            if (!node) return;
            pushDown(node);
            
            findMinVertex(node->left);
            
            if (node->vertex != -1 && node->vertex < min_vertex) {
                min_vertex = node->vertex;
            }
            
            findMinVertex(node->right);
        };
        
        splay(root);
        findMinVertex(root);
        
        return min_vertex;
    }

public:
    /**
     * 获取连通分量大小 - 可选功能
     */
    int componentSize(int u) {
        Node* root = getRoot(vertexNodes[u]);
        splay(root);
        
        int size = 0;
        std::function<void(Node*)> dfs = [&](Node* x) {
            if (!x) return;
            pushDown(x);
            dfs(x->left);
            if (x->vertex != -1) size++;
            dfs(x->right);
        };
        dfs(root);
        
        return size;
    }
    
    /**
     * 清空所有边但保留顶点结构
     */
    void reset() {
        // 释放所有边节点，保留顶点节点
        for (size_t i = vertexNodes.size(); i < nodePool.size(); i++) {
            delete nodePool[i];
        }
        nodePool.resize(vertexNodes.size());
        
        // 重置顶点节点
        for (auto* node : vertexNodes) {
            node->left = node->right = node->parent = nullptr;
            node->reversed = false;
        }
        
        edges.clear();
    }
};

#endif // SPLAY_ETT_H