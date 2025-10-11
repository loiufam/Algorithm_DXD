#include "ConnectedGraph.h"

ConnectedGraph::ConnectedGraph(const DancingMatrix& matrix){

    int rowCount = matrix.ROWS;
    N = rowCount;
    rowHeaderV.reserve(rowCount);
    rowHeaderE.reserve(rowCount);
    for (int i = 0; i < rowCount; ++i) {
        rowHeaderV.push_back(std::make_unique<vertexNode>(-1));
        rowHeaderE.push_back(std::make_unique<vertexNode>(-1));
    }

    // 设置 header 的自循环/自引用
    for (int i = 0; i < rowCount; ++i) {
        vertexNode* vhead = rowHeaderV[i].get();
        vhead->right = vhead;
        vhead->left = vhead;

        vertexNode* ehead = rowHeaderE[i].get();
        ehead->head = ehead;
        ehead->tail = ehead;
        ehead->down = nullptr; // 列链表以 nullptr 结尾
    }


    unordered_map<int, std::vector<int>> vertexToEdges;
    const ColumnHeader* root = matrix.getColumnHeader(0);
    Node* curCol = root->right;
    std::unordered_set<int> record;

    // step1: build mapping from vertex to edges
    while(curCol != root) {

        std::vector<int> globalSequence; // 全局元素序列
        bool isvisited = true;
        Node* curRow = curCol->down;
        while(curRow != curCol) {
            globalSequence.push_back(curRow->row);

            if(!record.count(curRow->row)) {
                isvisited = false;
            }
            curRow = curRow->down;
        }

        if(!isvisited) { 
        
            for(int i = 0; i < globalSequence.size(); ++i) {
                record.insert(globalSequence[i]);
                for(int j = i + 1; j < globalSequence.size(); ++j) {
                    vertexToEdges[globalSequence[i]].push_back(globalSequence[j]);
                }
            }
        }

        curCol = curCol->right;
    }

    // step2: build rowHeaderV and rowHeaderE
    edgePool.reserve(rowCount * 4);

    for(int i = 0; i < rowCount; ++i) {
        vertexNode* curV = rowHeaderV[i].get();

        auto it = vertexToEdges.find(i);
        if (it == vertexToEdges.end() || it->second.empty()) continue;

        for(auto j : it->second) {
            // 创建新边节点并获取裸指针用于链表连接
            auto node_uptr = std::make_unique<vertexNode>(j);
            vertexNode* newE = node_uptr.get();
            // newE->down 默认为 nullptr

            // 插入到行链表（在 header 后插入, 保持双向循环链）
            newE->right = curV->right;
            newE->left = curV;
            curV->right->left = newE;
            curV->right = newE;

            // 插入到列链表（尾插）
            vertexNode* ehead = rowHeaderE[j].get();
            ehead->tail->down = newE; // 当 tail==head 时，会将 head->down 指向 newE
            ehead->tail = newE; // 更新尾指针

            // 托管节点生命周期
            edgePool.push_back(std::move(node_uptr));

            // 将 curV 前移到刚插入的节点，以便下一次插入保持链表正确
            curV = curV->right;
        }
    }

    // cout << "ConnectedGraph has constructed." << endl;
}


// 打印函数
void ConnectedGraph::printGraph() const {
    std::cout << "Graph with " << N << " vertices:" << std::endl;
    for (int i = 0; i < N; ++i) {
        const vertexNode* curV = rowHeaderV[i].get();
        std::cout << "Vertex " << i << ": ";


        const vertexNode* e = curV->right;
        while (e != curV) {
            std::cout << e->value << " ";
            e = e->right;
        }
        std::cout << std::endl;
    }
}

void ConnectedGraph::remove(int i) {

    vertexNode* curHead = rowHeaderE[i].get();
    if (curHead->tail == curHead) {
        return; // 没有边，直接返回
    }
    // lock_guard<mutex> lock(graphMutex);

    vertexNode* curE = curHead->down;
    while(curE != nullptr) { 
        curE->left->right = curE->right;
        curE->right->left = curE->left;

        curE = curE->down;
    }

}

void ConnectedGraph::restore(int i) {

    // lock_guard<mutex> lock(graphMutex);
    vertexNode* curHead = rowHeaderE[i].get();

    vertexNode* curE = curHead->down;
    while(curE != nullptr) { 
        curE->left->right = curE;
        curE->right->left = curE;

        curE = curE->down;
    }
}

