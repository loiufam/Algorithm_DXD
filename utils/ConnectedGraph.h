#ifndef CONNECTED_GRAPH_H
#define CONNECTED_GRAPH_H

#include "DancingMatrix.h"
#include "ETForest.h"

struct vertexNode
{
    int value;  // 节点值
    vertexNode* left, *right; // 双向链表指针
    vertexNode* down, *head, *tail;  // 边链表指针
    
    vertexNode() {
        left = NULL; right = NULL;
        down = NULL;
        head = NULL; tail = NULL;
    }

    vertexNode(int v) : value(v) {
        left = NULL; right = NULL;
        down = NULL;
        head = NULL; tail = NULL;
    }
};

struct MatrixComponent{
    vector<int> rows;
    vector<int> cols;

    MatrixComponent() = default;
};

class UnionFindWithColumns {
private:
    std::vector<int> parent;
    std::vector<int> rank;
    // 每个连通分量的代表元素维护列集合
    std::vector<std::unordered_set<int>> columnSets;
    int numComponents;
    
public:
    UnionFindWithColumns(int n) : parent(n), rank(n, 0), columnSets(n), numComponents(n) {
        for (int i = 0; i < n; ++i) {
            parent[i] = i;
        }
    }
    
    int find(int x) {
        if (parent[x] != x) {
            parent[x] = find(parent[x]);
        }
        return parent[x];
    }
    
    // 优化的合并：同时合并列集合
    bool unite(int x, int y) {
        int rootX = find(x);
        int rootY = find(y);
        
        if (rootX == rootY) return false;
        
        // 总是将小集合合并到大集合
        if (rank[rootX] < rank[rootY]) {
            std::swap(rootX, rootY);
        }
        
        parent[rootY] = rootX;
        
        if (rank[rootX] == rank[rootY]) {
            rank[rootX]++;
        }
        
        // **合并列集合**
        if (columnSets[rootY].size() > columnSets[rootX].size()) {
            std::swap(columnSets[rootX], columnSets[rootY]);
        }
        columnSets[rootX].insert(columnSets[rootY].begin(), columnSets[rootY].end());
        columnSets[rootY].clear(); // 释放内存
        
        numComponents--;
        return true;
    }
    
    // 添加列到行
    void addColumn(int row, int col) {
        int root = find(row);
        columnSets[root].insert(col);
    }
    
    int getNumComponents() const {
        return numComponents;
    }

    bool connected(int x, int y) {
        return find(x) == find(y);
    } 

    // 获取连通分量的列集合
    const std::unordered_set<int>& getColumns(int row) {
        return columnSets[find(row)];
    }
    
    // 获取所有连通分量及其列集合
    std::vector<std::pair<std::vector<int>, std::unordered_set<int>>> getComponents() {
        std::unordered_map<int, std::vector<int>> components;
        
        for (int i = 0; i < parent.size(); ++i) {
            int root = find(i);
            components[root].push_back(i);
        }
        
        std::vector<std::pair<std::vector<int>, std::unordered_set<int>>> result;
        for (auto& [root, rows] : components) {
            result.emplace_back(std::move(rows), columnSets[root]);
        }
        
        return result;
    }
};

class ConnectedGraph {
    
    public:
        int R, C, N;
        ETForest etf;
        unordered_map<uint64_t, int> edgeCount;
        unordered_map<uint64_t, bool> isTreeEdge;
        vector<unordered_set<int>> incidentNonTree;
        mutable vector<char> mark; // 复用的标记数组
        mutable vector<Treap*> trav_stack; // reusable traversal stack to avoid allocations

        // ===== 新增：连通分量缓存机制 =====
        mutable vector<MatrixComponent> cachedComponents;
        mutable bool componentsCacheValid;
        mutable unordered_map<Treap*, int> rootToComponentIndex; // root -> component index
        mutable vector<int> nodeToComponent; // node -> component index

        ConnectedGraph (const DancingMatrix& matrix);
        // ConnectedGraph(int rows, int cols)
        //     : R(rows), C(cols), N(rows+cols), etf(rows+cols){
        //     incidentNonTree.assign(N,{});
        //     mark.assign(N,0);
        //     nodeActive.assign(N, true);  // 初始时所有节点都是活跃的
        //     trav_stack.reserve(1024); // initial reserve
        //     componentsCacheValid = false;
        //     nodeToComponent.assign(N, -1);
        // }
        ~ConnectedGraph() = default;

        // 打印函数
        void printGraph() const;

        // 标记行节点为不活跃
        void deactivateRow(int row) {
            if (row >= 0 && row < R) {
                nodeActive[row] = false;
                invalidateComponentCache();
            }
        }
        
        // 恢复行节点为活跃
        void activateRow(int row) {
            if (row >= 0 && row < R) {
                nodeActive[row] = true;
                invalidateComponentCache();
            }
        }
        
