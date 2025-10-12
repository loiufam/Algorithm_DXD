#ifndef ETT_H
#define ETT_H 

#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <set>
#include <vector>
#include <algorithm>

// ============================================================================
// ET-Tree 核心实现
// ============================================================================

class ETTree {
private:
    struct Node {
        Node *left, *right, *parent;
        int vertex;
        int subtree_size;
        bool is_repr;
        
        Node(int v = -1, bool repr = false) 
            : left(nullptr), right(nullptr), parent(nullptr),
              vertex(v), subtree_size(1), is_repr(repr) {}
    };
    
    std::unordered_map<int, Node*> vertex_repr;
    std::unordered_map<long long, std::pair<Node*, Node*>> edge_nodes;

    struct ComponentInfo {
        std::set<int> rows;          // 该分量包含的所有行
        std::unordered_set<int> cols;          // 该分量包含的所有列
        
        ComponentInfo() = default;
    };
    
    // 根节点 → 分量信息的映射
    std::unordered_map<Node*, ComponentInfo> component_info;
    
    // 行 → 列集合的快速索引（从外部传入）
    std::function<const std::set<int>&(int)> get_row_cols;

    void update(Node* x) {
        if (!x) return;
        x->subtree_size = 1;
        if (x->left) x->subtree_size += x->left->subtree_size;
        if (x->right) x->subtree_size += x->right->subtree_size;
    }
    
    void rotate_left(Node* x) {
        Node* y = x->right;
        Node* p = x->parent;
        x->right = y->left;
        if (y->left) y->left->parent = x;
        y->left = x;
        x->parent = y;
        y->parent = p;
        if (p) {
            if (p->left == x) p->left = y;
            else p->right = y;
        }
        update(x);
        update(y);
    }
    
    void rotate_right(Node* x) {
        Node* y = x->left;
        Node* p = x->parent;
        x->left = y->right;
        if (y->right) y->right->parent = x;
        y->right = x;
        x->parent = y;
        y->parent = p;
        if (p) {
            if (p->left == x) p->left = y;
            else p->right = y;
        }
        update(x);
        update(y);
    }
    
    void splay(Node* x) {
        if (!x) return;
        while (x->parent) {
            Node* p = x->parent;
            Node* g = p->parent;
            if (!g) {
                if (p->left == x) rotate_right(p);
                else rotate_left(p);
            } else if (g->left == p && p->left == x) {
                rotate_right(g);
                rotate_right(p);
            } else if (g->right == p && p->right == x) {
                rotate_left(g);
                rotate_left(p);
            } else if (g->left == p && p->right == x) {
                rotate_left(p);
                rotate_right(g);
            } else {
                rotate_right(p);
                rotate_left(g);
            }
        }
    }
    
    Node* get_root(Node* x) {
        if (!x) return nullptr;
        splay(x);
        while (x->left) x = x->left;
        splay(x);
        return x;
    }
    
    std::pair<Node*, Node*> split(Node* x) {
        if (!x) return {nullptr, nullptr};
        splay(x);
        Node* right = x->right;
        if (right) {
            right->parent = nullptr;
            x->right = nullptr;
            update(x);
        }
        return {x, right};
    }
    
    Node* merge(Node* left, Node* right) {
        if (!left) return right;
        if (!right) return left;
        while (left->right) left = left->right;
        splay(left);
        left->right = right;
        right->parent = left;
        update(left);
        return left;
    }
    
    long long edge_id(int u, int v) {
        if (u > v) std::swap(u, v);
        return ((long long)u << 32) | v;
    }
    
    Node* create_node(int vertex, bool is_repr = false) {
        return new Node(vertex, is_repr);
    }

    // ========================================================================
    // 分量信息维护
    // ========================================================================
    
    /**
     * 初始化单个顶点的分量信息
     */
    void init_component_info(Node* root, int vertex) {
        ComponentInfo& info = component_info[root];
        info.rows.insert(vertex);
        
        // 获取该行的所有列
        if (get_row_cols) {
            const auto& cols = get_row_cols(vertex);
            info.cols.insert(cols.begin(), cols.end());
        }
    }
    
    /**
     * 合并两个分量的信息
     */
    void merge_component_info(Node* new_root, Node* root1, Node* root2) {
        ComponentInfo& new_info = component_info[new_root];
        
        // 合并第一个分量的信息
        if (component_info.find(root1) != component_info.end()) {
            const auto& info1 = component_info[root1];
            new_info.rows.insert(info1.rows.begin(), info1.rows.end());
            new_info.cols.insert(info1.cols.begin(), info1.cols.end());
            component_info.erase(root1);
        }
        
        // 合并第二个分量的信息
        if (component_info.find(root2) != component_info.end()) {
            const auto& info2 = component_info[root2];
            new_info.rows.insert(info2.rows.begin(), info2.rows.end());
            new_info.cols.insert(info2.cols.begin(), info2.cols.end());
            component_info.erase(root2);
        }
    }
    
