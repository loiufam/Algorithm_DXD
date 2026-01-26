#include "../include/SplayTree.h"
#include <iostream>
#include <queue>

namespace splaytree {

// 构造和析构
EulerTourTree::EulerTourTree(int id) : root(nullptr), treeId(id) {}

EulerTourTree::~EulerTourTree() {
    deleteTree(root);
}

void EulerTourTree::deleteTree(Node* x) {
    if (!x) return;
    deleteTree(x->left);
    deleteTree(x->right);
    delete x;
}

// Splay树基本操作
void EulerTourTree::updateSize(Node* x) {
    if (!x) return;
    x->size = 1;
    if (x->left) x->size += x->left->size;
    if (x->right) x->size += x->right->size;
}

void EulerTourTree::rotate(Node* n) {
    Node* p = n->parent;
    if (!p) return;

    Node* g = p->parent;

    if (n == p->left) {
        // Right rotation
        p->left = n->right;
        if (n->right) {
            n->right->parent = p;
        }
        n->right = p;
    } else {
        // Left rotation
        p->right = n->left;
        if (n->left) {
            n->left->parent = p;
        }
        n->left = p;
    }

    p->parent = n;
    n->parent = g;

    if (g) {
        if (p == g->left) g->left = n;
        else g->right = n;
    } else {
        root = n;
    }

    updateSize(p);
    updateSize(n);
}

// Splay操作：将节点n旋转到根位置
void EulerTourTree::splay(Node* n) {
    if (!n) return;

    while (n->parent) {
        Node* p = n->parent;
        Node* g = p->parent;

        if (g) {
            if ((n == p->left && p == g->left) ||
                 (n == p->right && p == g->right)) {
                rotate(p);
            } else {
                rotate(n);
            }
        }
        rotate(n);
    }
    root = n;
}

Node* EulerTourTree::findMax(Node* x) {
    if (!x) return nullptr;
    while (x->right) x = x->right;
    splay(x);
    return x;
}

Node* EulerTourTree::findMin(Node* x) {
    if (!x) return nullptr;
    while (x->left) x = x->left;
    splay(x);
    return x;
}

Node* EulerTourTree::findRoot(Node* x) {
    if (!x) return nullptr;

    while (x->parent) x = x->parent;
    return x;
}

Node* EulerTourTree::split(Node* x) {
    if (!x) return nullptr;
    splay(x);

    Node* rightTree = x->right;
    if (rightTree) {
        rightTree->parent = nullptr;
        x->right = nullptr;
        updateSize(x);
    }
    return rightTree;
}

// Euler Tour Cancatenate
Node* EulerTourTree::join(Node* leftTree, Node* rightTree) {
    if (!leftTree) {
        return rightTree;
    }
    if (!rightTree) {
        return leftTree;
    }

    Node* maxNode = findMax(leftTree);

    maxNode->right = rightTree;
    rightTree->parent = maxNode;
    updateSize(maxNode);
    return maxNode;
}

Node* EulerTourTree::getRepresentative(int u) {
    // 优先返回自环 u->u，如果没有，返回任意一个出边
    if (edgeNodes.count(u)) {
        if (edgeNodes[u].count(u)) return edgeNodes[u][u];
        if (!edgeNodes[u].empty()) return edgeNodes[u].begin()->second;
    }
    return nullptr;
}

void EulerTourTree::collectNodes(Node* x, std::vector<Node*>& nodes) const {
    if (!x) return;
    collectNodes(x->left, nodes);
    nodes.push_back(x);
    collectNodes(x->right, nodes);
}

Node* EulerTourTree::buildFromNodes(std::vector<Node*>& nodes, int start, int end) {
    if (start > end) return nullptr;
    int mid = (start + end) / 2;
    Node* node = nodes[mid];
    node->left = buildFromNodes(nodes, start, mid - 1);
    node->right = buildFromNodes(nodes, mid + 1, end);
    node->parent = nullptr;
    if (node->left) node->left->parent = node;
    if (node->right) node->right->parent = node;
    updateSize(node);
    return node;
}

// 添加单个顶点
void EulerTourTree::addVertex(int v) {
    if (vertices.count(v)) return;
    
    vertices.insert(v);
    Node* node = new Node(v);
    // vertexOccurrences[v].push_back(node);
    edgeNodes[v][v] = node;
    
    if (!root) {
        root = node;
    } 
    // else {
    //     Node* last = findMax(root);
    //     splay(last);
    //     last->right = node;
    //     node->parent = last;
    //     updateSize(last);
    // }
}

void EulerTourTree::reroot(int u) {
    Node* node = getRepresentative(u);
    if (!node) return; // u 不在本树中

    splay(node);
    // 此时树结构： [L] node [R]
    // 欧拉序： ... L ... node ... R ...
    // 目标序： node ... R ... L ... 
    
    Node* leftPart = node->left;
    if (leftPart) {
        leftPart->parent = nullptr;
        node->left = nullptr;
        updateSize(node);
        
        Node* rightEnd = findMax(node);
        
        // 将 leftPart 连接到最右端
        rightEnd->right = leftPart;
        leftPart->parent = rightEnd;
        updateSize(rightEnd);
        
        // 更新 root
        root = findRoot(node);
    }
}

// Link操作：连接两棵不同树
void EulerTourTree::link(int u, int v, EulerTourTree* otherTree) {
    if (!otherTree || otherTree == this) return;
    if (!hasVertex(u) || !otherTree->hasVertex(v)) return;
    
    reroot(u);
    otherTree->reroot(v);
    
    Node* edgeUV = new Node(u, v);
    Node* edgeVU = new Node(v, u);
    
    edgeNodes[u][v] = edgeUV;
    edgeNodes[v][u] = edgeVU;
    
    // 合并: root -> edgeUV -> otherTree->root -> edgeVU
    // root = join(root, edgeUV);
    // root = join(root, otherTree->root);
    // root = join(root, edgeVU);

    // 找到 u 树的最右节点
    Node* uMax = findMax(root);
    // 连接 edgeUV
    uMax->right = edgeUV;
    edgeUV->parent = uMax;
    updateSize(uMax);
    
    // 连接 v 树
    edgeUV->right = otherTree->root;
    otherTree->root->parent = edgeUV;
    updateSize(edgeUV);
    
    // 找到 v 树的最右节点
    Node* vMax = findMax(otherTree->root);
    // 连接 edgeVU
    vMax->right = edgeVU;
    edgeVU->parent = vMax;
    updateSize(vMax);
    
    // 更新根节点到整个序列的根
    root = findRoot(root);
    
    // 合并数据
    vertices.insert(otherTree->vertices.begin(), otherTree->vertices.end());
    nonTreeEdges.insert(otherTree->nonTreeEdges.begin(), otherTree->nonTreeEdges.end());
    
    for (auto& [vertex, edges] : otherTree->edgeNodes) {
        for (auto& [neighbor, node] : edges) {
            edgeNodes[vertex][neighbor] = node;
        }
    }
    
    otherTree->root = nullptr;
    otherTree->vertices.clear();
    otherTree->nonTreeEdges.clear();
    otherTree->edgeNodes.clear();
}

Node* EulerTourTree::findEdgeNode(Node* subtree, int u, int v) {
    if (!subtree) return nullptr;
    
    // 使用队列进行BFS查找
    std::queue<Node*> q;
    q.push(subtree);
    
    while (!q.empty()) {
        Node* current = q.front();
        q.pop();
        
        // 检查当前节点是否是目标边节点
        if (current->isEdge() && current->u == u && current->v == v) {
            return current;
        }
        
        if (current->left) q.push(current->left);
        if (current->right) q.push(current->right);
    }
    
    return nullptr;
}

// 更高效的版本：使用DFS
Node* EulerTourTree::findEdgeNodeDFS(Node* subtree, int u, int v) {
    if (!subtree) return nullptr;
    
    // 先检查当前节点
    if (subtree->isEdge() && subtree->u == u && subtree->v == v) {
        return subtree;
    }
    
    // 递归查找左子树
    Node* leftResult = findEdgeNodeDFS(subtree->left, u, v);
    if (leftResult) return leftResult;
    
    // 递归查找右子树
    return findEdgeNodeDFS(subtree->right, u, v);
}

// Cut操作：删除边u-v
std::pair<std::unique_ptr<EulerTourTree>, std::unique_ptr<EulerTourTree>> EulerTourTree::cut(int u, int v, int newTreeId1, int newTreeId2) {
    // 标准化边
    if (u > v) std::swap(u, v);
    Edge edge(u, v);
    
    Node* edgeUV = nullptr;
    Node* edgeVU = nullptr;
    
    if (edgeNodes.count(u) && edgeNodes[u].count(v)) {
        edgeUV = edgeNodes[u][v];
    }
    if (edgeNodes.count(v) && edgeNodes[v].count(u)) {
        edgeVU = edgeNodes[v][u];
    }
    
    // 边不存在，返回空指针表示失败
    if (!edgeUV || !edgeVU) {
        std::cerr << "Error: Edge (" << u << ", " << v << ") not found in tree.\n";
        return {nullptr, nullptr};
    }

    // 收集所有节点，按欧拉序列顺序
    std::vector<Node*> allNodes;
    collectNodes(root, allNodes);

    Node* firstEdge = nullptr;
    Node* secondEdge = nullptr;

    bool foundFirst = false;
    for (Node* node : allNodes) {
        if (node == edgeUV) {
            if (!foundFirst) {
                firstEdge = edgeUV;
                secondEdge = edgeVU;
                foundFirst = true;
            } else {
                break;  // 已经确定顺序
            }
        } else if (node == edgeVU) {
            if (!foundFirst) {
                firstEdge = edgeVU;
                secondEdge = edgeUV;
                foundFirst = true;
            } else {
                break;  // 已经确定顺序
            }
        }
    }
    
    splay(firstEdge);
    
    Node* leftPart = firstEdge->left;
    Node* rightPart = firstEdge->right;
    
    // 断开连接
    if (leftPart) leftPart->parent = nullptr;
    if (rightPart) rightPart->parent = nullptr;
    
    // Node* edgeVUNode = findEdgeNodeDFS(rightPart, v, u);
    
    // if (!edgeVUNode) {
    //     // 数据结构不一致，这不应该发生
    //     std::cerr << "Error: Reverse edge (" << v << ", " << u << ") not found in expected subtree.\n";
    //     std::cerr << "Data structure may be corrupted.\n";
        
    //     // 尝试恢复原结构
    //     edgeUV->left = leftPart;
    //     edgeUV->right = rightPart;
    //     if (leftPart) leftPart->parent = edgeUV;
    //     if (rightPart) rightPart->parent = edgeUV;
    //     updateSize(edgeUV);
    //     root = edgeUV;
        
    //     return {nullptr, nullptr};
    // }
    
    Node* oldRoot = root;
    root = rightPart;  // 临时设置root以便splay工作

    splay(secondEdge);

    rightPart = root;  // 更新rightPart为新的根
    root = oldRoot;    // 恢复原root
    
    // 此时 rightPart 结构: [middle] edgeVU [right2]
    Node* middlePart = rightPart->left;
    Node* right2Part = rightPart->right;
    
    if (middlePart) middlePart->parent = nullptr;
    if (right2Part) right2Part->parent = nullptr;
    
    delete firstEdge;
    delete secondEdge;
    
    edgeNodes[u].erase(v);
    edgeNodes[v].erase(u);
    if (edgeNodes[u].empty()) edgeNodes.erase(u);
    if (edgeNodes[v].empty()) edgeNodes.erase(v);
    
    Node* tree1Root = join(leftPart, right2Part);
    Node* tree2Root = middlePart;
    
    // 步骤6: 创建两棵新的 EulerTourTree 对象
    auto newTree1 = std::make_unique<EulerTourTree>(newTreeId1);
    auto newTree2 = std::make_unique<EulerTourTree>(newTreeId2);
    
    newTree1->root = tree1Root;
    newTree2->root = tree2Root;

    std::vector<Node*> nodes1, nodes2;
    collectNodes(tree1Root, nodes1);
    collectNodes(tree2Root, nodes2);
    
    // 分配顶点
    std::unordered_set<int> verts1, verts2;
    for (Node* node : nodes1) {
        verts1.insert(node->u);
        if (node->isEdge()) verts1.insert(node->v);
    }
    for (Node* node : nodes2) {
        verts2.insert(node->u);
        if (node->isEdge()) verts2.insert(node->v);
    }
    
    newTree1->vertices = verts1;
    newTree2->vertices = verts2;

    for (Node* node : nodes1) {
        if (node->isEdge()) {
            newTree1->edgeNodes[node->u][node->v] = node;
        } else {
            newTree1->edgeNodes[node->u][node->u] = node;
        }
    }
    
    for (Node* node : nodes2) {
        if (node->isEdge()) {
            newTree2->edgeNodes[node->u][node->v] = node;
        } else {
            newTree2->edgeNodes[node->u][node->u] = node;
        }
    }

    std::unordered_set<Edge, EdgeHash> edges1, edges2;
    for (const Edge& e : nonTreeEdges) {
        bool u_in_1 = verts1.count(e.u);
        bool v_in_1 = verts1.count(e.v);
        
        if (u_in_1 && v_in_1) {
            edges1.insert(e);
        } else if (!u_in_1 && !v_in_1) {
            edges2.insert(e);
        }
        // 跨越两个分量的边被丢弃
    }
    
    newTree1->nonTreeEdges = std::move(edges1);
    newTree2->nonTreeEdges = std::move(edges2);
    
    root = nullptr;
    vertices.clear();
    nonTreeEdges.clear();
    edgeNodes.clear();
    
    return {std::move(newTree1), std::move(newTree2)};
}

// 寻找替代边
Edge EulerTourTree::findReplacementEdge(const std::unordered_set<int>& component1,
                                        const std::unordered_set<int>& component2) {
    // 遍历较小分量的非树边
    const auto& smaller = component1.size() < component2.size() ? component1 : component2;
    const auto& larger = component1.size() < component2.size() ? component2 : component1;
    
    for (const Edge& e : nonTreeEdges) {
        // 检查边的一个端点在small，另一个在large
        if ((smaller.count(e.u) && larger.count(e.v)) ||
            (smaller.count(e.v) && larger.count(e.u))) {
            return e;
        }
    }
    
    return Edge(-1, -1);
}

// 查询操作
bool EulerTourTree::isConnected(int u, int v) const {
    return vertices.count(u) && vertices.count(v);
}


// 移除顶点
void EulerTourTree::removeVertex(int v) {
    if (!vertices.count(v)) return;
    
    // 删除所有occurrence
    auto& occs = vertexOccurrences[v];
    for (Node* node : occs) {
        // 从树中删除该节点
        splay(node);
        Node* left = node->left;
        Node* right = node->right;
        if (left) left->parent = nullptr;
        if (right) right->parent = nullptr;
        
        // 连接左右子树
        if (left && right) {
            join(left, right);
        } else if (left) {
            root = left;
        } else if (right) {
            root = right;
        } else {
            root = nullptr;
        }
        
        delete node;
    }
    
    vertices.erase(v);
    vertexOccurrences.erase(v);
    
    // 删除相关的非树边
    std::vector<Edge> toRemove;
    for (const Edge& e : nonTreeEdges) {
        if (e.u == v || e.v == v) {
            toRemove.push_back(e);
        }
    }
    for (const Edge& e : toRemove) {
        nonTreeEdges.erase(e);
    }
}

// 获取顶点度数（用于判断是否为边界顶点）
int EulerTourTree::getVertexDegree(int v) const {
    if (!vertices.count(v)) return 0;
    auto it = vertexOccurrences.find(v);
    if (it == vertexOccurrences.end()) return 0;
    // 度数 = occurrence数量 / 2（因为每条边产生2个occurrence）
    return (it->second.size() + 1) / 2;
}

// 调试：打印欧拉回路
void EulerTourTree::printEulerTour() const {
    std::vector<Node*> nodes;
    collectNodes(root, nodes);
    
    std::cout << "=== Euler Tour (Tree " << treeId << ") ===" << std::endl;
    std::cout << "Total nodes: " << nodes.size() << std::endl;
    std::cout << "Vertices: { ";
    for (int v : vertices) {
        std::cout << v << " ";
    }
    std::cout << "}" << std::endl;
    
    std::cout << "Tour sequence: ";
    for (size_t i = 0; i < nodes.size(); ++i) {
        Node* node = nodes[i];
        if (node->isEdge()) {
            std::cout << "(" << node->u << "->" << node->v << ")";
        } else {
            std::cout << "[" << node->u << "]";
        }
        if (i < nodes.size() - 1) std::cout << " -> ";
    }
    std::cout << std::endl;
    
    // 打印非树边
    if (!nonTreeEdges.empty()) {
        std::cout << "Non-tree edges: ";
        for (const Edge& e : nonTreeEdges) {
            std::cout << "(" << e.u << "," << e.v << ") ";
        }
        std::cout << std::endl;
    }
    
    std::cout << "==============================" << std::endl;
}

} // namespace splaytree