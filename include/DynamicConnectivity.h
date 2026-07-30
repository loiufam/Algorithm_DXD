#ifndef DYNAMIC_CONNECTIVITY_H
#define DYNAMIC_CONNECTIVITY_H

#pragma once

#include <shared_mutex>
#include "Block.h"
#include "TreapETT.h"
#include "SplayTree.h"
#include <algorithm>
#include <cassert>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// 邻接表节点（支持O(1)删除和恢复）
struct AdjNode {
    int neighbor;
    bool deleted;
    AdjNode* next;
    
    AdjNode(int v) : neighbor(v), deleted(false), next(nullptr) {}
};

struct Vertex {
    bool     deleted = false;
    int      compId  = -1;    // 路由键：所属连通分量 ID
    AdjNode  sentinel{-1};    // 邻接表哨兵头（不存储真实数据）
};

class Graph;

class SubGraph {
public:
    SubGraph(Graph* parent, int compId, std::vector<int> vids)
        : parent_(parent), compId_(compId), vertexIds_(std::move(vids)) {}

    // ── 元信息 ──────────────────────────────
    int compId()  const { return compId_; }
    int size()    const { return (int)vertexIds_.size(); }
    const std::vector<int>& vertices() const { return vertexIds_; }

    // ── 无锁边/顶点操作（委托给父图内存）──
    void addEdge    (int u, int v);
    void deleteEdge (int u, int v);
    void restoreEdge(int u, int v);
    bool hasEdge    (int u, int v) const;
    void deleteVertex(int v);
    void restoreVertex(int v);
    bool hasVertex  (int v)        const;

    // neighbors：返回活跃边的邻居
    std::vector<int> neighbors(int v) const;

    std::vector<int> getAllNeighbors(int v) const;

    void print() const;

private:
    Graph*           parent_;
    int              compId_;
    std::vector<int> vertexIds_;
};

// Graph —— 主图（持有所有顶点和邻接表内存）
class Graph {
public:
    explicit Graph(int n) : vertices_(n), vertexComp_(n, -1) {}

    ~Graph() {
        for (auto& v : vertices_) {
            AdjNode* cur = v.sentinel.next;
            while (cur) { AdjNode* nxt = cur->next; delete cur; cur = nxt; }
        }
    }
    
    // ── 顶点操作 ────────────────────────────
    void addVertex    (int v) { chk(v); vertices_[v].deleted = false; }
    void deleteVertex (int v) { chk(v); vertices_[v].deleted = true;  }
    void restoreVertex(int v) { chk(v); vertices_[v].deleted = false; }
    bool hasVertex    (int v) const { chk(v); return !vertices_[v].deleted; }

    // ── 边操作 ──────────────────────────────
    void addEdge    (int u, int v) { insertDir(u,v); insertDir(v,u); }
    // Initialization fast path.  The caller must guarantee that the
    // undirected edge is new; unlike addEdge(), this avoids a linear adjacency
    // scan before each insertion.
    void addUniqueEdge(int u, int v) {
        insertUniqueDir(u, v);
        insertUniqueDir(v, u);
    }
    void deleteEdge (int u, int v) { markDir(u,v,true);  markDir(v,u,true);  }
    void restoreEdge(int u, int v) { markDir(u,v,false); markDir(v,u,false); }
    bool hasEdge    (int u, int v) const {
        AdjNode* n = findNode(u,v); return n && !n->deleted;
    }

    std::vector<int> neighbors(int v) const {
        chk(v);
        std::vector<int> res;
        for (AdjNode* c = vertices_[v].sentinel.next; c; c = c->next)
            if (!c->deleted && !vertices_[c->neighbor].deleted)
                res.push_back(c->neighbor);
        return res;
    }

    int numVertices()   const { return (int)vertices_.size(); }
    int numComponents() const { return (int)subgraphs_.size(); }

    void registerComponent(int compId,
                           const std::unordered_set<int>& comp_vertices)
    {
        std::vector<int> vids(comp_vertices.begin(), comp_vertices.end());
        for (int v : vids) {
            vertexComp_[v]      = compId;
            vertices_[v].compId = compId;
        }
        subgraphs_[compId] = std::make_unique<SubGraph>(
            this, compId, std::move(vids));
    }

    SubGraph* subgraphOf(int v) const {
        chk(v);
        int cid = vertexComp_[v];
        if (cid < 0) return nullptr;
        auto it = subgraphs_.find(cid);
        return it == subgraphs_.end() ? nullptr : it->second.get();
    }

    SubGraph* subgraphById(int cid) const {
        auto it = subgraphs_.find(cid);
        return it == subgraphs_.end() ? nullptr : it->second.get();
    }

    void forEachSubgraph(const std::function<void(int, SubGraph*)>& fn) const {
        for (auto& [cid, sg] : subgraphs_) fn(cid, sg.get());
    }