// TODO: 优化返回Block，无须继续计算列集合
vector<vector<int>> ConnectedGraph::getComponents(const set<int>& existRowSet)  { 

    vector<vector<int>> components;

    unordered_set<int> visited;
    unordered_map<int, int> rowToComponent;

    int curIndex = 0;

    for(auto i : existRowSet) { 

        if(!visited.count(i)) {
            components.push_back({i});
            visited.insert(i);
            rowToComponent[i] = curIndex++;
        }

        vertexNode* curHead = rowHeaderV[i].get();
        if (curHead->right != curHead) {
            vertexNode* cur = curHead->right;

            int index = rowToComponent[i];
            while(cur != curHead) { 
                if (!visited.count(cur->value) && existRowSet.count(cur->value)) {
                    components[index].push_back(cur->value);
                    visited.insert(cur->value);
                    rowToComponent[cur->value] = index;
                }
                cur = cur->right;
            }
        }

        if (visited.size() == existRowSet.size()) {
            break; // 所有节点都已访问，提前退出
        }

    }

    return components;

}

// 利用缓存优化
vector<Component> ConnectedGraph::getComponents() const{
    if (componentsCacheValid) {
        return cachedComponents;
    }

    cachedComponents.clear();
    rootToComponentIndex.clear();
    nodeToComponent.assign(N, -1);


    if((int)mark.size() < N) 
        mark.assign(N,0); 
    else 
        fill(mark.begin(), mark.end(), 0);

    for(int v = 0; v < N; ++v){ 
        if(!nodeActive[v] || mark[v]) continue;  // 跳过不活跃的节点

        Treap* Rroot = rootOf(etf.enter[v]);
        if(!Rroot) continue;

        // 检查是否已经处理过这个根
        if(rootToComponentIndex.count(Rroot)) {
            mark[v] = 1;
            continue;
        }
        
        Component comp; 
        comp.rows.reserve(max(4, sz(Rroot)/4)); 
        comp.cols.reserve(max(4, sz(Rroot)/4));

        Treap* cur = Rroot;
        trav_stack.clear();
        trav_stack.reserve(max((size_t)trav_stack.capacity(), 
            (size_t)min(sz(Rroot), (int)1e6)));

        vector<int> nodesInComp; // 记录当前分量的所有节点
        
        while(cur || !trav_stack.empty()){
            while(cur){ 
                trav_stack.push_back(cur); 
                cur = cur->l; 
            }
            
            cur = trav_stack.back(); 
            trav_stack.pop_back();

            if(cur->v >= 0 && !mark[cur->v] && nodeActive[cur->v]){
                mark[cur->v] = 1;
                nodesInComp.push_back(cur->v);
                if(cur->v < R) 
                    comp.rows.push_back(cur->v);
                else 
                    comp.cols.push_back(cur->v - R + 1);
            }
            cur = cur->r;
        }

        if(!comp.rows.empty() && !comp.cols.empty()) {
            int compIdx = cachedComponents.size();
            cachedComponents.push_back(std::move(comp));
            rootToComponentIndex[Rroot] = compIdx;
            
            // 记录每个节点属于哪个分量
            for(int node : nodesInComp) {
                nodeToComponent[node] = compIdx;
            }
        }
    }

    componentsCacheValid = true;
    return cachedComponents;
}

bool ConnectedGraph::inSameComponent(int u, int v) {
    if(!nodeActive[u] || !nodeActive[v]) return false;
    return etf.connected(u, v);
}

// ===== 获取指定节点的连通分量（不重新计算所有分量）=====
Component ConnectedGraph::getComponentOf(int node) const {
    
    Component comp;
    if(!nodeActive[node]) return comp;
    
    Treap* Rroot = rootOf(etf.enter[node]);
    if(!Rroot) return comp;
    
    comp.rows.reserve(max(4, sz(Rroot)/4));
    comp.cols.reserve(max(4, sz(Rroot)/4));
    
    vector<char> localMark(N, 0);
    Treap* cur = Rroot;
    vector<Treap*> stack;
    
    while(cur || !stack.empty()){
        while(cur){ stack.push_back(cur); cur = cur->l; }
        cur = stack.back(); 
        stack.pop_back();
        
        if(cur->v >= 0 && !localMark[cur->v] && nodeActive[cur->v]){
            localMark[cur->v] = 1;
            if(cur->v < R) 
                comp.rows.push_back(cur->v);
            else 
                comp.cols.push_back(cur->v - R + 1);
        }
        cur = cur->r;
    }
    
    return comp;
}