#include <memory>
#include <unordered_map>
#include <functional>
#include <cassert>

using namespace std;

struct SplayETTNode;
using ETTNodePtr = shared_ptr<SplayETTNode>;

struct SplayETTNode {
    ETTNodePtr l = nullptr, r = nullptr;
    weak_ptr<SplayETTNode> p;
    int vertex;      // vertex id (-1 for edge-occurrence)
    int edge_u, edge_v;  // edge endpoints for edge-occurrence
    int sz = 1;
    
    SplayETTNode(int vertex_ = -1, int u = -1, int v = -1)
        : vertex(vertex_), edge_u(u), edge_v(v), sz(1) {}
    
    ETTNodePtr parent() const { return p.lock(); }
    
    void update_size() {
        sz = 1;
        if (l) sz += l->sz;
        if (r) sz += r->sz;
    }
};

class SplayETT {
private:
    // Mapping from vertex id to its first occurrence node in the ETT
    unordered_map<int, ETTNodePtr> vertex_map;
    
    // Mapping from edge (u,v) to its occurrence node  
    unordered_map<long long, ETTNodePtr> edge_map;
    
    static constexpr long long MAX_VERTEX = 1000000LL;
    
    long long edge_key(int u, int v) const {
        if (u > v) swap(u, v);
        return (long long)u * MAX_VERTEX + v;
    }
    
    // Splay operations
    void set_child(ETTNodePtr p, ETTNodePtr ch, bool is_left) {
        if (p) {
            if (is_left) p->l = ch;
            else p->r = ch;
        }
        if (ch) ch->p = p;
    }
    
    void rotate(ETTNodePtr x) {
        if (!x) return;
        ETTNodePtr p = x->parent();
        if (!p) return;
        
        ETTNodePtr g = p->parent();
        bool x_is_left = (p->l == x);
        
        set_child(p, x_is_left ? x->r : x->l, x_is_left);
        set_child(x, p, !x_is_left);
        set_child(g, x, g && g->l == p);
        
        p->update_size();
        x->update_size();
    }
    
    void splay(ETTNodePtr x) {
        if (!x) return;
        
        while (x->parent()) {
            ETTNodePtr p = x->parent();
            ETTNodePtr g = p->parent();
            
            if (!g) {
                rotate(x);
            } else {
                bool x_is_left = (p->l == x);
                bool p_is_left = (g->l == p);
                
                if (x_is_left == p_is_left) {
                    rotate(p);
                    rotate(x);
                } else {
                    rotate(x);
                    rotate(x);
                }
            }
        }
    }
    
    // Get the root of the tree containing node x
    ETTNodePtr get_root(ETTNodePtr x) {
        if (!x) return nullptr;
        while (x->parent()) x = x->parent();
        return x;
    }
    
    // Join two trees: all elements in left < all elements in right
    ETTNodePtr join(ETTNodePtr left, ETTNodePtr right) {
        if (!left) return right;
        if (!right) return left;
        
        // Splay the rightmost node in left tree
        ETTNodePtr curr = left;
        while (curr->r) curr = curr->r;
        splay(curr);
        
        // Now curr is root with no right child
        curr->r = right;
        right->p = curr;
        curr->update_size();
        
        return curr;
    }
    
    // Find vertex occurrence in tree by DFS
    ETTNodePtr find_vertex_dfs(ETTNodePtr node, int v) {
        if (!node) return nullptr;
        
        ETTNodePtr left_result = find_vertex_dfs(node->l, v);
        if (left_result) return left_result;
        
        if (node->vertex == v) return node;
        
        return find_vertex_dfs(node->r, v);
    }
    
    // Find edge occurrence in tree by DFS
    ETTNodePtr find_edge_dfs(ETTNodePtr node, int u, int v) {
        if (!node) return nullptr;
        
        ETTNodePtr left_result = find_edge_dfs(node->l, u, v);
        if (left_result) return left_result;
        
        if (node->vertex == -1 && node->edge_u == u && node->edge_v == v) {
            return node;
        }
        
        return find_edge_dfs(node->r, u, v);
    }

public:
    SplayETT() = default;
    
    // Create a new isolated vertex
    void make_vertex(int v) {
        if (vertex_map.count(v)) return;
        
        auto node = make_shared<SplayETTNode>(v);
        vertex_map[v] = node;
    }
    
