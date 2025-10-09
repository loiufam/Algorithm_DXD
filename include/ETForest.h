#include <unordered_set>
#include <vector>
#include <utility> 

using namespace std;

struct Treap {
    int v; // >=0 for enter marker, <0 for exit marker (~id)
    int pr;
    Treap *l, *r, *p;
    int sz;
    Treap(int _v): v(_v), pr(rand()), l(nullptr), r(nullptr), p(nullptr), sz(1) {}
};

inline int sz(Treap* t) {
     return t ? t->sz : 0; 
}

inline void pull(Treap* t) { 
    if (t) { 
        t->sz = 1 + sz(t->l) + sz(t->r); 
        if(t->l) t->l->p = t; 
        if(t->r) t->r->p = t; 
    }
}


inline Treap* mergeTreap(Treap* a, Treap* b) {
    if(!a) return b; 
    if(!b) return a;
    if(a->pr < b->pr){ 
        a->r = mergeTreap(a->r, b); pull(a); 
        a->p=nullptr; return a; 
    }else { 
        b->l = mergeTreap(a, b->l); 
        pull(b); 
        b->p=nullptr; 
        return b; 
    }
}

inline pair<Treap*, Treap*> splitTreap(Treap* t, int k) {
    if(!t) 
        return {nullptr, nullptr};
    if(sz(t->l) >= k){
        auto pr = splitTreap(t->l, k);
        t->l = pr.second; 
        pull(t); 
        if(t->l) 
            t->l->p=t; 
        if(pr.first) 
            pr.first->p=nullptr; 
        return {pr.first, t};
    } else {
        auto pr = splitTreap(t->r, k - sz(t->l) - 1);
        t->r = pr.first; 
        pull(t); 
        if(t->r) 
            t->r->p=t; 
        if(pr.second) 
            pr.second->p=nullptr; 
        return {t, pr.second};
    }
}


inline int indexInTreap(Treap* x){
    int idx = sz(x->l);
    while(x->p){ 
        if(x == x->p->r) 
            idx += 1 + sz(x->p->l); 
        x = x->p; 
    }
    return idx;
}


inline Treap* rootOf(Treap* x){ 
    while(x && x->p) 
        x = x->p; 
    return x; 
}

// -------------------- Euler Tour Forest --------------------
struct ETForest {
    int N;
    vector<Treap*> enter, exit;
    ETForest(int n=0): N(n), enter(n,nullptr), exit(n,nullptr){
        for(int i=0; i<n; ++i){ 
            enter[i] = new Treap(i); 
            exit[i] = new Treap(~i); 
        }
    }

    ~ETForest() {
        unordered_set<Treap*> roots;
        for(auto t: enter) if(t) roots.insert(rootOf(t));
        for(auto t: exit) if(t) roots.insert(rootOf(t));
        for(auto root : roots){ 
            if(!root) continue; 
            vector<pair<Treap*,bool>> st; 
            st.emplace_back(root,false); 
            while(!st.empty()){ 
                auto pr = st.back(); 
                st.pop_back(); 
                Treap* node = pr.first; 
                bool visited = pr.second; 
                if(!node) continue; 
                if(visited){ 
                    delete node; 
                } else { 
                    st.emplace_back(node,true); 
                    if(node->r) st.emplace_back(node->r,false); 
                    if(node->l) st.emplace_back(node->l,false); 
                } 
            } 
        }    
    }

    bool connected(int a,int b){ 
        return rootOf(enter[a])==rootOf(enter[b]); 
    }

    void link(int a,int b){
        if(connected(a,b)) return;

        Treap* A = rootOf(enter[a]); 
        Treap* B = rootOf(enter[b]);
        int ia = indexInTreap(enter[a]); 
        auto pa = splitTreap(A,ia);
        Treap* Arot = mergeTreap(pa.second,pa.first);
        int ib = indexInTreap(enter[b]); 
        auto pb = splitTreap(B,ib);
        Treap* Brot = mergeTreap(pb.second,pb.first);
        Treap* merged = mergeTreap(Arot,Brot);
        if(merged) 
            merged->p=nullptr;
    }

    void cut(int a,int b){
        if(!connected(a,b)) return;

        Treap* R = rootOf(enter[a]);
        int ia = indexInTreap(enter[a]);
        int ib = indexInTreap(enter[b]); 
        if(ia>ib) swap(ia,ib);
        auto p1 = splitTreap(R,ia); 
        auto p2 = splitTreap(p1.second,ib-ia+1);
        Treap* left = mergeTreap(p1.first,p2.second); 
        Treap* right = p2.first;
        if(left) left->p = nullptr; 
        if(right) right->p = nullptr;
    }

    vector<int> component_nodes(int v){
        Treap* R = rootOf(enter[v]); 
        vector<int> res;
        if(!R) return res;
        res.reserve(sz(R)/2 + 1); // 估计大小，减少扩容


        // iterative inorder traversal
        Treap* cur = R;
        vector<Treap*> stack;
        while(cur || !stack.empty()){
            while(cur){ stack.push_back(cur); cur = cur->l; }
            cur = stack.back(); 
            stack.pop_back();
            if(cur->v >= 0) 
                res.push_back(cur->v);
            cur = cur->r;
        }


        // remove duplicates (Euler tour contains multiple enter markers)
        unordered_set<int> st(res.begin(), res.end());
        vector<int> out; 
        out.reserve(st.size());
        for(int x: st) out.push_back(x);
        return out;
    }

};