    /**
     * 分裂分量信息
     */
    void split_component_info(Node* old_root, Node* new_root1, Node* new_root2) {
        if (component_info.find(old_root) == component_info.end()) {
            return;
        }
        
        // 获取旧的分量信息
        ComponentInfo old_info = component_info[old_root];
        component_info.erase(old_root);
        
        // 重新计算两个新分量的信息
        ComponentInfo& info1 = component_info[new_root1];
        ComponentInfo& info2 = component_info[new_root2];
        
        // 遍历旧分量的所有行，判断属于哪个新分量
        for (int row : old_info.rows) {
            Node* row_node = vertex_repr[row];
            Node* root = get_root(row_node);
            
            if (root == new_root1) {
                info1.rows.insert(row);
                if (get_row_cols) {
                    const auto& cols = get_row_cols(row);
                    info1.cols.insert(cols.begin(), cols.end());
                }
            } else if (root == new_root2) {
                info2.rows.insert(row);
                if (get_row_cols) {
                    const auto& cols = get_row_cols(row);
                    info2.cols.insert(cols.begin(), cols.end());
                }
            }
        }
    }

public:

    ETTree() = default;
    
    /**
     * 设置获取行列信息的回调函数
     */
    void set_row_cols_getter(std::function<const std::set<int>&(int)> getter) {
        get_row_cols = getter;
    }

    void make_tree(int v) {
        if (vertex_repr.find(v) != vertex_repr.end()) return;
        Node* node = create_node(v, true);
        vertex_repr[v] = node;

        // 初始化分量信息
        init_component_info(node, v);
    }
    
    bool link(int u, int v) {
        if (connected(u, v)) return false;
        
        make_tree(u);
        make_tree(v);
        
        Node* repr_u = vertex_repr[u];
        Node* repr_v = vertex_repr[v];
        Node* root_u = get_root(repr_u);
        Node* root_v = get_root(repr_v);
        
        Node* edge_uv = create_node(-1);
        Node* edge_vu = create_node(-1);
        edge_nodes[edge_id(u, v)] = {edge_uv, edge_vu};
        
        auto [u_left, u_right] = split(repr_u);
        auto [v_left, v_right] = split(repr_v);
        
        Node* new_root = merge(u_left, edge_uv);
        new_root = merge(new_root, v_left);
        new_root = merge(new_root, repr_v);
        new_root = merge(new_root, v_right);
        new_root = merge(new_root, edge_vu);
        new_root = merge(new_root, repr_u);
        new_root = merge(new_root, u_right);
        
        merge_component_info(new_root, root_u, root_v);
        return true;
    }
    
    bool cut(int u, int v) {
        long long eid = edge_id(u, v);
        if (edge_nodes.find(eid) == edge_nodes.end()) return false;
        
        auto [edge_uv, edge_vu] = edge_nodes[eid];
        // 获取切割前的根节点
        Node* old_root = get_root(edge_uv);

        edge_nodes.erase(eid);
        
        splay(edge_uv);
        auto [left1, right1] = split(edge_uv);
        splay(edge_vu);
        auto [left2, right2] = split(edge_vu);
        
        Node* new_root1 = merge(left1, right1);
        Node* new_root2 = merge(left2, right2);
        
        delete edge_uv;
        delete edge_vu;

        split_component_info(old_root, new_root1, new_root2);
        
        return true;
    }
    
    bool connected(int u, int v) {
        if (vertex_repr.find(u) == vertex_repr.end() || 
            vertex_repr.find(v) == vertex_repr.end()) {
            return false;
        }
        return get_root(vertex_repr[u]) == get_root(vertex_repr[v]);
    }
    
    std::unordered_map<int, ComponentInfo> get_components() {
        std::unordered_map<int, ComponentInfo> result;
        
        int comp_id = 0;
        for (const auto& [root, info] : component_info) {
            result[comp_id++] = info;
        }
        
        return result;
    }

    /**
     * 【优化后】更新行的列信息（当矩阵元素改变时调用）
     */
    void update_row_columns(int row, const std::set<int>& new_cols) {
        if (vertex_repr.find(row) == vertex_repr.end()) {
            return;
        }
        
        Node* root = get_root(vertex_repr[row]);
        auto it = component_info.find(root);
        
        if (it != component_info.end()) {
            ComponentInfo& info = it->second;
            
            // 移除该行的旧列
            if (get_row_cols) {
                const auto& old_cols = get_row_cols(row);
                for (int col : old_cols) {
                    // 检查是否有其他行包含该列
                    bool has_other = false;
                    for (int other_row : info.rows) {
                        if (other_row != row) {
                            const auto& other_cols = get_row_cols(other_row);
                            if (other_cols.count(col)) {
                                has_other = true;
                                break;
                            }
                        }
                    }
                    if (!has_other) {
                        info.cols.erase(col);
                    }
                }
            }
            
            // 添加新列
            info.cols.insert(new_cols.begin(), new_cols.end());
        }
    }
    
    bool has_edge(int u, int v) {
        return edge_nodes.find(edge_id(u, v)) != edge_nodes.end();
    }
};


#endif