#ifndef ETT_H
#define ETT_H

#pragma once

// portable replacement for GCC-only <bits/stdc++.h>
#include <algorithm>
#include <deque>
#include <iostream>
#include <numeric>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
using namespace std;

struct ETTUnionFind {
    int num_elements;
    vector<int> parent;
    vector<int> rank;
    
    ETTUnionFind() : num_elements(0) {}
    explicit ETTUnionFind(int n) { initialize(n); }
    
    void initialize(int n) {
        num_elements = n;
        parent.resize(n);
        rank.assign(n, 0);
        iota(parent.begin(), parent.end(), 0);
    }
    
    int find_root(int node) {
        if (parent[node] != node) {
            parent[node] = find_root(parent[node]);  // path compression
        }
        return parent[node];
    }
    
    bool unite(int node_a, int node_b) {
        int root_a = find_root(node_a);
        int root_b = find_root(node_b);
        
        if (root_a == root_b) return false;
        
        // union by rank
        if (rank[root_a] < rank[root_b]) swap(root_a, root_b);
        parent[root_b] = root_a;
        if (rank[root_a] == rank[root_b]) ++rank[root_a];
        
        return true;
    }
    
    bool is_connected(int node_a, int node_b) {
        return find_root(node_a) == find_root(node_b);
    }
};

// Undirected graph where nodes represent matrix rows
struct Graph {
    int num_nodes;
    unordered_map<int, unordered_set<int>> adjacency_list;
    
    explicit Graph(int n) { initialize(n); }
    
    void initialize(int n) {
        cout << "Initializing graph with " << n << " nodes." << endl;
        num_nodes = n;
        adjacency_list.reserve(n);
    }
    
    // Add undirected edge between two nodes (ignores self-loops)
    void add_edge(int node_u, int node_v) {
        if (node_u == node_v) return;
        if (node_u < 0 || node_u >= num_nodes || 
            node_v < 0 || node_v >= num_nodes) {
            return;  // 跳过无效边
        }
        adjacency_list[node_u].insert(node_v);
        adjacency_list[node_v].insert(node_u);
    }
    
    // Remove duplicate edges and optionally sort adjacency lists
    // void normalize() {
    //     for (int i = 0; i < num_nodes; ++i) {
    //         auto& neighbors = adjacency_list[i];
    //         sort(neighbors.begin(), neighbors.end());
    //         neighbors.erase(unique(neighbors.begin(), neighbors.end()), neighbors.end());
    //     }
    // }

    void print() const {
        cout << "Graph adjacency list:" << endl;
        for (int i = 0; i < num_nodes; ++i) {
            cout << i << ": { ";
            for (int neighbor : adjacency_list.at(i)) {
                cout << neighbor << " ";
            }
            cout << "}" << endl;
        }
    }
};

// ================================ Static Euler Tour Tree ================================
// Static ETT: builds spanning forest and Euler tours for macro-component identification
// Note: This is NOT a dynamic ETT with link/cut operations
struct StaticEulerTourTree {;
    int num_nodes;
    
    vector<int> tree_id;           // tree_id[v] = ID of spanning tree containing v
    vector<int> parent_node;       // parent in spanning forest (-1 for roots)
    vector<int> first_visit_index; // index of first occurrence in Euler tour
    vector<int> euler_tour;        // concatenated Euler tours of all trees
    vector<int> tree_roots;        // list of root nodes (one per tree)
    
    StaticEulerTourTree() : num_nodes(0) {}
    
    // Build spanning forest and construct Euler tours
    void build(unordered_map<int, unordered_set<int>> adj, int num_nodes_) {
        num_nodes = num_nodes_;
        
        tree_id.assign(num_nodes, -1);
        parent_node.assign(num_nodes, -1);
        first_visit_index.assign(num_nodes, -1);
        euler_tour.clear();
        tree_roots.clear();
        
        vector<bool> visited(num_nodes, false);
        int current_tree_id = 0;
        
        // Build spanning forest using BFS for each connected component
        for (int start_node = 0; start_node < num_nodes; ++start_node) {
            if (visited[start_node]) continue;
            
            build_spanning_tree(start_node, current_tree_id, visited, adj);
            ++current_tree_id;
        }
    }
    
    // Get tree ID (macro component) for a given node
    int get_tree_id(int node) const {
        return (node >= 0 && node < num_nodes) ? tree_id[node] : -1;
    }
    
private:
    void build_spanning_tree(int root, int tree_idx, vector<bool>& visited, unordered_map<int, unordered_set<int>> adj) {
        // BFS to build spanning tree
        deque<int> queue;
        queue.push_back(root);
        visited[root] = true;
        parent_node[root] = -1;
        tree_roots.push_back(root);
        
        vector<int> tree_nodes;
        unordered_map<int, vector<int>> children;
        
        while (!queue.empty()) {
            int current = queue.front();
            queue.pop_front();
            tree_nodes.push_back(current);
            tree_id[current] = tree_idx;
            
            for (int neighbor : adj.at(current)) {
                if (!visited[neighbor]) {
                    visited[neighbor] = true;
                    parent_node[neighbor] = current;
                    children[current].push_back(neighbor);
                    queue.push_back(neighbor);
                }
            }
        }
        
        // Build Euler tour for this spanning tree
        build_euler_tour(root, children);
    }
    
    void build_euler_tour(int root, const unordered_map<int, vector<int>>& children) {
        // Iterative DFS to generate Euler tour
        // Each node appears multiple times: once on entry and once on exit
        vector<pair<int, int>> stack;  // pair(node, next_child_index)
        
        stack.emplace_back(root, 0);
        first_visit_index[root] = static_cast<int>(euler_tour.size());
        euler_tour.push_back(root);
        
        while (!stack.empty()) {
            int node = stack.back().first;
            int& child_idx = stack.back().second;
            
            auto it = children.find(node);
            const vector<int>& node_children = (it != children.end()) ? it->second : vector<int>();
            
            if (child_idx < static_cast<int>(node_children.size())) {
                int child = node_children[child_idx++];
                first_visit_index[child] = static_cast<int>(euler_tour.size());
                euler_tour.push_back(child);
                stack.emplace_back(child, 0);
            } else {
                // Exit current node
                euler_tour.push_back(node);
                stack.pop_back();
            }
        }
    }
};

#endif // ETT_H