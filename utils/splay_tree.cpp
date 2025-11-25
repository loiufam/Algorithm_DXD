#include "../include/splay_tree.hpp"
#include <cassert>
#include <utility>
#include <unordered_set>
#include <iostream>

using std::pair;

namespace splay_tree {

Node::Node(Node* _parent, Node* left, Node* right)
  : parent(_parent) {
  child[0] = left;
  child[1] = right;
  if (left) left->parent = this;
  if (right) right->parent = this;
}

Node::Node() : Node(nullptr, nullptr, nullptr) {}

void Node::AssignChild(int i, Node* v) {
  child[i] = v;
  if (v) v->parent = this;
}

void Node::Rotate() {
  Node* p = parent;
  assert(p != nullptr);
  Node* g = p->parent;

  // determine whether this is left(0) or right(1) child of p
  int which = (p->child[1] == this);
  int other = !which;

  // detach this->child[other] and attach to p
  p->AssignChild(which, child[other]);

  // attach p as child[other] of this
  this->AssignChild(other, p);

  // fix parent pointers
  this->parent = g;
  if (g != nullptr) {
    if (g->child[0] == p) g->child[0] = this;
    else if (g->child[1] == p) g->child[1] = this;
  }
}

void Node::Splay() {

  while (parent != nullptr) {
    if (parent->child[0] != this && parent->child[1] != this) {
      std::cerr << "Error: node is not child of its parent" << std::endl;
      parent = nullptr;
      break;
    }
    Node* p = parent;
    Node* g = p->parent;
    if (g == nullptr) {
      // Zig
      Rotate();
    } else {
      // Zig-zig or zig-zag
      bool p_is_left = (g->child[0] == p);
      bool this_is_left = (p->child[0] == this);
      if (p_is_left == this_is_left) {
        // zig-zig: rotate parent first
        p->Rotate();
        Rotate();
      } else {
        // zig-zag
        Rotate();
        Rotate();
      }
    }
  }
}

Node* Node::GetExtreme(int i) {
  Splay();
  Node* cur = this;
  while (cur->child[i] != nullptr)
    cur = cur->child[i];
  return cur;
}

Node* Node::GetMin() {
  return GetExtreme(0);
}

Node* Node::GetMax() {
  return GetExtreme(1);
}

// In ETT we will use a representative node: the min in its splay tree
Node* Node::GetRep() {
  return GetMin();
}

Node* Node::GetSuccessor() {
  Splay();
  if (child[1] == nullptr) return nullptr;
  Node* cur = child[1];
  while (cur->child[0] != nullptr) cur = cur->child[0];
  cur->Splay(); // cur is the min in the right subtree
  return cur;
}

// Splits right after this node: returns (left_tree_root, right_tree_root)
pair<Node*, Node*> Node::Split() {
  Splay();
  Node* right = child[1];
  if (right) right->parent = nullptr;
  child[1] = nullptr;
  // 'this' is root of left tree
  return std::make_pair(this, right);
}

// Join: all keys in lesser must come before keys in greater in the inorder.
// Returns new root (max of lesser becomes root).
Node* Node::Join(Node* lesser, Node* greater) {
  if (lesser == nullptr) return greater;
  if (greater == nullptr) return lesser;
  // bring max of lesser to root
  Node* lesser_max = lesser->GetMax();
  // now lesser_max has no right child
  lesser_max->AssignChild(1, greater);
  return lesser_max;
}

void Node::Append(Node* new_node) {
  Node* maxn = GetMax();
  maxn->AssignChild(1, new_node);
}

Node* Node::DeleteExtreme(int i) {
  Node* to_delete = GetExtreme(i);
  Node* tree_pointer = to_delete->child[!i];
  if (tree_pointer != nullptr)
    tree_pointer->parent = nullptr;

  to_delete->child[0] = to_delete->child[1] = nullptr;
  to_delete->parent = nullptr;

  return tree_pointer;
}

Node* Node::DeleteMin() { return DeleteExtreme(0); }
Node* Node::DeleteMax() { return DeleteExtreme(1); }

} // namespacesplay_tree
