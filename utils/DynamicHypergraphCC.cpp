/**
 * @file DynamicHypergraphCC.cpp
 * @brief 动态超图连通性算法实现
 */

#include "../include/DynamicHypergraphCC.h"
#include <algorithm>
#include <iostream>
#include <chrono>
#include <cassert>

namespace hypergraph {

// ==================== ETTNode实现 ====================

ETTNode::ETTNode(NodeType type, int id) 
    : type(type), id(id), parent(nullptr), left(nullptr), 
      right(nullptr), subtree_size(1), reversed(false) {
}

ETTNode::~ETTNode() {
    // 递归删除由外部管理
}

void ETTNode::pushDown() {
    if (reversed) {
        std::swap(left, right);
        if (left) left->reversed ^= true;
        if (right) right->reversed ^= true;
        reversed = false;
    }
}

void ETTNode::pushUp() {
    subtree_size = 1;
    if (left) subtree_size += left->subtree_size;
    if (right) subtree_size += right->subtree_size;
}

void ETTNode::rotate() {
    ETTNode* p = parent;
    ETTNode* g = p->parent;
    
    if (g) {
        if (g->left == p) g->left = this;
        else g->right = this;
    }
    parent = g;
    
    if (p->left == this) {
        p->left = right;
        if (right) right->parent = p;
        right = p;
    } else {
        p->right = left;
        if (left) left->parent = p;
        left = p;
    }
    p->parent = this;
    
    p->pushUp();
    pushUp();
}

void ETTNode::splay() {
    while (parent) {
        ETTNode* p = parent;
        ETTNode* g = p->parent;
        
        if (g) g->pushDown();
        p->pushDown();
        pushDown();
        
        if (!g) {
            rotate();
        } else if ((g->left == p) == (p->left == this)) {
            p->rotate();
            rotate();
        } else {
            rotate();
            rotate();
        }
    }
    pushDown();
}

ETTNode* ETTNode::getRoot() {
    ETTNode* node = this;
    while (node->parent) {
        node = node->parent;
    }
    return node;
}

bool ETTNode::isConnected(ETTNode* other) {
    if (!other) return false;
    return getRoot() == other->getRoot();
}

void ETTNode::updateSize() {
    pushUp();
}

// ==================== EulerTourTree实现 ====================

EulerTourTree::EulerTourTree() 
    : component_count(0), next_comp_id(0) {
}

EulerTourTree::~EulerTourTree() {
    clear();
}

void EulerTourTree::addVertex(RowID row_id) {
    if (vertex_nodes.find(row_id) != vertex_nodes.end()) {
        return;
    }
    
    ETTNode* node = new ETTNode(ETTNode::NodeType::VERTEX, row_id);
    vertex_nodes[row_id] = node;
    vertex_to_comp[row_id] = next_comp_id++;
    component_count++;
}

void EulerTourTree::removeVertex(RowID row_id) {
    auto it = vertex_nodes.find(row_id);
    if (it != vertex_nodes.end()) {
        delete it->second;
        vertex_nodes.erase(it);
        vertex_to_comp.erase(row_id);
    }
}

void EulerTourTree::link(RowID u, RowID v, ColID edge_id) {
    auto u_it = vertex_nodes.find(u);
    auto v_it = vertex_nodes.find(v);
    
    if (u_it == vertex_nodes.end() || v_it == vertex_nodes.end()) {
        return;
    }
    
    ETTNode* u_node = u_it->second;
    ETTNode* v_node = v_it->second;
    
    // 检查是否已经连通
    if (u_node->isConnected(v_node)) {
        return;  // 已连通，这是非树边
    }
    
    // 创建边节点
    ETTNode* edge_in = new ETTNode(ETTNode::NodeType::EDGE_IN, edge_id);
    ETTNode* edge_out = new ETTNode(ETTNode::NodeType::EDGE_OUT, edge_id);
    edge_nodes[edge_id] = {edge_in, edge_out};
    
    // 插入边到欧拉游览
    insertEdgeInTour(u, v, edge_id);
    
    // 更新连通分量
    CompID u_comp = vertex_to_comp[u];
    CompID v_comp = vertex_to_comp[v];
    
    // 合并分量
    for (auto& pair : vertex_to_comp) {
        if (pair.second == v_comp) {
            pair.second = u_comp;
        }
    }
    component_count--;
}

void EulerTourTree::cut(RowID u, RowID v, ColID edge_id) {
    auto edge_it = edge_nodes.find(edge_id);
    if (edge_it == edge_nodes.end()) {
        return;
    }
    
    removeEdgeFromTour(edge_id);
    
    delete edge_it->second.first;
    delete edge_it->second.second;
    edge_nodes.erase(edge_it);
    
    // 重新计算连通分量
    auto u_it = vertex_nodes.find(u);
    auto v_it = vertex_nodes.find(v);
    
    if (u_it != vertex_nodes.end() && v_it != vertex_nodes.end()) {
        if (!u_it->second->isConnected(v_it->second)) {
            // 分裂成两个分量
            recomputeComponent(u_it->second->getRoot());
            recomputeComponent(v_it->second->getRoot());
            component_count++;
        }
    }
}

bool EulerTourTree::isConnected(RowID u, RowID v) {
    auto u_it = vertex_nodes.find(u);
    auto v_it = vertex_nodes.find(v);
    
    if (u_it == vertex_nodes.end() || v_it == vertex_nodes.end()) {
        return false;
    }
    
    return u_it->second->isConnected(v_it->second);
}

CompID EulerTourTree::getComponentID(RowID row_id) {
    auto it = vertex_to_comp.find(row_id);
    if (it != vertex_to_comp.end()) {
        return it->second;
    }
    return -1;
}

std::vector<RowID> EulerTourTree::getComponentVertices(RowID row_id) {
    std::vector<RowID> result;
    auto it = vertex_nodes.find(row_id);
    if (it == vertex_nodes.end()) {
        return result;
    }
    
    CompID comp_id = vertex_to_comp[row_id];
    for (const auto& pair : vertex_to_comp) {
        if (pair.second == comp_id) {
            result.push_back(pair.first);
        }
    }
    
    return result;
}

ColID EulerTourTree::findReplacementEdge(
    RowID u, RowID v,
    const std::unordered_map<ColID, std::unordered_set<RowID>>& non_tree_edges) {
    
    // 获取两个子树
    auto u_vertices = getComponentVertices(u);
    auto v_vertices = getComponentVertices(v);
    
    std::unordered_set<RowID> u_set(u_vertices.begin(), u_vertices.end());
    std::unordered_set<RowID> v_set(v_vertices.begin(), v_vertices.end());
    
    // 查找连接两个子树的非树边
    for (const auto& edge_pair : non_tree_edges) {
        const auto& vertices = edge_pair.second;
        
        bool has_u = false, has_v = false;
        for (RowID vertex : vertices) {
            if (u_set.count(vertex)) has_u = true;
            if (v_set.count(vertex)) has_v = true;
            if (has_u && has_v) {
                return edge_pair.first;
            }
        }
    }
    
    return -1;  // 未找到替代边
}

void EulerTourTree::clear() {
    for (auto& pair : vertex_nodes) {
        delete pair.second;
    }
    vertex_nodes.clear();
    
    for (auto& pair : edge_nodes) {
        delete pair.second.first;
        delete pair.second.second;
    }
    edge_nodes.clear();
    
    vertex_to_comp.clear();
    component_count = 0;
}

void EulerTourTree::insertEdgeInTour(RowID u, RowID v, ColID edge_id) {
    ETTNode* u_node = vertex_nodes[u];
    ETTNode* v_node = vertex_nodes[v];
    auto& edge_pair = edge_nodes[edge_id];
    
    // 简化实现：将边节点链接到顶点
    makeRoot(u_node);
    makeRoot(v_node);
    
    // 连接: u -> edge_in -> v -> edge_out -> u
    edge_pair.first->left = u_node;
    u_node->parent = edge_pair.first;
    
    edge_pair.first->right = v_node;
    v_node->parent = edge_pair.first;
    
    edge_pair.second->left = edge_pair.first;
    edge_pair.first->parent = edge_pair.second;
}

void EulerTourTree::removeEdgeFromTour(ColID edge_id) {
    auto& edge_pair = edge_nodes[edge_id];
    ETTNode* edge_in = edge_pair.first;
    ETTNode* edge_out = edge_pair.second;
    
    // 断开连接
    if (edge_in->left) edge_in->left->parent = nullptr;
    if (edge_in->right) edge_in->right->parent = nullptr;
    if (edge_out->left) edge_out->left->parent = nullptr;
    if (edge_out->right) edge_out->right->parent = nullptr;
}

void EulerTourTree::recomputeComponent(ETTNode* root) {
    if (!root) return;
    
    // BFS遍历该组件的所有顶点
    std::queue<ETTNode*> queue;
    std::unordered_set<ETTNode*> visited;
    
    queue.push(root);
    visited.insert(root);
    
    CompID new_comp_id = next_comp_id++;
    
    while (!queue.empty()) {
        ETTNode* node = queue.front();
        queue.pop();
        
        if (node->type == ETTNode::NodeType::VERTEX) {
            vertex_to_comp[node->id] = new_comp_id;
        }
        
        if (node->left && visited.find(node->left) == visited.end()) {
            queue.push(node->left);
            visited.insert(node->left);
        }
        if (node->right && visited.find(node->right) == visited.end()) {
            queue.push(node->right);
            visited.insert(node->right);
        }
    }
}

void EulerTourTree::makeRoot(ETTNode* node) {
    if (!node) return;
    expose(node);
    node->splay();
    if (node->left) {
        node->left->reversed ^= true;
        node->left = nullptr;
        node->pushUp();
    }
}

void EulerTourTree::expose(ETTNode* node) {
    if (!node) return;
    node->splay();
}

ETTNode* EulerTourTree::findFirst(ETTNode* node) {
    if (!node) return nullptr;
    node->pushDown();
    while (node->left) {
        node = node->left;
        node->pushDown();
    }
    return node;
}

// ==================== HyperEdge实现 ====================

HyperEdge::HyperEdge(ColID id) 
    : col_id(id), active(true), edge_type(EdgeType::UNKNOWN), weight(1.0) {
}

void HyperEdge::addVertex(RowID row_id) {
    vertices.insert(row_id);
}

void HyperEdge::removeVertex(RowID row_id) {
    vertices.erase(row_id);
}

std::vector<std::pair<RowID, RowID>> HyperEdge::getVertexPairs() const {
    std::vector<std::pair<RowID, RowID>> pairs;
    std::vector<RowID> vec(vertices.begin(), vertices.end());
    
    for (size_t i = 0; i < vec.size(); ++i) {
        for (size_t j = i + 1; j < vec.size(); ++j) {
            pairs.emplace_back(vec[i], vec[j]);
        }
    }
    
    return pairs;
}

// ==================== Vertex实现 ====================

Vertex::Vertex(RowID id) 
    : row_id(id), active(true), component_id(-1) {
}

void Vertex::addEdge(ColID col_id, EdgeType type) {
    all_edges.insert(col_id);
    
    if (type == EdgeType::TREE) {
        tree_edges.insert(col_id);
        non_tree_edges.erase(col_id);
    } else if (type == EdgeType::NON_TREE) {
        non_tree_edges.insert(col_id);
        tree_edges.erase(col_id);
    }
}

void Vertex::removeEdge(ColID col_id) {
    all_edges.erase(col_id);
    tree_edges.erase(col_id);
    non_tree_edges.erase(col_id);
}

void Vertex::reclassifyEdge(ColID col_id, EdgeType new_type) {
    if (all_edges.count(col_id) == 0) return;
    
    tree_edges.erase(col_id);
    non_tree_edges.erase(col_id);
    
    if (new_type == EdgeType::TREE) {
        tree_edges.insert(col_id);
    } else if (new_type == EdgeType::NON_TREE) {
        non_tree_edges.insert(col_id);
    }
}

// ==================== PersistentState实现 ====================

PersistentState::PersistentState() 
    : current_version(0), next_version_id(1) {
    // 创建初始版本
    versions[0] = std::make_shared<Version>(0);
}

VersionID PersistentState::createVersion(VersionID parent_id) {
    std::lock_guard<std::mutex> lock(version_mutex);
    
    if (parent_id == -1) {
        parent_id = current_version;
    }
    
    VersionID new_id = next_version_id++;
    versions[new_id] = std::make_shared<Version>(new_id, parent_id);
    current_version = new_id;
    
    return new_id;
}

void PersistentState::addOperation(VersionID ver_id, const Operation& op) {
    std::lock_guard<std::mutex> lock(version_mutex);
    
    auto it = versions.find(ver_id);
    if (it != versions.end()) {
        it->second->operations.push_back(op);
    }
}

bool PersistentState::rollbackToVersion(VersionID target_version) {
    std::lock_guard<std::mutex> lock(version_mutex);
    
    if (versions.find(target_version) == versions.end()) {
        return false;
    }
    
    current_version = target_version;
    return true;
}

const std::vector<PersistentState::Operation>& 
PersistentState::getOperations(VersionID ver_id) const {
    static std::vector<Operation> empty;
    
    auto it = versions.find(ver_id);
    if (it != versions.end()) {
        return it->second->operations;
    }
    return empty;
}

std::vector<VersionID> PersistentState::getVersionHistory() const {
    std::vector<VersionID> history;
    VersionID ver = current_version;
    
    while (ver != -1) {
        history.push_back(ver);
        auto it = versions.find(ver);
        if (it != versions.end()) {
            ver = it->second->parent_version;
        } else {
            break;
        }
    }
    
    std::reverse(history.begin(), history.end());
    return history;
}

void PersistentState::pruneOldVersions(size_t keep_count) {
    std::lock_guard<std::mutex> lock(version_mutex);
    
    if (versions.size() <= keep_count) {
        return;
    }
    
    // 保留最近的版本
    std::vector<VersionID> to_remove;
    for (const auto& pair : versions) {
        if (pair.first < static_cast<VersionID>(next_version_id - keep_count)) {
            to_remove.push_back(pair.first);
        }
    }
    
    for (VersionID vid : to_remove) {
        versions.erase(vid);
    }
}

// ==================== ThreadPool实现 ====================

ThreadPool_::ThreadPool_(size_t num_threads) : stop(false), active_tasks(0) {
    for (size_t i = 0; i < num_threads; ++i) {
        workers.emplace_back([this] {
            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(queue_mutex);
                    condition.wait(lock, [this] {
                        return stop || !tasks.empty();
                    });
                    
                    if (stop && tasks.empty()) {
                        return;
                    }
                    
                    task = std::move(tasks.front());
                    tasks.pop();
                }
                
                active_tasks++;
                task();
                active_tasks--;
            }
        });
    }
}

