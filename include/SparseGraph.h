#ifndef SPARSEGRAPH_H
#define SPARSEGRAPH_H

#include <iostream>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <shared_mutex>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <stack>

// ============================================================================
// 并查集（用于动态维护列的连通性）
// ============================================================================
class UnionFind {
private:
    mutable std::unordered_map<int, int> parent;
    mutable std::unordered_map<int, int> rank;
    mutable std::shared_mutex mutex;
    
public:
    void makeSet(int x) {
        std::unique_lock lock(mutex);
        if (parent.find(x) == parent.end()) {
            parent[x] = x;
            rank[x] = 0;
        }
    }
    
    int find(int x) {
        std::unique_lock lock(mutex);
        if (parent.find(x) == parent.end()) {
            parent[x] = x;
            rank[x] = 0;
            return x;
        }
        
        if (parent[x] != x) {
            parent[x] = find(parent[x]);  // 路径压缩
        }
        return parent[x];
    }
    
    bool unite(int x, int y) {
        std::unique_lock lock(mutex);
        int px = find(x);
        int py = find(y);
        
        if (px == py) return false;
        
        if (rank[px] < rank[py]) {
            parent[px] = py;
        } else if (rank[px] > rank[py]) {
            parent[py] = px;
        } else {
            parent[py] = px;
            rank[px]++;
        }
        return true;
    }
    
    std::unordered_set<int> getAllInSet(int x) {
        std::shared_lock lock(mutex);
        int root = find(x);
        std::unordered_set<int> result;
        
        for (const auto& [elem, _] : parent) {
            if (find(elem) == root) {
                result.insert(elem);
            }
        }
        return result;
    }
    
    void clear() {
        std::unique_lock lock(mutex);
        parent.clear();
        rank.clear();
    }
};

// ============================================================================
// 线程安全的 Link-Cut Tree（基于行的连通性）
// ============================================================================
class LCTNode {
public:
    int row_id;  // 行ID
    LCTNode* parent;
    LCTNode* left;
    LCTNode* right;
    bool reversed;
    
    mutable std::shared_mutex mutex;
    
    LCTNode(int id) : row_id(id), parent(nullptr), 
                      left(nullptr), right(nullptr), reversed(false) {}
    
    bool isRoot() const {
        return parent == nullptr || 
               (parent->left != this && parent->right != this);
    }
    
    void pushDown() {
        if (reversed) {
            std::swap(left, right);
            if (left) left->reversed ^= true;
            if (right) right->reversed ^= true;
            reversed = false;
        }
    }
};

class LinkCutTree {
private:
    std::unordered_map<int, std::unique_ptr<LCTNode>> nodes;
    mutable std::shared_mutex global_mutex;
    
    void rotate(LCTNode* x) {
        LCTNode* p = x->parent;
        LCTNode* g = p->parent;
        
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
            else if (g->right == p) g->right = x;
        }
    }
    
    void splay(LCTNode* x) {
        while (!x->isRoot()) {
            LCTNode* p = x->parent;
            LCTNode* g = p->parent;
            
            if (!p->isRoot()) g->pushDown();
            p->pushDown();
            x->pushDown();
            
            if (!p->isRoot()) {
                if ((g->left == p) == (p->left == x)) {
                    rotate(p);
                } else {
                    rotate(x);
                }
            }
            rotate(x);
        }
        x->pushDown();
    }
    
    void access(LCTNode* x) {
        LCTNode* last = nullptr;
        for (LCTNode* y = x; y; y = y->parent) {
            splay(y);
            y->right = last;
            last = y;
        }
        splay(x);
    }
    
    void makeRoot(LCTNode* x) {
        access(x);
        x->reversed ^= true;
    }
    
    LCTNode* findRoot(LCTNode* x) {
        access(x);
        while (x->left) {
            x = x->left;
            x->pushDown();
        }
        splay(x);
        return x;
    }
    
