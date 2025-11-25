#include "../include/splay_tree_ett.hpp"

namespace splay_tree_ett {

using Node = splay_tree::Node;
using std::pair;

EulerTourTree::EulerTourTree(int _num_verts) : num_verts(_num_verts) {
  verts = new Node[num_verts];
  // edges.reserve(2 * (num_verts - 1));
  // Initialize reverse mapping
  for (int i = 0; i < num_verts; i++) {
    node_to_vertex[&verts[i]] = i;
  }
}

EulerTourTree::~EulerTourTree() {
  delete[] verts;
  edges.clear();
  node_to_vertex.clear();
}

bool EulerTourTree::IsConnected(int u, int v) {
  if (u == v) return true;

  if (u < 0 || u >= num_verts || v < 0 || v >= num_verts) return false;

  // If two vertices share the same splay tree representative, they are connected.
  Node* ru = verts[u].GetRep();
  Node* rv = verts[v].GetRep();
  return ru == rv;
}

void EulerTourTree::Link(int u, int v) {
  if (u == v) return; // ignoring self-loop
  if (u < 0 || u >= num_verts || v < 0 || v >= num_verts) return;

  if (IsConnected(u, v)) {
    return;
  }

  // 创建 unique_ptr 管理的新节点
  auto uv = std::make_unique<Node>();
  auto vu = std::make_unique<Node>();
  // allocated_edges.push_back(uv);
  // allocated_edges.push_back(vu);

  // 获取裸指针用于后续操作
  Node* uv_ptr = uv.get();
  Node* vu_ptr = vu.get();

  edges[make_pair(u, v)] = std::move(uv);
  edges[make_pair(v, u)] = std::move(vu);

  // Split after u and after v
  Node* U_left; Node* U_right;
  tie(U_left, U_right) = verts[u].Split(); // splits right after verts[u], left contains upto u

  Node* V_left; Node* V_right;
  tie(V_left, V_right) = verts[v].Split();

  // 合并序列：
  // 原 U 序列: ... u ... -> [ ... u ] (U_left) 和 [ ... ] (U_right)
  // 原 V 序列: ... v ... -> [ ... v ] (V_left) 和 [ ... ] (V_right)
  // 目标序列: (U左 ... u) -> (u,v) -> (V右 ...) -> (V左 ... v) -> (v,u) -> (U右 ...)

  // temp1 = Join(U_left, uv)
  Node* temp1 = Node::Join(U_left, uv_ptr);
  // temp2 = Join(temp1, V_right)
  Node* temp2 = Node::Join(temp1, V_right);
  // temp3 = Join(temp2, V_left)
  Node* temp3 = Node::Join(temp2, V_left);
  // temp4 = Join(temp3, vu)
  Node* temp4 = Node::Join(temp3, vu_ptr);
  // final = Join(temp4, U_right)
  Node* final_root = Node::Join(temp4, U_right);

}

Node* EulerTourTree::RemoveNode(Node* node) {
  if (!node) return nullptr;
  
  // Splay 该节点到根
  node->Splay();
  
  // 现在 node 是根，移除它并合并左右子树
  Node* left = node->child[0];
  Node* right = node->child[1];
  
  // 断开连接
  node->child[0] = nullptr;
  node->child[1] = nullptr;
  node->parent = nullptr;
  
  if (left) left->parent = nullptr;
  if (right) right->parent = nullptr;
  
  // 合并左右子树
  return Node::Join(left, right);
}

void EulerTourTree::Cut(int u, int v) {
  if (u == v) return;
  if (u < 0 || u >= num_verts || v < 0 || v >= num_verts) return;

  auto it_uv = edges.find(make_pair(u, v));
  auto it_vu = edges.find(make_pair(v, u));
  if (it_uv == edges.end() || it_vu == edges.end()) {
    return;
  }

  std::unique_ptr<Node> uv_holder = std::move(it_uv->second);
  std::unique_ptr<Node> vu_holder = std::move(it_vu->second);
  
  // 从 map 中删除
  edges.erase(it_uv);
  edges.erase(it_vu);
  
  Node* uv = uv_holder.get();
  Node* vu = vu_holder.get();
  
  // ===== 标准 ETT Cut 算法 =====
  
  // 步骤 1: 移除边节点 uv
  // Splay uv 到根，然后移除它
  uv->Splay();
  Node* left1 = uv->child[0];   // uv 左边的序列
  Node* right1 = uv->child[1];  // uv 右边的序列
  
  uv->child[0] = nullptr;
  uv->child[1] = nullptr;
  uv->parent = nullptr;
  
  if (left1) left1->parent = nullptr;
  if (right1) right1->parent = nullptr;
  
  // 步骤 2: 在 right1 中找到并移除 vu
  // 注意：vu 一定在 right1 中（因为 Link 时的顺序）
  if (right1) {
    // 在 right1 子树中 Splay vu
    vu->Splay();
    
    Node* left2 = vu->child[0];   // vu 左边的序列
    Node* right2 = vu->child[1];  // vu 右边的序列
    
    vu->child[0] = nullptr;
    vu->child[1] = nullptr;
    vu->parent = nullptr;
    
    if (left2) left2->parent = nullptr;
    if (right2) right2->parent = nullptr;
    
    // 步骤 3: 重组两个连通分量
    // 原始序列是：left1 | uv | left2 | vu | right2
    // 移除 uv 和 vu 后，需要连接：
    // - 组件1：left1 + right2
    // - 组件2：left2（独立）
    
    Node::Join(left1, right2);
    // left2 已经是独立的树了
  } else {
    // 如果 right1 为空，说明 vu 可能在 left1 中（不应该发生）
    // 或者树结构异常
    // 这种情况下，尝试在 left1 中找 vu
    if (left1) {
      vu->Splay();
      
      Node* left2 = vu->child[0];
      Node* right2 = vu->child[1];
      
      vu->child[0] = nullptr;
      vu->child[1] = nullptr;
      vu->parent = nullptr;
      
      if (left2) left2->parent = nullptr;
      if (right2) right2->parent = nullptr;
      
      Node::Join(left2, right2);
    }
  }
  
  // uv_holder 和 vu_holder 在函数结束时自动释放
}

int EulerTourTree::GetComponentId(int u) {
  if (u < 0 || u >= num_verts) return -1;
  Node* root = GetComponentRoot(u);
  if (!root) return -1;
  auto it = node_to_vertex.find(root);
  if (it != node_to_vertex.end()) return it->second;
  // As a fallback, try splaying the root and mapping to some vertex in tree:
  // (shouldn't be needed in correct usage)
  return -1;
}

splay_tree::Node* EulerTourTree::GetComponentRoot(int u) {
  // We use the minimum node in the splay tree that contains verts[u] as canonical root
  return verts[u].GetRep();
}

bool* EulerTourTree::BatchConnected(pair<int, int>* queries, int len) {
  bool* ans = new bool[len];
  for (int i = 0; i < len; i++) {
    ans[i] = IsConnected(queries[i].first, queries[i].second);
  }
  return ans;
}

void EulerTourTree::BatchLink(pair<int, int>* links, int len) {
  for (int i = 0; i < len; i++) {
    Link(links[i].first, links[i].second);
  }
}

void EulerTourTree::BatchCut(pair<int, int>* cuts, int len) {
  for (int i = 0; i < len; i++) {
    Cut(cuts[i].first, cuts[i].second);
  }
}

} // namespace splay_tree_ett
