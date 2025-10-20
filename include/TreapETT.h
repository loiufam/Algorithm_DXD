#ifndef TREAPETT_H
#define TREAPETT_H

// ===============================================
// ETTree (Treap-based Euler Tour Tree)
// ===============================================
//   ✅ Treap 实现高效 link/cut
//   ✅ Lazy component_info 更新
//   ✅ 支持 findComponentsInBlock 增量更新模式
//   ✅ 可直接在 DancingMatrix 中使用
// ===============================================

#include <bits/stdc++.h>
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

    void printBlock(int block_id) {
        if (rows.empty() || cols.empty()) return;
        cout << "Block " << block_id << ": { ";
        vector<int> R(rows.begin(), rows.end());
        vector<int> C(cols.begin(), cols.end());
        sort(R.begin(), R.end());
        sort(C.begin(), C.end());
        cout<< "cols: [ ";
        for(int c : C){
            cout << c << " ";
        }
        cout << "], ";
        cout<< "rows: [ ";
        for(int r : R){
            cout<< r << " ";
        }
        cout << "] }" << endl;
    }
};

// ------------------- Treap Node -------------------
struct ETTreapNode {
    ETTreapNode *l, *r, *p;
    unsigned pr; // priority
    int size;    // subtree size
    int vertex;  // >=0: vertex occurrence; -1: edge marker
    bool is_repr;
    bool is_edge;

    ETTreapNode(int v = -1, bool edge = false, bool repr = false)
        : l(nullptr), r(nullptr), p(nullptr), pr(rand()), size(1),
          vertex(v), is_repr(repr), is_edge(edge) {}
};

inline int tsz(ETTreapNode *t) { return t ? t->size : 0; }
inline void pull(ETTreapNode *t) {
    if (!t) return;
    t->size = 1 + tsz(t->l) + tsz(t->r);
    if (t->l) t->l->p = t;
    if (t->r) t->r->p = t;
}

inline ETTreapNode *treap_merge(ETTreapNode *a, ETTreapNode *b) {
    if (!a) { if (b) b->p = nullptr; return b; }
    if (!b) { a->p = nullptr; return a; }
    if (a->pr < b->pr) {
        a->r = treap_merge(a->r, b);
        pull(a);
        a->p = nullptr;
        return a;
    } else {
        b->l = treap_merge(a, b->l);
        pull(b);
        b->p = nullptr;
        return b;
    }
}

inline pair<ETTreapNode *, ETTreapNode *> treap_split(ETTreapNode *t, int k) {
    if (!t) return {nullptr, nullptr};
    if (tsz(t->l) >= k) {
        auto pr = treap_split(t->l, k);
        t->l = pr.second;
        pull(t);
        if (pr.first) pr.first->p = nullptr;
        if (t->l) t->l->p = t;
        return {pr.first, t};
    } else {
        auto pr = treap_split(t->r, k - tsz(t->l) - 1);
        t->r = pr.first;
        pull(t);
        if (pr.second) pr.second->p = nullptr;
        if (t->r) t->r->p = t;
        return {t, pr.second};
    }
}

inline ETTreapNode *treap_get_root(ETTreapNode *x) {
    if (!x) return nullptr;
    while (x->p) x = x->p;
    return x;
}

inline int treap_get_index(ETTreapNode *x) {
    int idx = tsz(x->l);
    ETTreapNode *cur = x;
    while (cur->p) {
        if (cur == cur->p->r) idx += 1 + tsz(cur->p->l);
        cur = cur->p;
    }
    return idx;
}

inline void collect_repr_rows(ETTreapNode *root, unordered_set<int> &out) {
    if (!root) return;
    vector<ETTreapNode *> st{root};
    while (!st.empty()) {
        ETTreapNode *cur = st.back(); st.pop_back();
        if (cur->is_repr && cur->vertex >= 0) out.insert(cur->vertex);
        if (cur->l) st.push_back(cur->l);
        if (cur->r) st.push_back(cur->r);
    }
}

// ------------------- ETTree Class -------------------
class ETTree {
public:
    ETTree() { srand(1234567); }

    ~ETTree() {
        for (auto *n : nodes_allocated) delete n;
    }

    void set_row_cols_getter(function<const set<int>&(int)> g) { get_row_cols = g; }

    inline void make_tree(int v) {
        if (vertex_repr.count(v)) return;
        auto *n = new_node(v, false, true);
        vertex_repr[v] = n;
        active_rows.insert(v);

        ETTreapNode *root = treap_get_root(n);
        Block &b = component_info[root];
        b.rows.insert(v);
        if (get_row_cols) {
            const set<int> &cols = get_row_cols(v);
            b.cols.insert(cols.begin(), cols.end());
        }
    }

    inline bool connected(int u, int v) {
        if (!vertex_repr.count(u) || !vertex_repr.count(v)) return false;
        return treap_get_root(vertex_repr[u]) == treap_get_root(vertex_repr[v]);
    }

    inline bool has_edge(int u, int v) {
        return edge_nodes.count(edge_id(u, v));
    }