ThreadPool_::~ThreadPool_() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stop = true;
    }
    condition.notify_all();
    
    for (std::thread& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void ThreadPool_::waitAll() {
    while (active_tasks > 0 || !tasks.empty()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

// ==================== DynamicHypergraphCC实现 ====================

DynamicHypergraphCC::DynamicHypergraphCC(const Config& config) 
    : config(config), next_comp_id(0) {
    
    ett = std::make_unique<EulerTourTree>();
    
    if (config.enable_persistence) {
        persistent_state = std::make_unique<PersistentState>();
    }
    
    if (config.enable_parallel) {
        thread_pool = std::make_unique<ThreadPool_>(config.num_threads);
    }
}

DynamicHypergraphCC::~DynamicHypergraphCC() = default;

void DynamicHypergraphCC::addRow(RowID row_id, const std::vector<ColID>& col_ids) {
    std::unique_lock<std::shared_mutex> lock(global_mutex);
    
    if (vertices.find(row_id) != vertices.end()) {
        return;
    }
    
    auto vertex = std::make_unique<Vertex>(row_id);
    
    for (ColID col_id : col_ids) {
        if (hyperedges.find(col_id) == hyperedges.end()) {
            hyperedges[col_id] = std::make_unique<HyperEdge>(col_id);
        }
        
        vertex->addEdge(col_id);
        hyperedges[col_id]->addVertex(row_id);
    }
    
    vertices[row_id] = std::move(vertex);
    ett->addVertex(row_id);
}

void DynamicHypergraphCC::buildInitialComponents() {
    std::unique_lock<std::shared_mutex> lock(global_mutex);
    
    std::unordered_set<RowID> visited;
    
    for (const auto& vertex_pair : vertices) {
        RowID row_id = vertex_pair.first;
        
        if (visited.count(row_id) || !vertex_pair.second->isActive()) {
            continue;
        }
        
        auto component = buildComponentBFS(row_id);
        
        // 标记已访问
        for (RowID r : component->rows) {
            visited.insert(r);
        }
        
        components[component->id] = component;
        classifyEdges(component);
    }
}

std::shared_ptr<ConnectedComponent> 
DynamicHypergraphCC::buildComponentBFS(RowID start_row) {
    auto component = std::make_shared<ConnectedComponent>(next_comp_id++);
    
    std::queue<RowID> queue;
    std::unordered_set<RowID> visited;
    
    queue.push(start_row);
    visited.insert(start_row);
    
    while (!queue.empty()) {
        RowID curr = queue.front();
        queue.pop();
        
        component->rows.insert(curr);
        vertices[curr]->setComponentID(component->id);
        
        // 遍历邻居
        for (RowID neighbor : getNeighbors(curr)) {
            if (visited.count(neighbor) == 0 && vertices[neighbor]->isActive()) {
                visited.insert(neighbor);
                queue.push(neighbor);
            }
        }
    }
    
    // 收集列
    for (RowID row_id : component->rows) {
        for (ColID col_id : vertices[row_id]->getAllEdges()) {
            if (hyperedges[col_id]->isActive()) {
                component->cols.insert(col_id);
            }
        }
    }
    
    updateComponentMetadata(component);
    
    return component;
}

std::vector<CompID> DynamicHypergraphCC::coverColumn(ColID col_id) {
    std::unique_lock<std::shared_mutex> lock(global_mutex);
    
    auto it = hyperedges.find(col_id);
    if (it == hyperedges.end() || !it->second->isActive()) {
        return {};
    }
    
    auto& hyperedge = it->second;
    std::vector<RowID> affected_rows(hyperedge->getVertices().begin(), 
                                     hyperedge->getVertices().end());
    
    // 记录操作
    if (persistent_state) {
        recordOperation(PersistentState::Operation::Type::COVER_COLUMN, 
                       col_id, affected_rows);
    }
    
    // 停用超边和行
    hyperedge->setActive(false);
    
    std::unordered_set<CompID> affected_comps;
    for (RowID row_id : affected_rows) {
        if (vertices[row_id]->isActive()) {
            affected_comps.insert(vertices[row_id]->getComponentID());
            vertices[row_id]->setActive(false);
            vertices[row_id]->removeEdge(col_id);
        }
    }
    
    // 增量更新
    std::vector<CompID> result;
    for (CompID comp_id : affected_comps) {
        auto new_comps = incrementalUpdate(comp_id, col_id, affected_rows);
        result.insert(result.end(), new_comps.begin(), new_comps.end());
    }
    
    return result;
}

std::vector<CompID> DynamicHypergraphCC::incrementalUpdate(
    CompID comp_id, ColID removed_col, const std::vector<RowID>& removed_rows) {
    
    auto comp_it = components.find(comp_id);
    if (comp_it == components.end()) {
        return {};
    }
    
    auto& component = comp_it->second;
    auto& hyperedge = hyperedges[removed_col];
    
    // 策略1：快速判断 - 删除非树边
    if (hyperedge->getEdgeType() == EdgeType::NON_TREE) {
        std::unordered_set<RowID> remaining;
        for (RowID r : component->rows) {
            if (vertices[r]->isActive()) {
                remaining.insert(r);
            }
        }
        
        if (quickConnectivityCheck(remaining)) {
            stats.quick_path_hits++;
            
            // 更新组件
            component->rows = remaining;
            component->cols.erase(removed_col);
            updateComponentMetadata(component);
            
            return {comp_id};
        }
    }
    
    // 策略2：使用ETT查找替代边
    if (hyperedge->getEdgeType() == EdgeType::TREE) {
        // 尝试在ETT中查找替代边
        std::unordered_map<ColID, std::unordered_set<RowID>> non_tree_edges;
        for (RowID r : component->rows) {
            if (!vertices[r]->isActive()) continue;
            for (ColID c : vertices[r]->getNonTreeEdges()) {
                if (hyperedges[c]->isActive()) {
                    non_tree_edges[c].insert(r);
                }
            }
        }
        
        // 使用ETT查找替代边
        if (!removed_rows.empty() && removed_rows.size() >= 2) {
            ColID replacement = ett->findReplacementEdge(
                removed_rows[0], removed_rows[1], non_tree_edges);
            
            if (replacement != -1) {
                stats.replacement_found++;
                
                // 升级替代边为树边
                hyperedges[replacement]->setEdgeType(EdgeType::TREE);
                for (RowID r : hyperedges[replacement]->getVertices()) {
                    vertices[r]->reclassifyEdge(replacement, EdgeType::TREE);
                }
                
                // 更新ETT
                auto pairs = hyperedges[replacement]->getVertexPairs();
                if (!pairs.empty()) {
                    ett->link(pairs[0].first, pairs[0].second, replacement);
                }
                
                component->cols.erase(removed_col);
                return {comp_id};
            }
        }
    }
    
    // 策略3：完整重算
    stats.full_recompute++;
    stats.splits_detected++;
    
    std::unordered_set<RowID> scope;
    for (RowID r : component->rows) {
        if (vertices[r]->isActive()) {
            scope.insert(r);
        }
    }
    
    component->active = false;
    return recomputeComponents(scope);
}

bool DynamicHypergraphCC::quickConnectivityCheck(
    const std::unordered_set<RowID>& vertices_set) {
    
    if (vertices_set.size() <= 1) {
        return true;
    }
    
    // 使用ETT快速检查
    auto it = vertices_set.begin();
    RowID first = *it;
    ++it;
    
    for (; it != vertices_set.end(); ++it) {
        if (!ett->isConnected(first, *it)) {
            return false;
        }
    }
    
    return true;
}

std::vector<CompID> DynamicHypergraphCC::recomputeComponents(
    const std::unordered_set<RowID>& scope_rows) {
    
    std::unordered_set<RowID> visited;
    std::vector<CompID> new_comp_ids;
    
    for (RowID row_id : scope_rows) {
        if (visited.count(row_id) || !vertices[row_id]->isActive()) {
            continue;
        }
        
        auto new_comp = buildComponentBFS(row_id);
        
        for (RowID r : new_comp->rows) {
            visited.insert(r);
        }
        
        components[new_comp->id] = new_comp;
        classifyEdges(new_comp);
        new_comp_ids.push_back(new_comp->id);
    }
    
    return new_comp_ids;
}

void DynamicHypergraphCC::classifyEdges(std::shared_ptr<ConnectedComponent> component) {
    // 使用BFS分类树边和非树边
    if (component->rows.empty()) return;
    
    RowID root = *component->rows.begin();
    std::unordered_set<RowID> visited;
    std::queue<RowID> queue;
    
    visited.insert(root);
    queue.push(root);
    
    while (!queue.empty()) {
        RowID curr = queue.front();
        queue.pop();
        
        for (ColID col_id : vertices[curr]->getAllEdges()) {
            if (!hyperedges[col_id]->isActive()) continue;
            
            auto& edge_vertices = hyperedges[col_id]->getVertices();
            bool is_tree = false;
            
            for (RowID neighbor : edge_vertices) {
                if (neighbor == curr || !vertices[neighbor]->isActive()) continue;
                
                if (visited.count(neighbor) == 0) {
                    // 树边
                    visited.insert(neighbor);
                    queue.push(neighbor);
                    is_tree = true;
                    
                    vertices[curr]->addEdge(col_id, EdgeType::TREE);
                    vertices[neighbor]->addEdge(col_id, EdgeType::TREE);
                    hyperedges[col_id]->setEdgeType(EdgeType::TREE);
                    
                    // 在ETT中添加
                    ett->link(curr, neighbor, col_id);
                }
            }
            
            if (!is_tree) {
                // 非树边
                hyperedges[col_id]->setEdgeType(EdgeType::NON_TREE);
                for (RowID v : edge_vertices) {
                    if (vertices[v]->isActive()) {
                        vertices[v]->addEdge(col_id, EdgeType::NON_TREE);
                    }
                }
            }
        }
    }
}

void DynamicHypergraphCC::updateComponentMetadata(
    std::shared_ptr<ConnectedComponent> component) {
    
    size_t total_edges = 0;
    for (RowID r : component->rows) {
        total_edges += vertices[r]->getAllEdges().size();
    }
    
    size_t max_edges = component->rows.size() * component->cols.size();
    component->density = max_edges > 0 ? 
        static_cast<double>(total_edges) / max_edges : 0.0;
}

std::unordered_set<RowID> DynamicHypergraphCC::getNeighbors(RowID row_id) {
    std::unordered_set<RowID> neighbors;
    
    for (ColID col_id : vertices[row_id]->getAllEdges()) {
        if (!hyperedges[col_id]->isActive()) continue;
        
        for (RowID neighbor : hyperedges[col_id]->getVertices()) {
            if (neighbor != row_id && vertices[neighbor]->isActive()) {
                neighbors.insert(neighbor);
            }
        }
    }
    
    return neighbors;
}

void DynamicHypergraphCC::solveComponentsParallel(ComponentSolver solver) {
    if (!config.enable_parallel || !thread_pool) {
        // 串行执行
        for (const auto& comp_pair : components) {
            if (comp_pair.second->active) {
                solver(*comp_pair.second);
            }
        }
        return;
    }
    
    // 并行执行
    std::vector<std::future<void>> futures;
    
    for (const auto& comp_pair : components) {
        if (comp_pair.second->active) {
            futures.push_back(thread_pool->enqueue(
                [solver, comp = comp_pair.second]() {
                    solver(*comp);
                }
            ));
        }
    }
    
    // 等待所有任务完成
    for (auto& future : futures) {
        future.get();
    }
    
    stats.parallel_solves++;
}

bool DynamicHypergraphCC::isConnected(RowID u, RowID v) {
    std::shared_lock<std::shared_mutex> lock(global_mutex);
    return ett->isConnected(u, v);
}

CompID DynamicHypergraphCC::getComponentID(RowID row_id) {
    std::shared_lock<std::shared_mutex> lock(global_mutex);
    
    auto it = vertices.find(row_id);
    if (it != vertices.end()) {
        return it->second->getComponentID();
    }
    return -1;
}

std::vector<std::shared_ptr<ConnectedComponent>> 
DynamicHypergraphCC::getAllComponents() {
    std::shared_lock<std::shared_mutex> lock(global_mutex);
    
    std::vector<std::shared_ptr<ConnectedComponent>> result;
    for (const auto& pair : components) {
        if (pair.second->active) {
            result.push_back(pair.second);
        }
    }
    return result;
}

void DynamicHypergraphCC::printStatistics() const {
    std::cout << "=== 动态超图连通性统计 ===" << std::endl;
    std::cout << "组件数: " << components.size() << std::endl;
    std::cout << "快速路径命中: " << stats.quick_path_hits << std::endl;
    std::cout << "完整重算: " << stats.full_recompute << std::endl;
    std::cout << "替代边找到: " << stats.replacement_found << std::endl;
    std::cout << "分裂检测: " << stats.splits_detected << std::endl;
    std::cout << "并行求解: " << stats.parallel_solves << std::endl;
}

void DynamicHypergraphCC::recordOperation(
    PersistentState::Operation::Type type, ColID col_id,
    const std::vector<RowID>& affected_rows) {
    
    if (!persistent_state) return;
    
    PersistentState::Operation op(type, col_id);
    op.affected_rows = affected_rows;
    
    persistent_state->addOperation(persistent_state->getCurrentVersion(), op);
}

// 其他辅助方法的实现...
void DynamicHypergraphCC::uncoverColumn(ColID col_id) {
    // 实现uncover逻辑
}

std::vector<CompID> DynamicHypergraphCC::batchCover(const std::vector<ColID>& col_ids) {
    // 实现批量cover逻辑
    std::vector<CompID> result;
    for (ColID col : col_ids) {
        auto comps = coverColumn(col);
        result.insert(result.end(), comps.begin(), comps.end());
    }
    return result;
}

VersionID DynamicHypergraphCC::saveVersion() {
    if (persistent_state) {
        return persistent_state->createVersion();
    }
    return -1;
}

bool DynamicHypergraphCC::restoreVersion(VersionID version_id) {
    if (persistent_state) {
        return persistent_state->rollbackToVersion(version_id);
    }
    return false;
}

std::shared_ptr<ConnectedComponent> DynamicHypergraphCC::getComponent(CompID comp_id) {
    auto it = components.find(comp_id);
    return it != components.end() ? it->second : nullptr;
}

std::vector<RowID> DynamicHypergraphCC::getComponentRows(CompID comp_id) {
    auto comp = getComponent(comp_id);
    if (comp) {
        return std::vector<RowID>(comp->rows.begin(), comp->rows.end());
    }
    return {};
}

std::vector<VersionID> DynamicHypergraphCC::getVersionHistory() {
    if (persistent_state) {
        return persistent_state->getVersionHistory();
    }
    return {};
}

void DynamicHypergraphCC::resetStatistics() {
    stats = Statistics();
}

void DynamicHypergraphCC::setConfig(const Config& new_config) {
    config = new_config;
}

} // namespace hypergraph