public:
    LinkCutTree() = default;
    
    void ensureNode(int row) {
        std::unique_lock lock(global_mutex);
        if (nodes.find(row) == nodes.end()) {
            nodes[row] = std::make_unique<LCTNode>(row);
        }
    }
    
    bool link(int row1, int row2) {
        ensureNode(row1);
        ensureNode(row2);
        
        std::shared_lock read_lock(global_mutex);
        LCTNode* n1 = nodes[row1].get();
        LCTNode* n2 = nodes[row2].get();
        
        std::unique_lock lock1(n1->mutex);
        std::unique_lock lock2(n2->mutex);
        
        if (findRoot(n1) == findRoot(n2)) {
            return false;
        }
        
        makeRoot(n1);
        n1->parent = n2;
        return true;
    }
    
    bool cut(int row1, int row2) {
        std::shared_lock read_lock(global_mutex);
        
        auto it1 = nodes.find(row1);
        auto it2 = nodes.find(row2);
        if (it1 == nodes.end() || it2 == nodes.end()) {
            return false;
        }
        
        LCTNode* n1 = it1->second.get();
        LCTNode* n2 = it2->second.get();
        
        std::unique_lock lock1(n1->mutex);
        std::unique_lock lock2(n2->mutex);
        
        makeRoot(n1);
        access(n2);
        
        if (n2->left != n1 || n1->right != nullptr) {
            return false;
        }
        
        n2->left = nullptr;
        n1->parent = nullptr;
        return true;
    }
    
    bool connected(int row1, int row2) {
        std::shared_lock read_lock(global_mutex);
        
        auto it1 = nodes.find(row1);
        auto it2 = nodes.find(row2);
        if (it1 == nodes.end() || it2 == nodes.end()) {
            return false;
        }
        
        LCTNode* n1 = it1->second.get();
        LCTNode* n2 = it2->second.get();
        
        std::shared_lock lock1(n1->mutex, std::defer_lock);
        std::shared_lock lock2(n2->mutex, std::defer_lock);
        std::lock(lock1, lock2);
        
        return findRoot(n1) == findRoot(n2);
    }
    
    int findComponent(int row) {
        std::shared_lock read_lock(global_mutex);
        
        auto it = nodes.find(row);
        if (it == nodes.end()) {
            return -1;
        }
        
        LCTNode* node = it->second.get();
        std::shared_lock lock(node->mutex);
        
        LCTNode* root = findRoot(node);
        return root->row_id;
    }
    
    std::vector<int> getAllRows() const {
        std::shared_lock lock(global_mutex);
        std::vector<int> result;
        for (const auto& [id, _] : nodes) {
            result.push_back(id);
        }
        return result;
    }
    
    void removeNode(int row) {
        std::unique_lock lock(global_mutex);
        nodes.erase(row);
    }
};

// ============================================================================
// 矩阵行信息
// ============================================================================
struct MatrixRow {
    int row_id;
    std::unordered_set<int> cols;  // 该行涉及的列（从1开始）
    std::unordered_set<int> adjacent_rows;  // 通过列相邻的行
    bool is_active;  // 是否激活（未被删除）
    mutable std::shared_mutex mutex;
    
    MatrixRow(int id) : row_id(id), is_active(true) {}
    
    void addColumn(int col) {
        std::unique_lock lock(mutex);
        cols.insert(col);
    }
    
    void addAdjacentRow(int row) {
        std::unique_lock lock(mutex);
        adjacent_rows.insert(row);
    }
    
    void removeAdjacentRow(int row) {
        std::unique_lock lock(mutex);
        adjacent_rows.erase(row);
    }
    
    std::unordered_set<int> getAdjacentRows() const {
        std::shared_lock lock(mutex);
        return adjacent_rows;
    }
    
    std::unordered_set<int> getCols() const {
        std::shared_lock lock(mutex);
        return cols;
    }
    
    void setActive(bool active) {
        std::unique_lock lock(mutex);
        is_active = active;
    }
    
    bool isActive() const {
        std::shared_lock lock(mutex);
        return is_active;
    }
};

class SparseGraph {
private:
    // 行ID -> 行信息
    std::unordered_map<int, std::shared_ptr<MatrixRow>> rows;

    LinkCutTree lct;  // 维护行的连通性
    UnionFind col_uf;  // 维护列的连通性

    mutable std::shared_mutex global_mutex;
    
    // 边存储：(row1, row2)，row1 < row2
    std::unordered_set<std::pair<int, int>, 
        std::hash<std::pair<int, int>>> edges;
    
    struct pair_hash {
        size_t operator()(const std::pair<int, int>& p) const {
            return std::hash<int>()(std::min(p.first, p.second)) ^ 
                   (std::hash<int>()(std::max(p.first, p.second)) << 1);
        }
    };
    
    // 更新两行之间的连通性
    void updateRowConnection(int row1, int row2, bool connect) {
        if (row1 == row2) return;
        if (row1 > row2) std::swap(row1, row2);
        
        auto it1 = rows.find(row1);
        auto it2 = rows.find(row2);
        if (it1 == rows.end() || it2 == rows.end()) return;
        
        if (!it1->second->isActive() || !it2->second->isActive()) return;
        
        if (connect) {
            if (edges.insert({row1, row2}).second) {
                it1->second->addAdjacentRow(row2);
                it2->second->addAdjacentRow(row1);
                lct.link(row1, row2);
            }
        } else {
            if (edges.erase({row1, row2})) {
                it1->second->removeAdjacentRow(row2);
                it2->second->removeAdjacentRow(row1);
                lct.cut(row1, row2);
            }
        }
    }

    


};


#endif // SPARSEGRAPH_H