    Vertex&       vertex(int v)       { return vertices_[v]; }
    const Vertex& vertex(int v) const { return vertices_[v]; }

    AdjNode* findNode(int u, int v) const {
        if (u < 0 || u >= (int)vertices_.size()) {
            fprintf(stderr, "[ERROR] findNode: u=%d, size=%zu\n", u, vertices_.size());
            abort();
        }
        for (AdjNode* c = vertices_[u].sentinel.next; c; c = c->next)
            if (c->neighbor == v) return c;
        return nullptr;
    }
    
    std::vector<int> getNeighbors(int v) const;
    std::vector<int> getAllNeighbors(int v) const;
    int getDegree(int v) const;

    void printGraph() const {
        std::cout << "── Graph "
            << " [" << numVertices() << " verts / "
            << numComponents() << " comps] ──────\n";
        for (int i = 0; i < numVertices(); ++i) {
            const auto& vt = vertices_[i];
            std::cout << "  v" << std::setw(2) << i
                      << (vt.deleted ? "[D]" : "   ")
                      << " c" << std::setw(2) << vt.compId << " │ ";
            
            for (AdjNode* c = vt.sentinel.next; c; c = c->next) {
                std::cout << c->neighbor;
                if (c->deleted) std::cout << "✗";
                std::cout << " ";
            }
            std::cout << "\n";
        }
        std::cout << "Components:\n";
        for (auto& [cid, sg] : subgraphs_) sg->print();
    }

private:
    std::vector<AdjNode*> adjList;
    std::unordered_map<int, std::unordered_map<int, AdjNode*>> edgeMap;

    std::vector<Vertex>   vertices_;
    std::vector<int>      vertexComp_;
    std::unordered_map<int, std::unique_ptr<SubGraph>> subgraphs_;

    void chk(int v) const { assert(v >= 0 && v < (int)vertices_.size()); }

    void insertDir(int u, int v) {
        AdjNode* node = findNode(u, v);
        if (node) { node->deleted = false; return; }
        AdjNode* n = new AdjNode(v);
        n->next    = vertices_[u].sentinel.next;
        vertices_[u].sentinel.next = n;
    }
    void insertUniqueDir(int u, int v) {
        chk(u);
        chk(v);
        AdjNode* n = new AdjNode(v);
        n->next = vertices_[u].sentinel.next;
        vertices_[u].sentinel.next = n;
    }
    void markDir(int u, int v, bool del) {
        AdjNode* node = findNode(u, v);
        if (node) node->deleted = del;
    }
};


inline void SubGraph::addEdge(int u, int v) { parent_->addEdge(u, v); }
inline void SubGraph::deleteEdge(int u, int v) { parent_->deleteEdge(u, v); }
inline void SubGraph::restoreEdge(int u, int v) { parent_->restoreEdge(u, v); }
inline bool SubGraph::hasEdge(int u, int v) const { return parent_->hasEdge(u, v); }
inline void SubGraph::deleteVertex(int v) { parent_->deleteVertex(v); }
inline void SubGraph::restoreVertex(int v) { parent_->restoreVertex(v); }
inline bool SubGraph::hasVertex(int v) const { return parent_->hasVertex(v); }

inline std::vector<int> SubGraph::neighbors(int v) const {
    return parent_->getNeighbors(v);
}

inline std::vector<int> SubGraph::getAllNeighbors(int v) const {
    return parent_->getAllNeighbors(v);
}

inline void SubGraph::print() const {
    std::cout << "  SubGraph[" << compId_ << "] (" << size() << "v): ";
    for (int v : vertexIds_) {
        std::cout << "v" << v << "(";
        bool first = true;
        for (AdjNode* c = parent_->vertex(v).sentinel.next; c; c = c->next) {
            if (!c->deleted && parent_->vertex(c->neighbor).compId == compId_) {
                if (!first) std::cout << ",";
                std::cout << c->neighbor;
                first = false;
            }
        }
        std::cout << ") ";
    }
    std::cout << "\n";
}

inline std::vector<int> Graph::getNeighbors(int v) const {
    std::vector<int> res;
    for (AdjNode* c = vertices_[v].sentinel.next; c; c = c->next)
        if (!c->deleted && !vertices_[c->neighbor].deleted)
            res.push_back(c->neighbor);
    return res;
}

inline std::vector<int> Graph::getAllNeighbors(int v) const {
    std::vector<int> res;
    for (AdjNode* c = vertices_[v].sentinel.next; c; c = c->next)
        res.push_back(c->neighbor);
    return res;
}

inline int Graph::getDegree(int v) const {
    return getNeighbors(v).size();
}

#endif // DYNAMIC_CONNECTIVITY_H
