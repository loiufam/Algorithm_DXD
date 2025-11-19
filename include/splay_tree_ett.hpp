#pragma once

#include <algorithm>
#include <unordered_map>

#include "splay_tree.hpp"
#include "hash_pair.hpp"

namespace splay_tree_ett {

class EulerTourTree {
 public:
  EulerTourTree(int _num_verts);
  ~EulerTourTree();


  bool IsConnected(int u, int v);
  void Link(int u, int v);
  void Cut(int u, int v);
  int GetComponentId(int u); // Returns the id of the component containing u.

  bool* BatchConnected(std::pair<int, int>* queries, int len);
  // Inserting all links in [links] must keep the graph acylic.
  void BatchLink(std::pair<int, int>* links, int len);
  // All edges in [cuts] must be in the graph, and no edges may be repeated.
  void BatchCut(std::pair<int, int>* cuts, int len);

 private:
  int num_verts;
  splay_tree::Node* verts;
  std::unordered_map<std::pair<int, int>, splay_tree::Node*, HashIntPairStruct> edges;

  // Reverse mapping: Node pointer -> vertex ID
  std::unordered_map<splay_tree::Node*, int> node_to_vertex;

  std::vector<splay_tree::Node*> allocated_edges; // For memory management
  
  // Helper function to get the representative node of a component
  splay_tree::Node* GetComponentRoot(int u);
};

} //namespace splay_tree_ett