        // 检查节点是否活跃
        bool isNodeActive(int node) const {
            return node >= 0 && node < N && nodeActive[node];
        }

        std::mutex graphMutex; // 保护图操作的互斥锁
        void remove(int i);
        void restore(int i);
        static inline uint64_t key(int a, int b){ 
            if(a > b) swap(a,b); 
            return ((uint64_t)a<<32)|(uint32_t)b; 
        }


        void insertEdge(int r, int c){
            int u = r, v = R+c;
            uint64_t k = key(u,v);
            edgeCount[k]++; 
            if(edgeCount[k] > 1) return;

            if(!etf.connected(u,v)){ 
                etf.link(u,v); 
                isTreeEdge[k]=true; 
                mergeComponentsOnLink(u,v);
            }else{ 
                incidentNonTree[u].insert(v); 
                incidentNonTree[v].insert(u); 
                isTreeEdge[k]=false; 
            }
        }

        void removeEdge(int r, int c){
            int u = r, v = R + c; 
            uint64_t k = key(u,v);
            auto it = edgeCount.find(k); 
            if(it == edgeCount.end()) return;
            if(--it->second> 0) return; 

            edgeCount.erase(it);
            bool wasTree = isTreeEdge[k]; 
            isTreeEdge.erase(k);

            if(!wasTree){ 
                incidentNonTree[u].erase(v); 
                incidentNonTree[v].erase(u); 
                return; 
            }
            etf.cut(u,v);
            // 分裂连通分量
            splitComponentsOnCut(u, v);

            vector<int> compU = etf.component_nodes(u); 
            vector<int> compV = etf.component_nodes(v);
            unordered_set<int> setU(compU.begin(),compU.end());
            vector<int> scanned = (setU.size() <= compV.size() ? compU : compV);
            
            for(int x : scanned){ 
                for(int y : incidentNonTree[x]) {
                    bool inU = setU.count(y);
                    if((setU.size() <= compV.size() && !inU)
                        || (setU.size() > compV.size() && inU)){
                        uint64_t rk = key(x,y);
                        incidentNonTree[x].erase(y); 
                        incidentNonTree[y].erase(x);
                        isTreeEdge[rk]=true; 
                        etf.link(x,y); 
                        // 重新合并
                        mergeComponentsOnLink(x, y);
                        return; 
                    }
                }
            }
        }

        vector<vector<int>> getComponents(const set<int>& existRowSet);

        vector<MatrixComponent> getComponents() const;

        // ===== 快速检查两个节点是否在同一连通分量 =====
        bool inSameComponent(int u, int v);
        
        // ===== 获取节点所在的连通分量 =====
        int getComponentIndex(int node) const {
            if(!componentsCacheValid) {
                getComponents(); // 触发缓存构建
            }
            return (node >= 0 && node < N) ? nodeToComponent[node] : -1;
        }

        MatrixComponent getComponentOf(int node) const;

    private:
        std::vector<std::unique_ptr<vertexNode>> rowHeaderV;
        std::vector<std::unique_ptr<vertexNode>> rowHeaderE;
        std::vector<std::unique_ptr<vertexNode>> edgePool; // 用于存放所有边节点
        vector<bool> nodeActive;  // 跟踪节点是否活跃

        // ===== 缓存失效管理 =====
        void invalidateComponentCache() const {
            componentsCacheValid = false;
        }

        // ===== 增量更新：link时合并分量 =====
        void mergeComponentsOnLink(int u, int v) {
            if(!componentsCacheValid) return; // 缓存已失效，无需更新
            
            int compU = nodeToComponent[u];
            int compV = nodeToComponent[v];
            
            if(compU == -1 || compV == -1 || compU == compV) {
                invalidateComponentCache();
                return;
            }
            
            // 合并两个分量：将compV合并到compU
            MatrixComponent& targetComp = cachedComponents[compU];
            MatrixComponent& sourceComp = cachedComponents[compV];
            
            // targetComp.rows.insert(targetComp.rows.end(), 
            //     sourceComp.rows.begin(), sourceComp.rows.end());
            // targetComp.cols.insert(targetComp.cols.end(), 
            //     sourceComp.cols.begin(), sourceComp.cols.end());
            
            // 更新节点映射
            for(int row : sourceComp.rows) {
                if(row < R) nodeToComponent[row] = compU;
            }
            for(int col : sourceComp.cols) {
                nodeToComponent[R + col - 1] = compU;
            }
            
            // 标记sourceComp为空（延迟删除）
            sourceComp.rows.clear();
            sourceComp.cols.clear();
        }

        // ===== 增量更新：cut时分裂分量 =====
        void splitComponentsOnCut(int u, int v) {
            // cut操作较复杂，直接失效缓存
            invalidateComponentCache();
        }
};

#endif