    // Remove vertex and all its incident edges
    void remove_vertex(int v) {
        auto it = vertex_map.find(v);
        if (it == vertex_map.end()) return;
        
        ETTNodePtr v_node = it->second;
        
        if (!v_node->parent() && !v_node->l && !v_node->r) {
            vertex_map.erase(it);
            return;
        }
        
        // Remove from tree structure
        splay(v_node);
        ETTNodePtr new_root = join(v_node->l, v_node->r);
        
        if (v_node->l) v_node->l->p.reset();
        if (v_node->r) v_node->r->p.reset();
        
        vertex_map.erase(it);
    }
    
    // Check if two vertices are in the same connected component
    bool connected(int u, int v) {
        auto u_it = vertex_map.find(u);
        auto v_it = vertex_map.find(v);
        
        if (u_it == vertex_map.end() || v_it == vertex_map.end()) {
            return false;
        }
        
        ETTNodePtr u_root = get_root(u_it->second);
        ETTNodePtr v_root = get_root(v_it->second);
        
        return u_root == v_root;
    }
    
    // Link two vertices with an edge
    void link(int u, int v) {
        auto u_it = vertex_map.find(u);
        auto v_it = vertex_map.find(v);
        
        if (u_it == vertex_map.end() || v_it == vertex_map.end()) {
            return;
        }
        
        long long key = edge_key(u, v);
        if (edge_map.count(key)) return;
        
        ETTNodePtr u_node = u_it->second;
        ETTNodePtr v_node = v_it->second;
        
        // Create edge occurrences
        auto edge_uv = make_shared<SplayETTNode>(-1, u, v);
        auto edge_vu = make_shared<SplayETTNode>(-1, v, u);
        
        edge_map[key] = edge_uv;
        
        // Make both nodes roots of their trees
        splay(u_node);
        splay(v_node);
        
        // Build: Tu + (u->v) + Tv + (v->u)
        // This creates Euler tour: u ... u, u->v, v ... v, v->u, (back to u)
        ETTNodePtr tree1 = join(u_node, edge_uv);
        ETTNodePtr tree2 = join(tree1, v_node);
        ETTNodePtr final_tree = join(tree2, edge_vu);
    }
    
    // Cut the edge between u and v
    void cut(int u, int v) {
        long long key = edge_key(u, v);
        auto edge_it = edge_map.find(key);
        
        if (edge_it == edge_map.end()) return;
        
        ETTNodePtr edge_uv = edge_it->second;
        
        // Find root and locate both edge occurrences
        ETTNodePtr root = get_root(edge_uv);
        ETTNodePtr edge_vu = find_edge_dfs(root, v, u);
        
        if (!edge_vu) return;
        
        // Splay edge_uv to root
        splay(edge_uv);
        
        // Tree is now:  [A] edge_uv [B + edge_vu + C]
        ETTNodePtr A = edge_uv->l;
        ETTNodePtr BvuC = edge_uv->r;
        
        // Detach A
        if (A) A->p.reset();
        if (BvuC) BvuC->p.reset();
        
        // Find edge_vu in BvuC
        edge_vu = find_edge_dfs(BvuC, v, u);
        if (!edge_vu) {
            // edge_vu must be in A
            edge_vu = find_edge_dfs(A, v, u);
            if (!edge_vu) return;
            
            // Splay edge_vu in A
            splay(edge_vu);
            
            // Now: [A1] edge_vu [A2]
            ETTNodePtr A1 = edge_vu->l;
            ETTNodePtr A2 = edge_vu->r;
            
            if (A1) A1->p.reset();
            if (A2) A2->p.reset();
            
            // Reconnect: A1 + BvuC and A2
            join(A1, BvuC);
            
        } else {
            // edge_vu is in BvuC
            // Splay it
            splay(edge_vu);
            
            // Now: [B] edge_vu [C]
            ETTNodePtr B = edge_vu->l;
            ETTNodePtr C = edge_vu->r;
            
            if (B) B->p.reset();
            if (C) C->p.reset();
            
            // Reconnect: A + C and B
            join(A, C);
        }
        
        edge_map.erase(edge_it);
    }
    
    // Get component representative for vertex v
    intptr_t get_component_id(int v) {
        auto it = vertex_map.find(v);
        if (it == vertex_map.end()) return -1;
        
        ETTNodePtr root = get_root(it->second);
        return find_first_vertex(root);
    }

private:
    // Find the first vertex id in the Euler tour
    int find_first_vertex(ETTNodePtr root) {
        if (!root) return -1;
        
        if (root->l) {
            int left_v = find_first_vertex(root->l);
            if (left_v != -1) return left_v;
        }
        
        if (root->vertex != -1) return root->vertex;
        
        if (root->r) {
            return find_first_vertex(root->r);
        }
        
        return -1;
    }
    
};