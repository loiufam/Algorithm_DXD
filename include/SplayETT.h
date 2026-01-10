#ifndef SPLAY_ETT_H
#define SPLAY_ETT_H

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <functional>
#include <iostream>
#include <climits>


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

    // 添加一个映射:树根 -> 连通分量ID
    std::unordered_map<Node*, int> rootToComponentId;
    int nextComponentId = 0;

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

    // 获取或创建连通分量ID
    int getOrCreateComponentId(Node* root) {
        auto it = rootToComponentId.find(root);
        if (it != rootToComponentId.end()) {
            return it->second;
        }
        
        // 新连通分量,分配ID
        int compId = nextComponentId++;
        rootToComponentId[root] = compId;
        return compId;
    }
    
    // 更新rootToComponentId映射
    void updateComponentMapping(Node* oldRoot1, Node* oldRoot2, Node* newRoot) {
        int compId1 = -1, compId2 = -1;
        
        auto it1 = rootToComponentId.find(oldRoot1);
        if (it1 != rootToComponentId.end()) {
            compId1 = it1->second;
            rootToComponentId.erase(it1);
        }
        
        auto it2 = rootToComponentId.find(oldRoot2);
        if (it2 != rootToComponentId.end()) {
            compId2 = it2->second;
            rootToComponentId.erase(it2);
        }
        
        // 使用较小的ID
        int finalId = (compId1 != -1 && compId2 != -1) ? 
                      std::min(compId1, compId2) : 
                      (compId1 != -1 ? compId1 : compId2);
        
        if (finalId == -1) {
            finalId = nextComponentId++;
        }
        
        rootToComponentId[newRoot] = finalId;
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
        
        unsigned long long edgeKey = makeEdgeKey(u, v);
        // 只检查边是否已存在，不检查连通性
        if (edges.find(edgeKey) != edges.end()) {
            return false;  // 边已存在，不重复添加
        }
        
        Node* e1 = allocNode(-1, edgeKey);
        Node* e2 = allocNode(-1, edgeKey);
        
        Node* rootU = getRoot(vertexNodes[u]);
        Node* rootV = getRoot(vertexNodes[v]);
        
        Node* rightU = split(vertexNodes[u]);
        
        Node* newRoot = merge(vertexNodes[u], e1);
        newRoot = merge(newRoot, rootV);
        newRoot = merge(newRoot, e2);
        newRoot = merge(newRoot, rightU);

        // 更新连通分量映射
        updateComponentMapping(rootU, rootV, newRoot);
        
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

        // 记录旧根
        Node* oldRoot = getRoot(e1);    
        
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
        
        Node* newRoot1 = merge(left, middle);

        // 更新连通分量映射
        auto rootIt = rootToComponentId.find(oldRoot);
        if (rootIt != rootToComponentId.end()) {
            int oldCompId = rootIt->second;
            rootToComponentId.erase(rootIt);
            
            if (newRoot1) {
                rootToComponentId[newRoot1] = oldCompId;
            }
        }
        
        edges.erase(it);
        return true;
    }

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
        return getOrCreateComponentId(root);
    }

    void batchLink(const std::vector<std::pair<int, int>>& edges_to_add) {
        for (const auto& [u, v] : edges_to_add) {
            link(u, v);
        }
    }

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
    
};

#endif // SPLAY_ETT_H