    inline bool link(int u, int v) {
        if (connected(u, v)) return false;
        make_tree(u); make_tree(v);
        auto *ru = treap_get_root(vertex_repr[u]);
        auto *rv = treap_get_root(vertex_repr[v]);

        auto *edge = new_node(-1, true, false);
        edge_nodes[edge_id(u, v)] = edge;

        auto *merged = treap_merge(ru, edge);
        merged = treap_merge(merged, rv);

        auto *root = treap_get_root(merged);
        component_info.erase(ru);
        component_info.erase(rv);
        component_info[root];

        row_adjacency[u].insert(v);
        row_adjacency[v].insert(u);
        return true;
    }

    inline bool cut(int u, int v) {
        long long eid = edge_id(u, v);
        auto it = edge_nodes.find(eid);
        if (it == edge_nodes.end()) return false;
        auto *edge = it->second;
        auto *root = treap_get_root(edge);

        int pos = treap_get_index(edge);
        auto p1 = treap_split(root, pos);
        auto p2 = treap_split(p1.second, 1);

        auto *left = p1.first;
        auto *right = p2.second;

        edge_nodes.erase(it);
        delete edge;

        if (left) left->p = nullptr;
        if (right) right->p = nullptr;

        row_adjacency[u].erase(v);
        row_adjacency[v].erase(u);

        component_info.erase(root);
        component_info[left];
        component_info[right];
        return true;
    }

    const Block &get_component_info_of_node(ETTreapNode *node) {
        ETTreapNode *root = treap_get_root(node);
        auto it = component_info.find(root);
        if (it == component_info.end() || it->second.rows.empty()) {
            Block b; unordered_set<int> rows;
            collect_repr_rows(root, rows);
            b.rows = std::move(rows);
            if (get_row_cols) for (int r : b.rows) {
                const set<int> &cols = get_row_cols(r);
                b.cols.insert(cols.begin(), cols.end());
            }
            component_info[root] = std::move(b);
            return component_info[root];
        }
        return it->second;
    }

    inline const unordered_set<int> &getActiveRows() const { return active_rows; }
    inline void deactivateRow(int r) { active_rows.erase(r); deactivated_rows.insert(r); }
    inline void reactivateRow(int r) { deactivated_rows.erase(r); active_rows.insert(r); }
    inline bool isRowActive(int r) const { return active_rows.count(r); }

    // ================= findComponentsInBlock（增量式） =================
    std::vector<Block> findComponentsInBlock(const std::unordered_set<int> &block_rows) {
        vector<int> added, removed;
        for (int r : block_rows) if (!cached_block_rows.count(r)) added.push_back(r);
        for (int r : cached_block_rows) if (!block_rows.count(r)) removed.push_back(r);

        bool small_delta = (added.size() + removed.size()) * 5 < block_rows.size();

        if (!small_delta) {
            cached_root_blocks.clear();
            cached_row_root.clear();
            cached_block_rows = block_rows;

            for (int row : block_rows) {
                if (!isRowActive(row)) continue;
                auto it = vertex_repr.find(row);
                if (it == vertex_repr.end()) continue;
                auto *root = treap_get_root(it->second);

                Block &blk = cached_root_blocks[root];
                blk.rows.insert(row);
                if (get_row_cols) {
                    const auto &cols = get_row_cols(row);
                    blk.cols.insert(cols.begin(), cols.end());
                }
                cached_row_root[row] = root;
            }
        } else {
            for (int r : removed) {
                auto it = cached_row_root.find(r);
                if (it != cached_row_root.end()) {
                    auto *root = it->second;
                    auto blk_it = cached_root_blocks.find(root);
                    if (blk_it != cached_root_blocks.end()) blk_it->second.rows.erase(r);
                    cached_row_root.erase(it);
                }
                cached_block_rows.erase(r);
            }

            for (int r : added) {
                if (!isRowActive(r)) continue;
                auto it = vertex_repr.find(r);
                if (it == vertex_repr.end()) continue;
                auto *root = treap_get_root(it->second);

                Block &blk = cached_root_blocks[root];
                blk.rows.insert(r);
                if (get_row_cols) {
                    const auto &cols = get_row_cols(r);
                    blk.cols.insert(cols.begin(), cols.end());
                }
                cached_row_root[r] = root;
                cached_block_rows.insert(r);
            }
        }

        vector<Block> result;
        result.reserve(cached_root_blocks.size());
        for (auto &[r, blk] : cached_root_blocks) {
            if (!blk.rows.empty()) result.push_back(blk);
        }
        return result;
    }

private:
    unordered_map<int, ETTreapNode *> vertex_repr;
    unordered_map<long long, ETTreapNode *> edge_nodes;
    unordered_map<ETTreapNode *, Block> component_info;
    unordered_map<int, unordered_set<int>> row_adjacency;
    unordered_set<int> active_rows, deactivated_rows;
    vector<ETTreapNode *> nodes_allocated;
    function<const set<int>&(int)> get_row_cols;

    // 缓存（增量模式）
    unordered_map<ETTreapNode *, Block> cached_root_blocks;
    unordered_map<int, ETTreapNode *> cached_row_root;
    unordered_set<int> cached_block_rows;

    ETTreapNode *new_node(int v, bool edge = false, bool repr = false) {
        auto *n = new ETTreapNode(v, edge, repr);
        nodes_allocated.push_back(n);
        return n;
    }

    long long edge_id(int u, int v) {
        if (u > v) swap(u, v);
        return ((long long)u << 32) | (unsigned long long)v;
    }
};

#endif 
