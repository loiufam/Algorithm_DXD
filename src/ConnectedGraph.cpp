#include "ConnectedGraph.h"

ConnectedGraph::ConnectedGraph(const DancingMatrix& matrix){

    int rowCount = matrix.ROWS;
    N = rowCount;
    rowHeaderV = new vertexNode[rowCount];  // 图节点数组
    rowHeaderE = new vertexNode[rowCount];  // 边节点数组

    for(int i = 0; i < rowCount; ++i) {
        // 对图头节点进行初始化
        vertexNode* curV = &rowHeaderV[i];
        curV->right = curV;
        curV->left = curV;

        vertexNode* curE = &rowHeaderE[i];
        curE->head = curE;
        curE->tail = curE;
    }

    unordered_map<int, set<int>> vertexToEdges;
    const ColunmHeader* root = matrix.getRoot();
    Node* curCol = root->right;
    unordered_set<int> record;
    // step1: build mapping from vertex to edges
    while(curCol != root) {

        vector<int> globalSequence; // 全局元素序列
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
                set<int> subSequence;
                for(int j = i+1; j < globalSequence.size(); ++j) {
                    subSequence.insert(globalSequence[j]);
                }
                vertexToEdges[globalSequence[i]].insert(subSequence.begin(), subSequence.end());
            }
        }

        curCol = curCol->right;
    }

    // step2: build rowHeaderV and rowHeaderE
    for(int i = 0; i < rowCount; ++i) {
        vertexNode* curV = &rowHeaderV[i];

        if(vertexToEdges[i].empty()) {
            continue;
        }

        for(auto j : vertexToEdges[i]) {
            vertexNode* newE = new vertexNode(j);
            newE->right = curV->right;
            newE->left = curV;
            curV->right->left = newE;
            curV->right = newE;

            rowHeaderE[j].tail->down = newE;
            rowHeaderE[j].tail = newE;

            curV = curV->right;
        }
    }

    cout << "ConnectedGraph has constructed." << endl;
}

// 析构函数
ConnectedGraph::~ConnectedGraph() {

    for(int i = 0; i < N; ++i) {
        vertexNode* curV = &rowHeaderV[i];
        vertexNode* curE = curV->right;
        while(curE != curV) {
            vertexNode* nextE = curE->right;
            delete curE;
            curE = nextE;
        }
    }
    delete[] rowHeaderV;
    delete[] rowHeaderE;
}

// ConnectedGraph::~ConnectedGraph() {
//     // ETForest 的析构函数会自动释放 Treap 节点
//     edgeCount.clear();
//     isTreeEdge.clear();
//     incidentNonTree.clear();
//     mark.clear();
//     nodeActive.clear();
//     trav_stack.clear();
// }

void ConnectedGraph::remove(int i) {

    vertexNode* curHead = &rowHeaderE[i];
    if (curHead->tail == curHead) {
        return; // 没有边，直接返回
    }
    // lock_guard<mutex> lock(graphMutex);

    vertexNode* curE = curHead->down;
    while(curE != NULL) { 
        curE->left->right = curE->right;
        curE->right->left = curE->left;

        curE = curE->down;
    }

}

void ConnectedGraph::restore(int i) {

    // lock_guard<mutex> lock(graphMutex);
    vertexNode* curHead = &rowHeaderE[i];

    vertexNode* curE = curHead->down;
    while(curE != NULL) { 
        curE->left->right = curE;
        curE->right->left = curE;

        curE = curE->down;
    }
}

vector<vector<int>> ConnectedGraph::getComponents(set<int> existRowSet) { 

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

        vertexNode* curHead = &rowHeaderV[i];
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