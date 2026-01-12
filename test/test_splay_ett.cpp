#include "../include/SplayETT.h"
#include <queue>
#include <iostream>
#include <vector>

using namespace std;

/**
 * 从邻接矩阵构建无向图的邻接表
 */
vector<vector<int>> buildAdjListFromMatrix(const vector<vector<int>>& mat) {
    int n = mat.size();
    vector<vector<int>> adj(n);
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            if (mat[i][j]) {
                adj[i].push_back(j);
                adj[j].push_back(i);
            }
        }
    }
    return adj;
}

/**
 * BFS 获取生成树边
 */
vector<pair<int,int>> bfsSpanningTree(const vector<vector<int>>& adj, int root = 0) {
    int n = adj.size();
    vector<bool> vis(n, false);
    queue<int> q;
    vector<pair<int,int>> treeEdges;

    vis[root] = true;
    q.push(root);

    while (!q.empty()) {
        int u = q.front(); q.pop();
        for (int v : adj[u]) {
            if (!vis[v]) {
                vis[v] = true;
                q.push(v);
                treeEdges.emplace_back(u, v);
            }
        }
    }
    return treeEdges;
}


/**
 * 主测试函数
 */
int main() {
    /*
     * 示例图（邻接矩阵）
     *
     *   0 —— 1 —— 3
     *   |    |
     *   2 —— 4
     */
    vector<vector<int>> matrix = {
        {0,1,1,0,0},
        {1,0,0,1,1},
        {1,0,0,0,1},
        {0,1,0,0,0},
        {0,1,1,0,0}
    };

    // 1. 构建邻接表
    auto adj = buildAdjListFromMatrix(matrix);

    // 2. BFS 生成树
    auto treeEdges = bfsSpanningTree(adj, 0);

    cout << "BFS Spanning Tree Edges:\n";
    for (auto [u, v] : treeEdges) {
        cout << u << " - " << v << "\n";
    }
    cout << "\n";

    // 3. 构建 ETT
    SplayETT ett(adj.size());
    for (auto [u, v] : treeEdges) {
        ett.link(u, v);
    }

    // 4. 打印初始化欧拉回路（说明性输出）
    cout << "After building ETT:\n";
    cout << "Component ID of 0: " << ett.getComponentId(0) << "\n";
    cout << "Component size: " << ett.componentSize(0) << "\n\n";
    cout << "Euler Tour of component containing 0: ";
    ett.debugPrintEuler(0);

    // 5. cut 一条生成树边
    cout << "Cut edge (1, 3)\n";
    ett.cut(1, 3);

    // 6. 再次打印
    cout << "\nAfter cut:\n";
    cout << "Component ID of 0: " << ett.getComponentId(0) << "\n";
    cout << "Component size: " << ett.componentSize(0) << "\n";
    cout << "Component ID of 3: " << ett.getComponentId(3) << "\n";
    cout << "Component size: " << ett.componentSize(3) << "\n\n";
    cout << "Euler Tour of component containing 0: ";
    ett.debugPrintEuler(0);
    cout << "Euler Tour of component containing 3: ";   
    ett.debugPrintEuler(3);

    return 0;
}
