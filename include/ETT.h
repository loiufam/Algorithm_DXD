#ifndef ETT_H
#define ETT_H 

#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <set>
#include <queue>
#include <stack>
#include <vector>
#include <algorithm>
using namespace std;

struct Block {
    unordered_set<int> rows;  // 舞蹈链行id集合 
    unordered_set<int> cols;  // 从1开始编号,对应舞蹈链列数id
    bool is_spilited = false;
    
    Block() = default;

    size_t size() const { return rows.size(); }

    Block(const unordered_set<int>& r, const unordered_set<int>& c) :  rows(r), cols(c) {}

    Block(const set<int>& r, const set<int>& c, bool is_spilited = true){
        rows.insert(r.begin(), r.end());
        cols.insert(c.begin(), c.end());
    }

    Block(const vector<int>& r, const unordered_set<int>& c) : cols(c) {
        rows.insert(r.begin(), r.end());
    }

    Block(const set<int>& r, const unordered_set<int>& c) : cols(c) {
        rows.insert(r.begin(), r.end());
    }

    Block(const Block& other) {
        cols = other.cols;
        rows = other.rows;
    };

    void printBlock() {
        cout<< "cols: " << endl;
        for(int c : cols){
            cout << c << " ";
        }
        cout<<endl;
        cout<< "rows: " << endl;
        for(int r : rows){
            cout<< r << " ";
        }
        cout<<endl; 
    }
};

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

    // 根节点 → 分量信息的映射
    std::unordered_map<Node*, Block> component_info;
    
    // 行 → 列集合的快速索引（从外部传入）
    std::function<const std::set<int>&(int)> get_row_cols;

    // ========================================================================
    // Row 激活/失活状态管理
    // ========================================================================
    
    std::unordered_set<int> active_rows;      // 当前活跃的行集合
    std::unordered_set<int> deactivated_rows; // 被deactivate的行

    // 存储每行的邻接关系（用于局部搜索）
    std::unordered_map<int, std::unordered_set<int>> row_adjacency;


    // ========================================================================
    // Splay Tree 基础操作
    // ========================================================================

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
        // 向上找到真正的 root
        while (x->parent) x = x->parent;
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
    
    void collect_repr_rows(Node* node, std::unordered_set<int>& out_rows) {
        if (!node) return;
        std::stack<Node*> st;
        st.push(node);
        while (!st.empty()) {
            Node* cur = st.top(); st.pop();
            if (!cur) continue;
            if (cur->is_repr && cur->vertex >= 0) {
                out_rows.insert(cur->vertex);
            }
            if (cur->left) st.push(cur->left);
            if (cur->right) st.push(cur->right);
        }
    }

    /**
     * 初始化单个顶点的分量信息
     */
    void init_component_info(Node* root, int vertex) {
        Block& info = component_info[root];
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
    void merge_component_info(Node* new_root_raw, Node* root1_raw, Node* root2_raw) {
        Node* new_root = get_root(new_root_raw);
        Node* r1 = get_root(root1_raw);
        Node* r2 = get_root(root2_raw);
        
        Block& new_info = component_info[new_root];
        
        auto it1 = component_info.find(r1);
        if (it1 != component_info.end()) {
            new_info.rows.insert(it1->second.rows.begin(), it1->second.rows.end());
            new_info.cols.insert(it1->second.cols.begin(), it1->second.cols.end());
            component_info.erase(it1);
        }


        auto it2 = component_info.find(r2);
        if (it2 != component_info.end()) {
            new_info.rows.insert(it2->second.rows.begin(), it2->second.rows.end());
            new_info.cols.insert(it2->second.cols.begin(), it2->second.cols.end());
            component_info.erase(it2);
        }
    }
    
    /**
     * 分裂分量信息
     */
    void split_component_info(Node* old_root_raw, Node* new_root1_raw, Node* new_root2_raw) {
        Node* old_root = get_root(old_root_raw);
        Node* nr1 = get_root(new_root1_raw);
        Node* nr2 = get_root(new_root2_raw);

        // 移除旧映射（若存在）
        component_info.erase(old_root);

        // 收集新子树的代表行
        std::unordered_set<int> rows1, rows2;
        collect_repr_rows(nr1, rows1);
        collect_repr_rows(nr2, rows2);

        Block& info1 = component_info[nr1];
        Block& info2 = component_info[nr2];

        info1.rows = std::move(rows1);
        info2.rows = std::move(rows2);


        if (get_row_cols) {
            for (int r : info1.rows) {
                const auto& cols = get_row_cols(r);
                info1.cols.insert(cols.begin(), cols.end());
            }
            for (int r : info2.rows) {
                const auto& cols = get_row_cols(r);
                info2.cols.insert(cols.begin(), cols.end());
            }
        }
    }

public:

    ETTree() = default;

    ~ETTree() {
        for (auto& [v, node] : vertex_repr) {
            delete node;
        }
        for (auto& [eid, nodes] : edge_nodes) {
            delete nodes.first;
            delete nodes.second;
        }
    }
    
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
        active_rows.insert(v);
        // 初始化分量信息
        init_component_info(node, v);
    }
    
    bool link(int u, int v) {
        if (connected(u, v)) return false;
        
        // make_tree(u);
        // make_tree(v);
        
        Node* repr_u = vertex_repr[u];
        Node* repr_v = vertex_repr[v];
        Node* root_u = get_root(repr_u);
        Node* root_v = get_root(repr_v);
        
        Node* edge_uv = create_node(-1);
        Node* edge_vu = create_node(-1);
        edge_nodes[edge_id(u, v)] = {edge_uv, edge_vu};
        
        // 维护邻接关系
        row_adjacency[u].insert(v);
        row_adjacency[v].insert(u);

        auto [u_left, u_right] = split(repr_u);
        auto [v_left, v_right] = split(repr_v);
        
        Node* new_root = merge(u_left, edge_uv);
        new_root = merge(new_root, v_left);
        new_root = merge(new_root, repr_v);
        new_root = merge(new_root, v_right);
        new_root = merge(new_root, edge_vu);
        new_root = merge(new_root, repr_u);
        new_root = merge(new_root, u_right);
        
        cout << "start merge" << endl;
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

        // 更新邻接关系
        row_adjacency[u].erase(v);
        row_adjacency[v].erase(u);
        
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

    /**
     * Deactivate一行：标记为不活跃，但不删除边
     * 用于coverColumn时移除包含该列的行
     */
    void deactivateRow(int row) {
        if (active_rows.find(row) != active_rows.end()) {
            active_rows.erase(row);
            deactivated_rows.insert(row);
        }
    }
    
    /**
     * Reactivate一行：恢复为活跃状态
     * 用于uncoverColumn时恢复行
     */
    void reactivateRow(int row) {
        if (deactivated_rows.find(row) != deactivated_rows.end()) {
            deactivated_rows.erase(row);
            active_rows.insert(row);
        }
    }
    
    /**
     * 检查行是否活跃
     */
    bool isRowActive(int row) const {
        return active_rows.find(row) != active_rows.end();
    }
    
    /**
     * 获取所有活跃的行
     */
    const std::unordered_set<int>& getActiveRows() const {
        return active_rows;
    }
    
    /**
     * 在指定的行和列范围内，找出所有连通分量
     * @param block_rows 块内的行集合
     * @param block_cols 块内的列集合
     * @return 每个连通分量的Block（包含行集合和列集合）
     */
    std::vector<Block> findComponentsInBlock(
        const std::unordered_set<int>& block_rows) {
        
        std::vector<Block> components;
        std::unordered_set<int> visited;
        
        // 只处理块内且活跃的行
        for (int start_row : block_rows) {
            if (visited.count(start_row)) continue;
            if (!isRowActive(start_row)) continue;
            if (vertex_repr.find(start_row) == vertex_repr.end()) continue;
            
            // BFS 找出连通分量
            Block component;
            std::queue<int> q;
            q.push(start_row);
            visited.insert(start_row);
            
            while (!q.empty()) {
                int current = q.front();
                q.pop();
                component.rows.insert(current);

                // 收集该行的列
                if (get_row_cols) {
                    const auto& cols = get_row_cols(current);
                    component.cols.insert(cols.begin(), cols.end());
                }
                
                // 遍历邻接行
                if (row_adjacency.find(current) != row_adjacency.end()) {
                    for (int neighbor : row_adjacency[current]) {
                        // 只访问块内、活跃、未访问的行
                        if (visited.count(neighbor)) continue;
                        if (!block_rows.count(neighbor)) continue;
                        if (!isRowActive(neighbor)) continue;
                        
                        visited.insert(neighbor);
                        q.push(neighbor);
                    }
                }
            }
            
            if (!component.rows.empty()) {
                components.push_back(component);
            }
        }
        
        return components;
    }

    // ========================================================================
    // 优化：增量式信息查询
    // ========================================================================
    
    /**
     * 获取所有活跃行的全局连通分量
     * 返回每个组件的Block
     */
    std::vector<Block> getActiveComponents() const {
        std::vector<Block> result;
        
        for (const auto& [root, info] : component_info) {
            Block active_info;
            
            // 只包含活跃的行
            for (int row : info.rows) {
                if (isRowActive(row)) {
                    active_info.rows.insert(row);
                }
            }
            
            // 重新计算活跃行对应的列
            if (!active_info.rows.empty() && get_row_cols) {
                for (int row : active_info.rows) {
                    const auto& cols = get_row_cols(row);
                    active_info.cols.insert(cols.begin(), cols.end());
                }
            }
            
            if (!active_info.rows.empty()) {
                result.push_back(active_info);
            }
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
            Block& info = it->second;
            
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