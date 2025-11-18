#include <bits/stdc++.h>
using namespace std;

struct SplayETTNode;
using ETTNodePtr = shared_ptr<SplayETTNode>;

struct SplayETTNode {
    ETTNodePtr l = nullptr, r = nullptr;
    weak_ptr<SplayETTNode> p;
    int vertex; // vertex id this occurrence belongs to (or -1 for non-vertex / edge-occurrence)
    int edge_u, edge_v; // if this node is an edge-occurrence, store endpoints (u->v)
    int sz = 1;


    SplayETTNode(int vertex_ = -1, int u = -1, int v = -1)
    : vertex(vertex_), edge_u(u), edge_v(v), sz(1) {}
};

inline int getsz(const ETTNodePtr& t) { return t ? t->sz : 0; }
inline void pull(const ETTNodePtr& t) {
    if (!t) return;
    t->sz = 1 + getsz(t->l) + getsz(t->r);
    if (t->l) t->l->p = t;
    if (t->r) t->r->p = t;
}

// Get root of tree containing node x
inline ETTNodePtr get_root(ETTNodePtr x) {
    if (!x) return nullptr;
    while (x->p.lock()) x = x->p.lock();
    return x;
}

// rotate x up (x must have a parent)
inline void rotate(ETTNodePtr x) {
    auto p = x->p.lock();
    if (!p) return;
    auto g = p->p.lock();

    if (p->l == x) {
        // right rotate p
        p->l = x->r;
        if (x->r) x->r->p = p;
        x->r = p;
        p->p = x;
    } else {
        // left rotate p
        p->r = x->l;
        if (x->l) x->l->p = p;
        x->l = p;
        p->p = x;
    }

    x->p = g;
    if (g) {
        if (g->l == p) g->l = x; else if (g->r == p) g->r = x;
    }
    pull(p);
    pull(x);
}

// Splay x to root; returns new root
inline ETTNodePtr splay(ETTNodePtr x) {
    if (!x) return nullptr;
    while (true) {
        auto p = x->p.lock();
        if (!p) break;
        auto g = p->p.lock();
        if (!g) {
            rotate(x);
        } else if ((g->l == p) == (p->l == x)) {
            rotate(p);
            rotate(x);
        } else {
            rotate(x);
            rotate(x);
        }
    }
    return x;
}

// Find rightmost (max) node in tree rooted at t
inline ETTNodePtr find_max(ETTNodePtr t) {
    if (!t) return nullptr;
    while (t->r) t = t->r;
    return t;
}

// Join two trees a and b where all keys in a come before keys in b in Euler order
inline ETTNodePtr joinTrees(ETTNodePtr a, ETTNodePtr b) {
    if (!a) return b;
    if (!b) return a;
    // Find max in a and splay it
    ETTNodePtr maxa = find_max(a);
    maxa = splay(maxa);
    // Attach b as right subtree
    maxa->r = b;
    if (b) b->p = maxa;
    pull(maxa);
    return maxa;
}

// Split tree rooted at 'node' such that 'node' becomes root
// Returns: {left subtree, node (as root with no children), right subtree}
inline tuple<ETTNodePtr, ETTNodePtr, ETTNodePtr> split_at_node(ETTNodePtr node) {
    if (!node) return {nullptr, nullptr, nullptr};
    
    node = splay(node); // node is now root
    ETTNodePtr left = node->l;
    ETTNodePtr right = node->r;
    
    if (left) left->p.reset();
    if (right) right->p.reset();
    node->l.reset();
    node->r.reset();
    pull(node);
    
    return {left, node, right};
}

// collect nodes inorder
inline void collect_nodes(const ETTNodePtr& root, vector<ETTNodePtr>& out) {
    if (!root) return;
    if (root->l) collect_nodes(root->l, out);
    out.push_back(root);
    if (root->r) collect_nodes(root->r, out);
}

class SplayETT {
public:
    // For each vertex id, we keep a pointer to any occurrence node belonging to that vertex
    unordered_map<int, ETTNodePtr> vertex_occurrence;

    struct EdgeOcc { ETTNodePtr uv = nullptr; ETTNodePtr vu = nullptr; };
    map<pair<int,int>, EdgeOcc> edge_map; // normalized (min, max)

public:

    SplayETT() {}

    static pair<int,int> edge_key(int a, int b) {
        return {min(a, b), max(a, b)};
    }

    void make_vertex(int v) {
        if (vertex_occurrence.count(v)) return;
        auto node = make_shared<SplayETTNode>(v, -1, -1);
        vertex_occurrence[v] = node;
    }

    // Remove vertex v completely from ETT
    void remove_vertex(int v) {
        auto it = vertex_occurrence.find(v);
        if (it == vertex_occurrence.end()) return;
        
        // Just remove from map - vertex should be isolated (no edges)
        vertex_occurrence.erase(it);
    }
    
    ETTNodePtr get_any_occurrence(int v) {
        auto it = vertex_occurrence.find(v);
        return it == vertex_occurrence.end() ? nullptr : it->second;
    }


    bool connected(int u, int v) {
        auto ou = get_any_occurrence(u);
        auto ov = get_any_occurrence(v);
        if (!ou || !ov) return false;
        ETTNodePtr ru = get_root(ou);
        ETTNodePtr rv = get_root(ov);
        return ru == rv;
    }

    bool isEdgeExist(int u, int v) {
        return edge_map.find(edge_key(u, v)) != edge_map.end();
    }

