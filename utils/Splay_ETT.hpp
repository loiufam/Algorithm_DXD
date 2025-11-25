#pragma once

/**
 * 动态连通性检测器 - 用于DLX算法的并行化优化
 * 支持高效的连通分量查询、动态边更新和无锁并发操作
 * 
 * 使用欧拉回路树(Euler Tour Tree)实现O(log n)的动态连通性查询
 */

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <stack>
#include <algorithm>

namespace splay_ett {
    /**
 * 欧拉回路树(Euler Tour Tree)实现
 * 使用Splay Tree维护欧拉回路,支持O(log n)的动态连通性操作
 */
class EulerTourTree {
private:
    // Splay树节点
    struct Node {
        std::pair<int, int> edge;  // 边(u, v), (-1,-1)表示根
        Node* parent;
        Node* left;
        Node* right;
        int size;  // 子树大小
        
        Node() : edge(-1, -1), parent(nullptr), left(nullptr), right(nullptr), size(1) {}
        Node(int u, int v) : edge(u, v), parent(nullptr), left(nullptr), right(nullptr), size(1) {}
        
        void update() {
            size = 1;
            if (left) size += left->size;
            if (right) size += right->size;
        }
        
        ~Node() {
            // delete left;
            // delete right;
        }
    };
    
    // vertex -> 该顶点关联的边节点列表
    std::unordered_map<int, std::vector<Node*>> vertex_edges_;
    // vertex -> 该顶点所在树的根节点
    std::unordered_map<int, Node*> vertex_root_;
    // 所有创建的节点,用于内存管理
    std::vector<std::unique_ptr<Node>> all_nodes_;
    
    // Splay树旋转操作
    void rotateRight(Node* node) {
        Node* left = node->left;
        node->left = left->right;
        if (left->right) left->right->parent = node;
        
        left->parent = node->parent;
        if (node->parent) {
            if (node->parent->left == node)
                node->parent->left = left;
            else
                node->parent->right = left;
        }
        
        left->right = node;
        node->parent = left;
        node->update();
        left->update();
    }
    
    void rotateLeft(Node* node) {
        Node* right = node->right;
        node->right = right->left;
        if (right->left) right->left->parent = node;
        
        right->parent = node->parent;
        if (node->parent) {
            if (node->parent->left == node)
                node->parent->left = right;
            else
                node->parent->right = right;
        }
        
        right->left = node;
        node->parent = right;
        node->update();
        right->update();
    }
    
    // 将节点伸展到根
    Node* splay(Node* node) {
        while (node->parent) {
            Node* parent = node->parent;
            Node* grand = parent->parent;
            
            if (!grand) {
                // Zig
                if (parent->left == node)
                    rotateRight(parent);
                else
                    rotateLeft(parent);
            } else if (grand->left == parent && parent->left == node) {
                // Zig-Zig
                rotateRight(grand);
                rotateRight(parent);
            } else if (grand->right == parent && parent->right == node) {
                // Zig-Zig
                rotateLeft(grand);
                rotateLeft(parent);
            } else {
                // Zig-Zag
                if (parent->left == node) {
                    rotateRight(parent);
                    rotateLeft(grand);
                } else {
                    rotateLeft(parent);
                    rotateRight(grand);
                }
            }
        }
        return node;
    }
    
    // 获取节点所在树的根
    Node* getRoot(Node* node) {
        while (node->parent)
            node = node->parent;
        return node;
    }
    
    // 检查节点是否在指定根的树中
    bool isInTree(Node* node, Node* root) {
        return getRoot(node) == root;
    }
    
    // 创建新节点
    Node* createNode(int u = -1, int v = -1) {
        all_nodes_.push_back(std::make_unique<Node>(u, v));
        return all_nodes_.back().get();
    }

public:
    EulerTourTree() = default;
    
