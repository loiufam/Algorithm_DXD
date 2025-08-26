#ifndef CONNECTED_GRAPH_H
#define CONNECTED_GRAPH_H

#include "DancingMatrix.h"
using namespace std;

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

class ConnectedGraph {
    
    public:
        int N; // 图的节点数

        ConnectedGraph (const DancingMatrix& matrix);
        ~ConnectedGraph();
        bool prune() {
            return false;
        };

        void remove(int i);
        void restore(int i);
        vector<vector<int>> getComponents(set<int> existRowSet);

    
    private:
        vertexNode* rowHeaderV;
        vertexNode* rowHeaderE;

};

#endif