    // Link u-v: Standard ETT link operation
    // Split u's tour at u, insert: left(u) + u + (u,v) + tour(v) + (v,u) + right(u)
    void link(int u, int v) {
        if (u == v) return;
        make_vertex(u); 
        make_vertex(v);
        
        if (connected(u, v)) return;

        auto ou = vertex_occurrence[u];
        auto ov = vertex_occurrence[v];

        // Create edge occurrences
        auto uv_node = make_shared<SplayETTNode>(-1, u, v);
        auto vu_node = make_shared<SplayETTNode>(-1, v, u);
        edge_map[edge_key(u, v)] = {uv_node, vu_node};

        // Split u's tour at ou: [left(u), ou, right(u)]
        auto [left_u, ou_isolated, right_u] = split_at_node(ou);

        // Get v's entire tour
        ETTNodePtr tour_v = get_root(ov);

        // Build new tour: left(u) + ou + uv + tour(v) + vu + right(u)
        ETTNodePtr part1 = joinTrees(left_u, ou_isolated);  // left(u) + ou
        ETTNodePtr part2 = joinTrees(part1, uv_node);       // + (u,v)
        ETTNodePtr part3 = joinTrees(part2, tour_v);        // + tour(v)
        ETTNodePtr part4 = joinTrees(part3, vu_node);       // + (v,u)
        ETTNodePtr new_tour = joinTrees(part4, right_u);    // + right(u)

        // Update vertex_occurrence to point to nodes in the new tour
        // ou and ov are already in the tree, so they're still valid
        vertex_occurrence[u] = ou_isolated;
        vertex_occurrence[v] = ov;
    }

    // Cut u-v: Remove edge from Euler tour
    // Find (u,v) and (v,u), remove both, and split into two components
    void cut(int u, int v) {
        if (u == v) return;
        
        auto key = edge_key(u, v);
        auto it = edge_map.find(key);
        if (it == edge_map.end()) return;

        auto uv_node = it->second.uv;
        auto vu_node = it->second.vu;
        if (!uv_node || !vu_node) {
            edge_map.erase(it);
            return;
        }

        // Ensure uv_node corresponds to (u->v) and vu_node corresponds to (v->u).
        // Because we stored nodes under normalized key, the stored uv/vu may not
        // match the orientation of arguments (u,v). Fix by swapping if necessary.
        if (!(uv_node->edge_u == u && uv_node->edge_v == v)) {
            // If uv_node is not matching (u->v), try swapping
            if (vu_node->edge_u == u && vu_node->edge_v == v) {
                std::swap(uv_node, vu_node);
            } else {
                // If neither matches, fall back: try to locate the correct nodes by checking both
                // (This should not normally happen, but be defensive.)
                if (it->second.uv->edge_u == v && it->second.uv->edge_v == u) {
                    // stored uv is actually v->u; swap to align
                    std::swap(uv_node, vu_node);
                }
                // otherwise proceed anyway (best-effort)
            }
        }

        // Split at uv_node: [A, uv_node, B]
        auto [A, uv_isolated, B] = split_at_node(uv_node);

        // Now find vu_node in the remaining sequence A + B
        // vu_node is either in A or in B
        ETTNodePtr combined = joinTrees(A, B);
        
        // Split at vu_node: [C, vu_node, D]
        auto [C, vu_isolated, D] = split_at_node(vu_node);

        // Now we have two separate tours: C and D
        // These represent the two disconnected components
        
        // Update vertex_occurrence for vertices in each component
        // We need to ensure each vertex points to a valid occurrence in its component
        auto update_occurrences = [&](ETTNodePtr root) {
            if (!root) return;
            vector<ETTNodePtr> nodes;
            collect_nodes(root, nodes);
            for (auto& node : nodes) {
                if (node->vertex != -1) {
                    vertex_occurrence[node->vertex] = node;
                }
            }
        };

        update_occurrences(C);
        update_occurrences(D);

        // Clean up isolated edge nodes (safety)
        if (uv_isolated) {
            uv_isolated->l.reset();
            uv_isolated->r.reset();
            uv_isolated->p.reset();
            uv_isolated->sz = 1;
        }
        if (vu_isolated) {
            vu_isolated->l.reset();
            vu_isolated->r.reset();
            vu_isolated->p.reset();
            vu_isolated->sz = 1;
        }

        // Remove edge from map
        edge_map.erase(it);
    }

    // Get distinct vertices in component containing v
    vector<int> get_component_vertices(int v) {
        vector<int> res;
        auto occ = get_any_occurrence(v);
        if (!occ) return res;
        
        ETTNodePtr root = get_root(occ);
        vector<ETTNodePtr> nodes;
        collect_nodes(root, nodes);
        
        unordered_set<int> seen;
        for (auto& node : nodes) {
            if (node->vertex != -1 && seen.insert(node->vertex).second) {
                res.push_back(node->vertex);
            }
        }
        return res;
    }

    // Get component size (number of nodes in Euler tour, not distinct vertices)
    int component_size(int v) {
        return get_component_vertices(v).size();
    }

    // Get component representative (root) for vertex v
    // Returns -1 if vertex doesn't exist
    intptr_t get_component_id(int v) {
        auto occ = get_any_occurrence(v);
        if (!occ) return -1;
        ETTNodePtr root = get_root(occ);
        // Use root pointer address as component ID
        return reinterpret_cast<intptr_t>(root.get());
    }
};