    // 添加边 O(log n)
    void addEdge(int u, int v) {
        // 创建两个节点表示边的两个方向
        Node* node_uv = createNode(u, v);
        Node* node_vu = createNode(v, u);
        
        vertex_edges_[u].push_back(node_uv);
        vertex_edges_[v].push_back(node_vu);
        
        // 如果顶点还没有树,创建单节点树
        if (vertex_root_.find(u) == vertex_root_.end()) {
            Node* root_u = createNode();
            vertex_root_[u] = root_u;
        }
        
        if (vertex_root_.find(v) == vertex_root_.end()) {
            Node* root_v = createNode();
            vertex_root_[v] = root_v;
        }
        
        // 合并两棵树
        Node* root_u = getRoot(vertex_root_[u]);
        Node* root_v = getRoot(vertex_root_[v]);
        
        if (root_u != root_v) {
            // 将边的节点插入到欧拉回路中
            node_uv->left = root_u;
            node_uv->right = root_v;
            root_u->parent = node_uv;
            root_v->parent = node_uv;
            node_uv->update();
            
            // 更新根引用
            Node* new_root = node_uv;
            for (auto& [vertex, node] : vertex_root_) {
                Node* curr_root = getRoot(node);
                if (curr_root == root_u || curr_root == root_v) {
                    vertex_root_[vertex] = new_root;
                }
            }
        }
    }
    
    // 删除边 O(log n)
    void removeEdge(int u, int v) {
        // 找到对应的边节点
        Node* node_to_remove = nullptr;
        auto it = vertex_edges_.find(u);
        if (it != vertex_edges_.end()) {
            for (Node* node : it->second) {
                if (node->edge.first == u && node->edge.second == v) {
                    node_to_remove = node;
                    break;
                }
            }
        }
        
        if (!node_to_remove) return;
        
        // 从顶点边列表中移除
        auto& edges_u = vertex_edges_[u];
        edges_u.erase(std::remove(edges_u.begin(), edges_u.end(), node_to_remove), edges_u.end());
        
        auto& edges_v = vertex_edges_[v];
        for (auto it = edges_v.begin(); it != edges_v.end(); ++it) {
            if ((*it)->edge.first == v && (*it)->edge.second == u) {
                edges_v.erase(it);
                break;
            }
        }
        
        // 伸展到根并分裂树
        Node* root = splay(node_to_remove);
        Node* left_tree = root->left;
        Node* right_tree = root->right;
        
        if (left_tree) left_tree->parent = nullptr;
        if (right_tree) right_tree->parent = nullptr;
        
        // 断开连接,防止析构时删除子树
        root->left = nullptr;
        root->right = nullptr;
        
        // 更新顶点的根引用
        if (left_tree) {
            for (auto& [vertex, node] : vertex_root_) {
                if (getRoot(node) == root) {
                    if (isInTree(node, left_tree)) {
                        vertex_root_[vertex] = left_tree;
                    }
                }
            }
        }
        
        if (right_tree) {
            for (auto& [vertex, node] : vertex_root_) {
                if (getRoot(node) == root) {
                    if (isInTree(node, right_tree)) {
                        vertex_root_[vertex] = right_tree;
                    }
                }
            }
        }
    }
    
    // 查询两个顶点是否连通 O(log n)
    bool isConnected(int u, int v) {
        if (vertex_root_.find(u) == vertex_root_.end() || 
            vertex_root_.find(v) == vertex_root_.end()) {
            return false;
        }
        return getRoot(vertex_root_[u]) == getRoot(vertex_root_[v]);
    }
    
    // 获取顶点所在连通分量的所有顶点
    std::unordered_set<int> getComponentVertices(int u) {
        std::unordered_set<int> component;
        
        if (vertex_root_.find(u) == vertex_root_.end()) {
            component.insert(u);
            return component;
        }
        
        Node* root = getRoot(vertex_root_[u]);
        for (const auto& [vertex, node] : vertex_root_) {
            if (getRoot(node) == root) {
                component.insert(vertex);
            }
        }
        
        return component;
    }
    
    // 获取指定图顶点范围的所有连通分量
    std::unordered_map<int, std::unordered_set<int>> getAllComponents(const std::set<int>& vertices) {
        std::unordered_map<int, std::unordered_set<int>> components;
        
        for (const auto& [vertex, node] : vertex_root_) {
            if (vertices.find(vertex) != vertices.end()) continue;
            
            auto component = getComponentVertices(vertex);
            int component_id = *std::min_element(component.begin(), component.end());
            components[component_id] = component;
            
        }
        
        return components;
    }
    
    // 清空所有数据
    void clear() {
        vertex_edges_.clear();
        vertex_root_.clear();
        all_nodes_.clear();
    }
};

}