#include "../include/DancingMatrix.h"
#include "DynamicGraph.cpp"
#include <chrono>
thread_local std::unique_ptr<DancingMatrix::ThreadLocalState> DancingMatrix::tlsState = nullptr;

namespace {

double elapsedSeconds(
    const std::chrono::steady_clock::time_point& start) {
    return std::chrono::duration_cast<std::chrono::duration<double>>(
               std::chrono::steady_clock::now() - start)
        .count();
}

}  // namespace

//构造函数
DancingMatrix::DancingMatrix(int rows, int cols, int** matrix, bool verbose,
                             AlgorithmMode mode)
    : ROWS(rows), COLS(cols), algorithmMode(mode),
      enableGraphSync(verbose) {
    EXIST_ROWS = rows;  
    ColIndex = std::make_unique<ColumnHeader[]>(cols + 1);  
    RowIndex = std::make_unique<RowNode[]>(rows);  
    // if (verbose) graph = make_unique<ConnectedGraph>(rows, cols);

    root = &ColIndex[0];  
    ColIndex[0].left = &ColIndex[COLS];  
    ColIndex[0].right = &ColIndex[1];  
    ColIndex[COLS].right = &ColIndex[0];  
    ColIndex[COLS].left = &ColIndex[COLS-1];  

    for( int i = 1; i < cols; i++ )  
    {  
        ColIndex[i].left = &ColIndex[i-1];  
        ColIndex[i].right = &ColIndex[i+1];  
    }  

    for ( int i = 0; i <= cols; i++ )  
    {  
        ColIndex[i].up = &ColIndex[i];  
        ColIndex[i].down = &ColIndex[i];  
        ColIndex[i].col = i;  
    }  
    ColIndex[0].down = &RowIndex[0];  
    
    dataNodes.reserve(rows * cols / 2);

    // graph = std::make_shared<ConnectedGraph>(ROWS, COLS);
    for( int i = 0; i < rows; i++ ){
        for( int j = 0; j < cols ; j++ ) {  
            if(matrix[i][j] == 1){
                insert(  i , j+1 );  //行数与原矩阵相同，而列数加1
                rowsSet.insert(i);
                colsSet.insert(j+1); 
                ++ONE_COUNT;
            }
        }
    }

    InitBlock = Block(rowsSet, colsSet);
    InitBlock.activeEntries = ONE_COUNT;
    InitBlock.activeEntriesValid = true;
    // if(verbose) graph = make_unique<ConnectedGraph>(*this);
    std::cout<< "初始化舞蹈链完成." << endl;
}

// 从文件构造舞蹈链矩阵
DancingMatrix::DancingMatrix(const string& file_path, AlgorithmMode mode)
    : algorithmMode(mode)
{
    ifstream file(file_path);
    if (!file.is_open()) {
        cerr << "无法打开文件: " << file_path << endl;
        throw runtime_error("无法打开文件");
    }

    string line;
    if (!getline(file, line)) {
        throw runtime_error("文件为空");
    }

    int rows, cols;
    int skip_tokens = -1; // -1 表示尚未确定每行需要跳过的 token 数量

    // 自动检测模式 1 (通常首行包含 '=' 或以 'c ' 开头)
    if (line.find("=") != string::npos || line.find("c ") == 0) {
        PreProccess::extractNM( line, cols, rows );
        getline(file, line);  // 跳过第二行 
        skip_tokens = 1;      // 模式 1 数据行只跳过一个 token (即 row_id)
    } else {
        // 首行是 "cols rows" (模式 2 或 3)
        istringstream iss(line);
        iss >> cols >> rows;
        // 此处先不设置 skip_tokens，留到读取第一行有效数据时动态推断
    }

    ROWS = rows;
    COLS = cols;

    if (ROWS > MAX_ROW) {
        cerr << "矩阵行数过大，无法处理: " << ROWS << " 行." << endl;
        throw runtime_error("matrix exceeds MAX_ROW");
    }

    cout << "rows: " << rows << endl;
    cout << "cols: " << cols << endl;
    ColIndex = std::make_unique<ColumnHeader[]>(cols + 1);  
    RowIndex = std::make_unique<RowNode[]>(rows);  
    root = &ColIndex[0];  

    ColIndex[0].left = &ColIndex[COLS];  
    ColIndex[0].right = &ColIndex[1];  
    ColIndex[COLS].right = &ColIndex[0];  
    ColIndex[COLS].left = &ColIndex[COLS-1];  

    for( int i = 1; i < cols; i++ )  
    {  
        ColIndex[i].left = &ColIndex[i-1];  
        ColIndex[i].right = &ColIndex[i+1];  
    }  

    for ( int i = 0; i <= cols; i++ )  
    {  
        ColIndex[i].up = &ColIndex[i];  
        ColIndex[i].down = &ColIndex[i];  
        ColIndex[i].col = i;  
    }  
    ColIndex[0].down = &RowIndex[0]; 

    dataNodes.reserve(rows * cols / 2);
    int currentRow = 0;
    while (getline(file, line)) {
        if (line.empty()) continue; // 跳过空行

        // 探测第一行数据以区分模式 2 和模式 3
        if (skip_tokens == -1) {
            istringstream peek_iss(line);
            std::vector<int> temp_tokens;
            int val;
            while (peek_iss >> val) {
                temp_tokens.push_back(val);
            }
            if (temp_tokens.empty()) continue; // 预防全空格的行

            // 模式 3: [count] [id1] [id2] ...
            // 元素总个数一定等于 temp_tokens[0] + 1
            if (temp_tokens.size() == static_cast<size_t>(temp_tokens[0] + 1)) {
                skip_tokens = 1; 
            } 
            // 模式 2: [id] [count] [id1] [id2] ...
            // 元素总个数一定等于 temp_tokens[1] + 2
            else if (temp_tokens.size() >= 2 && temp_tokens.size() == static_cast<size_t>(temp_tokens[1] + 2)) {
                skip_tokens = 2;
            } 
            else {
                cerr << "警告: 无法自动识别数据行格式，默认跳过1个标识符" << endl;
                skip_tokens = 1;
            }
        }

        istringstream iss(line);
        string token;

        // 自动跳过不需要的 id 或 count 字段
        for (int i = 0; i < skip_tokens; ++i) {
            iss >> token;
        }

        int currentCol; 
        // std::vector<int> rowCols;
        while(iss >> currentCol) {
            if (currentCol < 1 || currentCol > cols) {
                cerr << "无效的列索引: " << currentCol << " 在行 " << currentRow + 1 << endl;
                exit(1);
            }
            insert(currentRow, currentCol); // 插入节点
            col_to_rows[currentCol].push_back(currentRow);
            row_to_cols[currentRow].insert(currentCol);
            ONE_COUNT++; // 统计矩阵中1的个数
            rowsSet.insert(currentRow);
            colsSet.insert(currentCol); 
            // rowCols.push_back(currentCol);
        }

        currentRow++;
        
        if (currentRow >= rows) break; // 防止超过预期行数
    }

    InitBlock = Block(rowsSet, colsSet);
    InitBlock.activeEntries = ONE_COUNT;
    InitBlock.activeEntriesValid = true;

    if (algorithmMode == AlgorithmMode::MDLX_DYNAMIC) {
        graph = make_unique<Graph>(ROWS);
        buildGraphFromMatrix();
        buildSpanningForest();
        
        cout << "Statistics graph initialization complete." << endl;
    }

    file.close();
}

DancingMatrix::~DancingMatrix() = default;

void DancingMatrix::buildStatsSpanningForest() {
    statsForest.assign(ROWS, {});
    statsNonTreeEdges.clear();
    statsActiveEdgeCount = 0;
    std::vector<bool> visited(ROWS, false);
    std::queue<int> queue;
    for (int start = 0; start < ROWS; ++start) {
        if (visited[start]) continue;
        visited[start] = true;
        queue.push(start);
        while (!queue.empty()) {
            const int u = queue.front();
            queue.pop();
            for (int v : graph->getNeighbors(u)) {
                if (!visited[v]) {
                    visited[v] = true;
                    queue.push(v);
                    statsForest[u].insert(v);
                    statsForest[v].insert(u);
                } else if (!statsForest[u].count(v)) {
                    statsNonTreeEdges.emplace(u, v);
                }
            }
        }
    }
    for (int u = 0; u < ROWS; ++u) statsActiveEdgeCount += statsForest[u].size();
    statsActiveEdgeCount = statsActiveEdgeCount / 2 + statsNonTreeEdges.size();
    statsForestReady = true;
}

void DancingMatrix::initThreadLocalState(const Block& block, std::unique_ptr<splaytree::EulerTourTree> tree) {
    
    // 创建或重置线程局部状态
    if (!tlsState) {
        tlsState = std::make_unique<ThreadLocalState>();
    }  
    
    // 清空旧数据
    tlsState->components.clear();
    tlsState->graphContext.reset();
    tlsState->subgraph  = nullptr; 
    tlsState->activeSubgraph = nullptr;
    tlsState->nextTreeId = 0;
    tlsState->decompositionPolicy = DecompositionPolicy{};
    tlsState->ettEligibleRows.clear();
    tlsState->ettEligibleRows.insert(block.rows.begin(), block.rows.end());
    tlsState->dynamicActiveVertices = 0;
    tlsState->dynamicActiveEdges = 0;
    tlsState->dynamicMetricsValid = false;

    if (!tree) {
        // Descendants that no longer maintain ETT only need thread-local
        // policy state and the current subgraph routing information.
        tlsState->initialized = true; 
        return; 
    }
    
    int anyV = tree->getAnyVertex();

    auto context = std::make_shared<TreeGraphContext>();
    context->blockRows.insert(tree->vertices.begin(), tree->vertices.end());
    context->graph = std::make_unique<Graph>(ROWS);
    TreeGraphContext* rawContext = context.get();
    context->graph->setNeighborProvider(
        [this, rawContext](int row) {
            return implicitProjectedNeighborsInRows(
                row, rawContext->blockRows, rawContext->graph.get());
        });
    for (int row : tlsState->ettEligibleRows) {
        if (!block.rows.count(row)) {
            context->graph->deleteVertex(row);
        }
    }
    tlsState->graphContext = context;
    if (anyV >= 0) {
        context->graph->registerComponent(0, context->blockRows);
        tlsState->subgraph = context->graph->subgraphOf(anyV);
        tlsState->activeSubgraph = tlsState->subgraph;
    }
    bindTreeReplacementProvider(tree.get(), context);
    tlsState->components.push_back(std::move(tree));
    
    tlsState->initialized = true;
}

void DancingMatrix::bindTreeReplacementProvider(
    splaytree::EulerTourTree* tree,
    const std::shared_ptr<TreeGraphContext>& context) {
    if (!tree || !context || !context->graph) return;

    tree->setReplacementNeighborProvider(
        [this, context](int row, std::unordered_set<int>& out) {
            std::vector<int> neighbors = implicitProjectedNeighborsInRows(
                row, context->blockRows, context->graph.get());
            out.insert(neighbors.begin(), neighbors.end());
        });
}

void DancingMatrix::bindGlobalTreeReplacementProvider(
    splaytree::EulerTourTree* tree) {
    if (!tree) return;

    tree->setReplacementNeighborProvider(
        [this](int row, std::unordered_set<int>& out) {
            std::vector<int> neighbors = implicitProjectedNeighbors(row);
            out.insert(neighbors.begin(), neighbors.end());
        });
}

void DancingMatrix::rebindTreeReplacementProvider(
    splaytree::EulerTourTree* tree, ThreadLocalState* targetState) {
    if (!tree) return;
    if (targetState && targetState->graphContext) {
        bindTreeReplacementProvider(tree, targetState->graphContext);
    } else {
        bindGlobalTreeReplacementProvider(tree);
    }
}

void DancingMatrix::cleanupThreadLocalState() {
    if (tlsState) {
        tlsState.reset();
    }
}


// 将矩阵的行映射为无向图的顶点，构建邻接表存储所有边
void DancingMatrix::buildGraphFromMatrix() {

    std::unordered_set<splaytree::Edge, splaytree::EdgeHash> temp_edges;

    for (const auto& [col, rows] : col_to_rows) {
        if (rows.size() <= 1) continue;

        for (size_t i = 0; i < rows.size(); ++i) {
            for (size_t j = i + 1; j < rows.size(); ++j) {
                int u = rows[i];
                int v = rows[j];
                
                // 确保 u < v，将无向边标准化
                if (u > v) std::swap(u, v);
                
                temp_edges.emplace(u, v);
            }
        }
    }

    for (const auto& edge : temp_edges) {
        // temp_edges already guarantees uniqueness.  Avoid Graph::addEdge's
        // duplicate lookup, which is quadratic on dense projected graphs.
        graph->addUniqueEdge(edge.u, edge.v);
    }

    // graph->printGraph();
}

std::vector<int> DancingMatrix::implicitProjectedNeighbors(int row) const {
    return implicitProjectedNeighborsInRows(
        row, dynamicBlockRows_, graph.get());
}

std::vector<int> DancingMatrix::implicitProjectedNeighborsInRows(
    int row, const std::unordered_set<int>& candidateRows,
    const Graph* graph) const {
    std::vector<int> neighbors;
    if (!graph || !graph->hasVertex(row) || !candidateRows.count(row)) {
        return neighbors;
    }

    std::unordered_set<int> uniqueNeighbors;
    auto rowIt = row_to_cols.find(row);
    if (rowIt == row_to_cols.end()) return neighbors;

    for (int col : rowIt->second) {
        auto colIt = col_to_rows.find(col);
        if (colIt == col_to_rows.end()) continue;
        for (int other : colIt->second) {
            if (other == row || !candidateRows.count(other) ||
                !graph->hasVertex(other)) {
                continue;
            }
            uniqueNeighbors.insert(other);
        }
    }

    neighbors.assign(uniqueNeighbors.begin(), uniqueNeighbors.end());
    return neighbors;
}

void DancingMatrix::prepareDynamicCCForBlock(const Block& block) {
    releaseDynamicCCStorage();
    if (block.rows.empty() || block.cols.empty()) return;

    dynamicBlockRows_.insert(block.rows.begin(), block.rows.end());
    graph = std::make_unique<Graph>(ROWS);
    graph->setNeighborProvider(
        [this](int row) { return implicitProjectedNeighbors(row); });
    buildSpanningForestForRows(block.rows);
}

void DancingMatrix::rebuildGlobalDynamicCCForActiveBlock(
    const Block& block) {
    // Parallel workers use block-local projected graphs. After they finish,
    // the matrix has been backtracked to the parent state, so rebuild one
    // global forest that can safely be updated by the parent frame.
    const auto eligibleRows = dynamicBlockRows_;
    releaseDynamicCCStorage();

    if (block.rows.empty() || block.cols.empty()) return;

    dynamicBlockRows_ = eligibleRows;
    dynamicBlockRows_.insert(block.rows.begin(), block.rows.end());
    graph = std::make_unique<Graph>(ROWS);
    graph->setNeighborProvider(
        [this](int row) { return implicitProjectedNeighbors(row); });

    for (int row : dynamicBlockRows_) {
        if (!block.rows.count(row)) {
            graph->deleteVertex(row);
        }
    }
    buildSpanningForestForRows(block.rows);
}

void DancingMatrix::releaseDynamicCCStorage() {
    components.clear();
    graph.reset();
    dynamicBlockRows_.clear();
    activeSubgraph_ = nullptr;
    nextTreeId = 0;
}

void DancingMatrix::releaseThreadLocalDynamicCCStorage() {
    if (!tlsState) return;
    tlsState->components.clear();
    tlsState->graphContext.reset();
    tlsState->subgraph = nullptr;
    tlsState->activeSubgraph = nullptr;
    tlsState->nextTreeId = 0;
    tlsState->dynamicActiveVertices = 0;
    tlsState->dynamicActiveEdges = 0;
    tlsState->dynamicMetricsValid = false;
    tlsState->initialized = true;
}

std::vector<std::unique_ptr<splaytree::EulerTourTree>>
DancingMatrix::cloneThreadLocalDynamicCCForest() const {
    std::vector<std::unique_ptr<splaytree::EulerTourTree>> snapshot;
    if (!tlsState) return snapshot;
    snapshot.reserve(tlsState->components.size());
    for (const auto& tree : tlsState->components) {
        if (tree) snapshot.push_back(tree->clone());
    }
    return snapshot;
}

void DancingMatrix::restoreThreadLocalDynamicCCSnapshot(
    const Block& block,
    const std::vector<std::unique_ptr<
        splaytree::EulerTourTree>>& snapshot) {
    if (!tlsState) {
        tlsState = std::make_unique<ThreadLocalState>();
    }
    releaseThreadLocalDynamicCCStorage();
    if (block.rows.empty() || block.cols.empty()) return;

    auto context = std::make_shared<TreeGraphContext>();
    context->blockRows.insert(block.rows.begin(), block.rows.end());
    context->blockRows.insert(tlsState->ettEligibleRows.begin(),
                              tlsState->ettEligibleRows.end());
    context->graph = std::make_unique<Graph>(ROWS);

    for (int row : context->blockRows) {
        const auto rowIt = row_to_cols.find(row);
        if (rowIt == row_to_cols.end()) continue;
        for (int col : rowIt->second) {
            const auto colIt = col_to_rows.find(col);
            if (colIt == col_to_rows.end()) continue;
            for (int other : colIt->second) {
                if (row < other && context->blockRows.count(other)) {
                    context->graph->addUniqueEdge(row, other);
                }
            }
        }
    }
    for (int row : tlsState->ettEligibleRows) {
        if (!block.rows.count(row)) {
            context->graph->deleteVertex(row);
        }
    }
    tlsState->graphContext = context;

    tlsState->nextTreeId = 0;
    for (const auto& source : snapshot) {
        if (!source) continue;
        auto tree = source->clone();
        tree->setReplacementNeighborProvider({});
        const int anyVertex = tree->getAnyVertex();
        if (anyVertex >= 0) {
            const auto vertices = tree->getVertices();
            context->graph->registerComponent(
                tlsState->nextTreeId, vertices);
            if (!tlsState->subgraph) {
                tlsState->subgraph =
                    context->graph->subgraphOf(anyVertex);
                tlsState->activeSubgraph = tlsState->subgraph;
            }
        }
        tlsState->nextTreeId =
            std::max(tlsState->nextTreeId,
                     tree->getTreeId() + 1);
        tlsState->components.push_back(std::move(tree));
    }
}

void DancingMatrix::prepareThreadLocalDynamicCCForBlock(
    const Block& block, bool explicitProjectedEdges,
    bool initializeProjectedMetrics) {
    if (!tlsState) {
        tlsState = std::make_unique<ThreadLocalState>();
    }
    releaseThreadLocalDynamicCCStorage();

    if (block.rows.empty() || block.cols.empty()) return;

    auto context = std::make_shared<TreeGraphContext>();
    context->blockRows.insert(block.rows.begin(), block.rows.end());
    context->blockRows.insert(tlsState->ettEligibleRows.begin(),
                              tlsState->ettEligibleRows.end());
    context->graph = std::make_unique<Graph>(ROWS);
    TreeGraphContext* rawContext = context.get();
    if (explicitProjectedEdges) {
        for (int row : rawContext->blockRows) {
            const auto rowIt = row_to_cols.find(row);
            if (rowIt == row_to_cols.end()) continue;
            for (int col : rowIt->second) {
                const auto colIt = col_to_rows.find(col);
                if (colIt == col_to_rows.end()) continue;
                for (int other : colIt->second) {
                    if (row < other &&
                        rawContext->blockRows.count(other)) {
                        context->graph->addUniqueEdge(row, other);
                    }
                }
            }
        }
    } else {
        context->graph->setNeighborProvider(
            [this, rawContext](int row) {
                return implicitProjectedNeighborsInRows(
                    row, rawContext->blockRows, rawContext->graph.get());
            });
    }
    for (int row : tlsState->ettEligibleRows) {
        if (!block.rows.count(row)) {
            context->graph->deleteVertex(row);
        }
    }
    tlsState->graphContext = context;

    std::unordered_set<int> visited;
    std::queue<int> queue;
    uint64_t projectedDegreeSum = 0;
    for (int start : block.rows) {
        if (visited.count(start)) continue;

        std::unordered_set<int> componentRows;
        std::vector<splaytree::Edge> treeEdges;
        visited.insert(start);
        componentRows.insert(start);
        queue.push(start);

        while (!queue.empty()) {
            const int u = queue.front();
            queue.pop();
            const vector<int> neighbors =
                explicitProjectedEdges
                    ? rawContext->graph->getNeighbors(u)
                    : implicitProjectedNeighborsInRows(
                          u, rawContext->blockRows,
                          rawContext->graph.get());
            if (collectCCExperimentStats &&
                initializeProjectedMetrics) {
                projectedDegreeSum += neighbors.size();
            }
            for (int v : neighbors) {
                if (!visited.insert(v).second) continue;
                componentRows.insert(v);
                treeEdges.emplace_back(u, v);
                queue.push(v);
            }
        }

        context->graph->registerComponent(tlsState->nextTreeId, componentRows);

        std::unordered_map<int, std::unique_ptr<splaytree::EulerTourTree>>
            vertexTrees;
        std::unordered_map<int, int> representative;
        for (int vertex : componentRows) {
            auto tree = std::make_unique<splaytree::EulerTourTree>(
                tlsState->nextTreeId++);
            tree->addVertex(vertex);
            representative[vertex] = vertex;
            vertexTrees[vertex] = std::move(tree);
        }

        std::function<int(int)> findRepresentative = [&](int vertex) -> int {
            int root = vertex;
            while (representative[root] != root) root = representative[root];
            while (representative[vertex] != vertex) {
                const int next = representative[vertex];
                representative[vertex] = root;
                vertex = next;
            }
            return root;
        };

        for (const auto& edge : treeEdges) {
            const int rootU = findRepresentative(edge.u);
            const int rootV = findRepresentative(edge.v);
            if (rootU == rootV) continue;
            auto& treeU = vertexTrees[rootU];
            auto& treeV = vertexTrees[rootV];
            if (!treeU || !treeV) continue;
            treeU->link(edge.u, edge.v, treeV.get());
            representative[rootV] = rootU;
            vertexTrees[rootV].reset();
        }

        for (auto& [vertex, tree] : vertexTrees) {
            (void)vertex;
            if (!tree) continue;
            if (explicitProjectedEdges) {
                std::unordered_set<splaytree::Edge, splaytree::EdgeHash>
                    treeEdgeSet(treeEdges.begin(), treeEdges.end());
                for (int u : componentRows) {
                    for (int v : context->graph->getNeighbors(u)) {
                        if (u < v &&
                            !treeEdgeSet.count(splaytree::Edge(u, v))) {
                            tree->addNonTreeEdge(splaytree::Edge(u, v));
                        }
                    }
                }
            } else {
                bindTreeReplacementProvider(tree.get(), context);
            }
            tlsState->components.push_back(std::move(tree));
            break;
        }
    }

    if (!tlsState->components.empty()) {
        const int anyVertex = tlsState->components.front()->getAnyVertex();
        if (anyVertex >= 0) {
            tlsState->subgraph =
                context->graph->subgraphOf(anyVertex);
            tlsState->activeSubgraph = tlsState->subgraph;
        }
    }

    if (collectCCExperimentStats &&
        initializeProjectedMetrics) {
        tlsState->dynamicActiveVertices = block.rows.size();
        tlsState->dynamicActiveEdges = projectedDegreeSum / 2;
        tlsState->dynamicMetricsValid = true;
    }

}

void DancingMatrix::buildSpanningForestForRows(const std::set<int>& rows) {
    std::unordered_set<int> visited;
    std::queue<int> queue;

    for (int start : rows) {
        if (visited.count(start)) continue;

        std::unordered_set<int> componentRows;
        std::vector<splaytree::Edge> treeEdges;
        visited.insert(start);
        componentRows.insert(start);
        queue.push(start);

        while (!queue.empty()) {
            int u = queue.front();
            queue.pop();
            for (int v : implicitProjectedNeighbors(u)) {
                if (!visited.insert(v).second) continue;
                componentRows.insert(v);
                treeEdges.emplace_back(u, v);
                queue.push(v);
            }
        }

        int componentId = nextTreeId;
        graph->registerComponent(componentId, componentRows);

        std::unordered_map<int, std::unique_ptr<splaytree::EulerTourTree>>
            vertexTrees;
        std::unordered_map<int, int> representative;
        for (int vertex : componentRows) {
            auto tree = std::make_unique<splaytree::EulerTourTree>(
                getNextTreeId()++);
            tree->addVertex(vertex);
            vertexTrees[vertex] = std::move(tree);
            representative[vertex] = vertex;
        }

        std::function<int(int)> findRepresentative = [&](int vertex) -> int {
            int root = vertex;
            while (representative[root] != root) root = representative[root];
            while (representative[vertex] != vertex) {
                int next = representative[vertex];
                representative[vertex] = root;
                vertex = next;
            }
            return root;
        };

        for (const auto& edge : treeEdges) {
            int rootU = findRepresentative(edge.u);
            int rootV = findRepresentative(edge.v);
            if (rootU == rootV) continue;
            auto& treeU = vertexTrees[rootU];
            auto& treeV = vertexTrees[rootV];
            if (!treeU || !treeV) continue;
            treeU->link(edge.u, edge.v, treeV.get());
            representative[rootV] = rootU;
            vertexTrees[rootV].reset();
        }

        for (auto& [vertex, tree] : vertexTrees) {
            if (!tree) continue;
            tree->setReplacementNeighborProvider(
                [this](int row, std::unordered_set<int>& out) {
                    std::vector<int> neighbors = implicitProjectedNeighbors(row);
                    out.insert(neighbors.begin(), neighbors.end());
                });
            components.push_back(std::move(tree));
            break;
        }
    }
}

std::vector<splaytree::Edge> DancingMatrix::bfsSpanningTree(int start, std::unordered_set<int>& visited, std::unordered_set<int>& comp_vertices) {
    std::vector<splaytree::Edge> treeEdges;
    std::queue<int> q;
    q.push(start);
    visited.insert(start);
    comp_vertices.insert(start);
    
    while (!q.empty()) {
        int u = q.front();
        q.pop();
        
        for (int v : graph->getNeighbors(u)) {
            if (!visited.count(v)) {
                visited.insert(v);
                comp_vertices.insert(v);
                treeEdges.push_back(splaytree::Edge(u, v));
                q.push(v);
            }
        }
    }
    
    return treeEdges;
}

// 由无向图得到初始生成森林，基于生成树构建Euler Tour Trees
void DancingMatrix::buildSpanningForest() {
    std::unordered_set<int> visited;
    
    for (int i = 0; i < ROWS; i++) {
        if (visited.count(i)) continue;
        
        // BFS构建生成树,得到树边集合及分量顶点集合
        unordered_set<int> comp_vertices;
        std::vector<splaytree::Edge> treeEdges = bfsSpanningTree(i, visited, comp_vertices);

        int compId = nextTreeId;
        graph->registerComponent(compId, comp_vertices);
        
        // 为每个顶点创建单独的树
        std::unordered_map<int, std::unique_ptr<splaytree::EulerTourTree>> vertexTrees;
        // 映射：顶点 -> 当前所属树的代表顶点
        std::unordered_map<int, int> vertexToRepresentative;

        for (int v : comp_vertices) {
            auto vTree = std::make_unique<splaytree::EulerTourTree>(nextTreeId++);
            vTree->addVertex(v);
            vertexTrees[v] = std::move(vTree);
            vertexToRepresentative[v] = v;  // 初始时每个顶点代表自己
        }

        // 查找顶点当前所属树的代表顶点（带路径压缩）
        std::function<int(int)> findRepresentative = [&](int v) -> int {
            if (vertexToRepresentative[v] != v) {
                vertexToRepresentative[v] = findRepresentative(vertexToRepresentative[v]);
            }
            return vertexToRepresentative[v];
        };
        
        // 连接树边，逐步合并树
        for (const splaytree::Edge& e : treeEdges) {
            int repU = findRepresentative(e.u);
            int repV = findRepresentative(e.v);
            
            // if (repU == repV) {
            //     std::cout << "Warning: Edge (" << e.u << ", " << e.v 
            //               << ") forms a cycle, skipping.\n";
            //     continue;
            // }
            
            auto& uTree = vertexTrees[repU];
            auto& vTree = vertexTrees[repV];
            
            if (uTree && vTree) {
                // std::cout << "Linking edge (" << e.u << ", " << e.v << ")\n";
                uTree->link(e.u, e.v, vTree.get());
                vertexToRepresentative[repV] = repU;
                vertexTrees[repV].reset();
                // std::cout << "  Merged tree " << repV << " into tree " << repU << "\n";
            }
        }
        
        // 找到最终的树（未被reset的那个）
        splaytree::EulerTourTree* finalTree = nullptr;
        int finalRep = -1;

        for (auto& [v, tree] : vertexTrees) {
            if (tree) {
                finalTree = tree.get();
                finalRep = v;

                // 添加非树边
                std::unordered_set<splaytree::Edge, splaytree::EdgeHash> treeEdgeSet(treeEdges.begin(), treeEdges.end());
                
                for (int u : comp_vertices) {
                    for (int v : graph->getNeighbors(u)) {
                        // 只处理 u < v 的情况，避免 (u,v) 和 (v,u) 重复计算
                        // 确认 v 也在当前连通分量中
                        if (u < v && comp_vertices.count(v)) {
                            splaytree::Edge candidate(u, v);
                            
                            // 3. 如果这条边不是树边，那就是非树边
                            if (!treeEdgeSet.count(candidate)) {
                                tree->addNonTreeEdge(candidate);
                            }
                        }
                    }
                }

                components.push_back(std::move(tree)); // 保存生成的树
                break;
            }
        }

        if (!finalTree) std::cerr << "Error: No final tree found for component starting at vertex " << i << "\n";   
    }
}

splaytree::EulerTourTree* DancingMatrix::findEulerTourTree(int v) {
    auto& comps = getComponents();
    
    for (auto& tree : comps) {
        if (tree && tree->containsVertex(v)) {
            return tree.get();
        }
    }
    return nullptr;
}

bool DancingMatrix::processBoundaryVertex(int v, splaytree::EulerTourTree* tree, SubGraph* g){
    // 处理所有相邻边(包含非树边)
    std::vector<int> neighbors = g->neighbors(v);
    const bool implicitEdges = g->usesImplicitEdges();
    
    // 边界顶点：删除所有非树边，无需寻找替代边
    // std::cout << "Removing boundary vertex " << v << "\n";
    int treeEdgeNeighbor = tree->getBoundaryVertexTreeNeighbor(v);
   
    if (treeEdgeNeighbor < 0) {
        if (implicitEdges) g->deleteVertex(v);
        // 只需删除所有非树边（如果还有的话），然后移除顶点
        for (int u : neighbors) {
            splaytree::Edge e(v, u);
            if (tree->hasNonTreeEdge(e)) tree->removeNonTreeEdge(e);
            g->deleteEdge(v, u);
        }
        tree->removeVertex(v);
        return false;
    }

    if (implicitEdges) g->deleteVertex(v);

    // 删除非树边
    for (int u : neighbors) {
        if (u != treeEdgeNeighbor) {
            splaytree::Edge e(v, u);
            tree->removeNonTreeEdge(e);
            g->deleteEdge(v, u);
        }
    }
    
    // 删除唯一的树边
    tree->cutBoundary(v, treeEdgeNeighbor);
    g->deleteEdge(v, treeEdgeNeighbor);
    
    // 删除顶点
    tree->removeVertex(v);
    return true;
}

// 减量式更新单连通分量
void DancingMatrix::DecUpdateCC(
    const std::set<int>& deletedVertices,
    const uint64_t* deletedEntries,
    CCUpdateStats* updateStats) {

    uint64_t deletedVertexCount = 0;
    uint64_t activeVertexCount = 0;
    uint64_t activeEdgeCount = 0;
    uint64_t deletedEdgeCount = 0;
    uint64_t treeEdgeCutCount = 0;
    uint64_t treeEdgeLinkCount = 0;
    uint64_t replacementScanSteps = 0;
    uint64_t boundaryEdgeCount = 0;

    if (updateStats) *updateStats = CCUpdateStats{};

    // if (!isGraphSyncEnabled()) return;
    if (deletedVertices.empty()) return;

    auto& comps = getComponents();
    auto* g = getGraph();

    if (!g) {
        return;
    }

    if (comps.empty()) return;
    
    const bool useIncrementalMetricCounters =
        isThreadLocal() && tlsState && tlsState->dynamicMetricsValid;
    if (collectCCExperimentStats) {
        if (useIncrementalMetricCounters) {
            activeVertexCount = tlsState->dynamicActiveVertices;
            activeEdgeCount = tlsState->dynamicActiveEdges;

            for (int v : deletedVertices) {
                if (!findEulerTourTree(v)) continue;
                ++deletedVertexCount;
            }
            if (deletedEntries) {
                deletedEdgeCount = *deletedEntries;
                boundaryEdgeCount = deletedEdgeCount;
            } else {
                for (int v : deletedVertices) {
                    if (!findEulerTourTree(v)) continue;
                    for (int u : g->neighbors(v)) {
                        const bool otherDeleted =
                            deletedVertices.count(u) > 0;
                        if (!otherDeleted) {
                            ++boundaryEdgeCount;
                        }
                        if (!otherDeleted || v < u) {
                            ++deletedEdgeCount;
                        }
                    }
                }
            }
        } else {
            std::unordered_set<int> activeVertices;
            for (auto& tree : comps) {
                if (!tree) continue;
                activeVertices.insert(
                    tree->vertices.begin(), tree->vertices.end());
            }
            activeVertexCount = activeVertices.size();

            // Fallback for paths that did not initialize local counters.
            // This is only used outside the Hybrid ETT experiment path.
            for (int v : activeVertices) {
                for (int u : g->neighbors(v)) {
                    if (v < u && activeVertices.count(u)) {
                        ++activeEdgeCount;
                    }
                }
            }

            for (int v : deletedVertices) {
                if (!activeVertices.count(v)) continue;
                ++deletedVertexCount;
                for (int u : g->neighbors(v)) {
                    if (!activeVertices.count(u)) continue;
                    const bool otherDeleted =
                        deletedVertices.count(u) > 0;
                    if (!otherDeleted && g->hasVertex(u)) {
                        ++boundaryEdgeCount;
                    }
                    if (!otherDeleted || v < u) {
                        ++deletedEdgeCount;
                    }
                }
            }
        }
    }

    std::vector<int> boundaryVertices;
    std::vector<int> otherVertices;
    boundaryVertices.reserve(deletedVertices.size());
    otherVertices.reserve(deletedVertices.size());

    for (int v : deletedVertices) {
        splaytree::EulerTourTree* tree = findEulerTourTree(v);
        if (!tree) continue;

        int treeEdgeCount = tree->getVertexDegree(v);
        if (treeEdgeCount == 1) {
            boundaryVertices.push_back(v);
        } else {
            otherVertices.push_back(v);
        }
    }

    // 先删边界点。每个顶点都重新查询所属树，因为前一个删除可能已导致分裂。
    for (int v : boundaryVertices) {
        splaytree::EulerTourTree* tree = findEulerTourTree(v);
        if (!tree) continue;
        if (processBoundaryVertex(v, tree, g) && collectCCExperimentStats) {
            ++treeEdgeCutCount;
        }
    }

    // 再处理非边界点；同样不假设当前线程只有 comps[0]。
    for (int v : otherVertices) {

        // 有可能出现分裂
        splaytree::EulerTourTree* tree = findEulerTourTree(v);
        if (!tree) continue; 

        // 处理所有相邻边
        std::vector<int> neighbors = g->neighbors(v);
        int currentDegree = tree->getVertexDegree(v);
        const bool implicitEdges = g->usesImplicitEdges();

        if (currentDegree == 0) {
            if (implicitEdges) g->deleteVertex(v);
            // 已完全孤立，直接移除
            tree->removeVertex(v);
            continue;
        }
        // 原本非边界顶点，可能变成边界顶点
        if (currentDegree == 1) {
            if (processBoundaryVertex(v, tree, g) && collectCCExperimentStats) {
                ++treeEdgeCutCount;
            }
            continue;
        }
        
        // 仍是非边界顶点
        // 收集树边邻居和非树边邻居
        std::vector<int> treeNeighbors;
        std::vector<int> nonTreeNeighbors;
        
        for (int u : neighbors) {
            if (tree->isTreeEdge(v, u)) {
                treeNeighbors.push_back(u);
            } else {
                nonTreeNeighbors.push_back(u);
            }
        }
        
        // 删除所有非树边
        for (int u : nonTreeNeighbors) {
            splaytree::Edge e(v, u);
            if (tree->hasNonTreeEdge(e)) {
                tree->removeNonTreeEdge(e);
            }
            g->deleteEdge(v, u);
        }

        if (implicitEdges) g->deleteVertex(v);

        // 删除所有树边
        for (int u : treeNeighbors) {
            splaytree::EulerTourTree* currentTree = findEulerTourTree(v);
            if (!currentTree) {
                break;
            }
            if (!currentTree->isTreeEdge(v, u)) continue;

            splaytree::ReplacementSearchMetrics searchMetrics;
            auto newTree = currentTree->cutWithReplacement(
                v, u, collectCCExperimentStats ? &searchMetrics : nullptr);
            if (collectCCExperimentStats && searchMetrics.searched) {
                replacementScanSteps += searchMetrics.scanSteps;
            }
            if (collectCCExperimentStats) {
                ++treeEdgeCutCount;
                if (searchMetrics.found) {
                    ++treeEdgeLinkCount;
                }
            }
            if (newTree) {
                comps.push_back(std::move(newTree));
            }
            
            g->deleteEdge(v, u);
        }

        splaytree::EulerTourTree* finalTree = findEulerTourTree(v);
        if (finalTree) {
            finalTree->removeVertex(v);
        }
    }

    for (int v : deletedVertices) {
        if (g->hasVertex(v)) g->deleteVertex(v);
    }
    
    // 更新连通分量
    comps.erase(
        std::remove_if(comps.begin(), comps.end(),
            [](const std::unique_ptr<splaytree::EulerTourTree>& t) { 
                return !t || t->isEmpty(); 
            }),
        comps.end()
    );

    if (collectCCExperimentStats) {
        if (useIncrementalMetricCounters) {
            tlsState->dynamicActiveVertices -=
                std::min(tlsState->dynamicActiveVertices,
                         deletedVertexCount);
            tlsState->dynamicActiveEdges -=
                std::min(tlsState->dynamicActiveEdges,
                         boundaryEdgeCount);
        }
        std::lock_guard<std::mutex> lock(ccExperimentStatsMutex);
        ++ccExperimentStats.decCalls;
        ccExperimentStats.activeVertexSum += activeVertexCount;
        ccExperimentStats.activeEdgeSum += activeEdgeCount;
        ccExperimentStats.deletedEdgeSum += deletedEdgeCount;
        ccExperimentStats.deletedTreeEdgeSum += treeEdgeCutCount;
        ccExperimentStats.deletedNonTreeEdgeSum +=
            deletedEdgeCount > treeEdgeCutCount
                ? deletedEdgeCount - treeEdgeCutCount
                : 0;
        ccExperimentStats.treeEdgeCutSum += treeEdgeCutCount;
        ccExperimentStats.treeEdgeLinkSum += treeEdgeLinkCount;
        ccExperimentStats.deletedVertexSum += deletedVertexCount;
        ccExperimentStats.replacementCheckSum += replacementScanSteps;
    }

    if (updateStats) {
        updateStats->activeVertices = activeVertexCount;
        updateStats->activeEdges = activeEdgeCount;
        updateStats->deletedVertices = deletedVertexCount;
        updateStats->deletedEdges = deletedEdgeCount;
        updateStats->deletedTreeEdges = treeEdgeCutCount;
        updateStats->deletedNonTreeEdges =
            deletedEdgeCount > treeEdgeCutCount
                ? deletedEdgeCount - treeEdgeCutCount
                : 0;
        updateStats->treeEdgeLinks = treeEdgeLinkCount;
        updateStats->replacementScanSteps = replacementScanSteps;
    }
}

void DancingMatrix::IncUpdateCC(const std::set<int>& restoredVertices) {
    if (restoredVertices.empty()) return;

    auto& comps = getComponents();
    auto* g = getGraph();
    if (!g && currentGraph() && !restoredVertices.empty()) {
        g = currentGraph()->subgraphOf(*restoredVertices.begin());
    }
    if (!g) {
        return;
    }

    std::unordered_set<int> restoredSet(restoredVertices.begin(), restoredVertices.end());
    for (int v : restoredVertices) {
        if (!g->hasVertex(v)) {
            g->restoreVertex(v);
        }
    }

    for (int v : restoredVertices) {
        if (findEulerTourTree(v)) continue;
        auto newTree = std::make_unique<splaytree::EulerTourTree>(
            getNextTreeId()++);
        newTree->addVertex(v);
        comps.push_back(std::move(newTree));
    }
    
    for (int v : restoredVertices) {
        std::vector<int> neighbors = g->getAllNeighbors(v); 

        for (int u : neighbors) {

            splaytree::EulerTourTree* treeU = findEulerTourTree(u);
            const bool uActive = (treeU != nullptr) || restoredSet.count(u);
            if (!uActive) continue;

            if (restoredSet.count(u) && v > u) continue;

            if (!g->hasEdge(v, u)) {
                g->restoreEdge(v, u);
            }
            treeU = findEulerTourTree(u);
            if (!treeU) continue;
            splaytree::EulerTourTree* treeV = findEulerTourTree(v); 
            if (!treeV) continue;

            if (treeU != treeV) {
                treeU->link(u, v, treeV);
            } else if (!treeU->usesImplicitNonTreeEdges()) {
                treeU->addNonTreeEdge(splaytree::Edge(v, u));
            }
        }
    }
    
    comps.erase(
        std::remove_if(comps.begin(), comps.end(),
            [](const std::unique_ptr<splaytree::EulerTourTree>& t) { return t->isEmpty(); }),
        comps.end()
    );

}

std::vector<std::unordered_set<int>> DancingMatrix::getConnectedComponents() const {
    std::vector<std::unordered_set<int>> result;
    for (const auto& tree : components) {
        if (!tree->isEmpty()) {
            result.push_back(tree->getVertices());
        }
    }
    return result;
}

void DancingMatrix::printComponents() {
    auto& comps = getComponents();
    std::cout << "Number of connected components: " << comps.size() << "\n";
    int idx = 1;
    for (const auto& tree : comps) {
        if (!tree->isEmpty()) {
            std::cout << "Component " << idx++ << " (Tree ID: " << tree->getTreeId() << "): ";
            tree->printEulerTour();
            std::cout << "\n";
        }
    }
    std::cout << "============================" << std::endl;
}

void DancingMatrix::testDynamicUpdateCC(const std::vector<int>& deletedVertices) {
    std::cout << "Before Decremental Update:\n";
    printComponents();

    set<int> deleted_set(deletedVertices.begin(), deletedVertices.end());

    DecUpdateCC(deleted_set);

    std::cout << "After Decremental Update:\n";
    printComponents();

    IncUpdateCC(deleted_set);

    std::cout << "After Incremental Update:\n";
    printComponents();
}

void DancingMatrix::testCutEdge(int u, int v) {
    
    splaytree::EulerTourTree* tree = findEulerTourTree(u);
    if (!tree) {
        std::cout << "Error: Vertex " << u << " not found in any component.\n";
        return;
    }
    
    std::cout << "Before cutting edge (" << u << ", " << v << "):\n";
    tree->printEulerTour();
    
    int treeId1 = nextTreeId++;
    int treeId2 = nextTreeId++;

    // 接收返回值
    auto [newTree1, newTree2] = tree->cut(u, v, treeId1, treeId2);
    
    // 检查是否成功
    if (!newTree1 || !newTree2) {
        std::cout << "Error: Failed to cut edge (" << u << ", " << v << ").\n";
        std::cout << "This edge may not exist or is not a tree edge.\n";
        return;
    }
    
    std::cout << "Successfully cut edge (" << u << ", " << v << ").\n";

    // ========== 更新 components ==========
    auto it = std::find_if(components.begin(), components.end(),
                          [tree](const auto& ptr) { return ptr.get() == tree; });
    
    if (it != components.end()) {
        components.erase(it);  // 这会自动调用 unique_ptr 的析构
    }
    
    splaytree::EulerTourTree* tree1Ptr = newTree1.get();
    splaytree::EulerTourTree* tree2Ptr = newTree2.get();
    
    components.push_back(std::move(newTree1));  // newTree1 现在是 nullptr
    components.push_back(std::move(newTree2));  // newTree2 现在是 nullptr
    
    std::cout << "After cutting edge (" << u << ", " << v << "):\n";
    printComponents();

    tree1Ptr->link(u, v, tree2Ptr);
    std::cout << "After linking back edge (" << u << ", " << v << "):\n";
    tree1Ptr->printEulerTour();
}

vector<Block> DancingMatrix::getComponentsByBFS(
    const set<int>& target_cols, BFSScanMetrics* metrics) {
    vector<Block> components;
    
    if (target_cols.empty()) {
        return components;
    }


    // 局部变量，各线程独立持有，保证线程安全
    vector<bool> visited_cols(COLS + 1, false);
    vector<bool> visited_rows(ROWS, false);
    
    // 只在限制的 target_cols 集合内启动 BFS
    for (int start_col : target_cols) {
        // 如果该列尚未被访问，启动 BFS
        if (!visited_cols[start_col]) {
            Block block;
            queue<int> col_queue;
            
            // 初始化 BFS 队列
            visited_cols[start_col] = true;
            if (metrics) ++metrics->vertices;
            col_queue.push(start_col);
            
            while (!col_queue.empty()) {
                int c = col_queue.front();
                col_queue.pop();
                
                // 将当前列加入连通块
                block.cols.insert(c);
                
                // 沿着当前列向下遍历
                Node* node_in_col = ColIndex[c].down;
                while (node_in_col != &ColIndex[c]) {
                    if (metrics) ++metrics->graphEdges;
                    ++block.activeEntries;
                    int r = node_in_col->row; 
                    
                    if (!visited_rows[r]) {
                        visited_rows[r] = true;
                        if (metrics) ++metrics->vertices;
                        block.rows.insert(r); 
                        
                        // 沿着当前行向右遍历，寻找相连的列
                        Node* node_in_row = node_in_col->right;
                        while (node_in_row != node_in_col) {
                            int next_c = node_in_row->col;
                            
                            if (target_cols.count(next_c) && !visited_cols[next_c]) {
                                visited_cols[next_c] = true;
                                if (metrics) ++metrics->vertices;
                                col_queue.push(next_c);
                            }
                            node_in_row = node_in_row->right;
                        }
                    }
                    node_in_col = node_in_col->down;
                }
            }
            
            // 保存找到的独立子矩阵
            block.activeEntriesValid = true;
            components.push_back(block);
        }
    }
    
    return components;
}

DancingMatrix::IncidenceBfsResult
DancingMatrix::getComponentsByIncidenceBFS(
    const set<int>& target_cols) {
    IncidenceBfsResult result;
    if (target_cols.empty()) return result;

    std::unordered_map<int, int> parent;
    std::unordered_map<int, unsigned char> rank;
    parent.reserve(
        static_cast<std::size_t>(target_cols.size() + ROWS));
    rank.reserve(
        static_cast<std::size_t>(target_cols.size() + ROWS));

    auto ensure = [&](int vertex) {
        parent.emplace(vertex, vertex);
    };
    std::function<int(int)> find = [&](int vertex) -> int {
        auto it = parent.find(vertex);
        if (it == parent.end()) {
            ensure(vertex);
            return vertex;
        }
        int root = vertex;
        while (parent[root] != root) root = parent[root];
        while (parent[vertex] != vertex) {
            const int next = parent[vertex];
            parent[vertex] = root;
            vertex = next;
        }
        return root;
    };
    auto unite = [&](int lhs, int rhs) {
        lhs = find(lhs);
        rhs = find(rhs);
        if (lhs == rhs) return false;
        if (rank[lhs] < rank[rhs]) std::swap(lhs, rhs);
        parent[rhs] = lhs;
        if (rank[lhs] == rank[rhs]) ++rank[lhs];
        return true;
    };

    std::unordered_set<int> activeRows;
    std::unordered_set<int> activeColumns;
    activeColumns.reserve(target_cols.size());
    for (int column : target_cols) {
        const int columnVertex = ROWS + column;
        ensure(columnVertex);
        activeColumns.insert(column);
        ColumnHeader* header = getColumnHeader(column);
        for (Node* rowNode = header->down;
             rowNode != header; rowNode = rowNode->down) {
            const int row = rowNode->row;
            ensure(row);
            activeRows.insert(row);
            ++result.metrics.graphEdges;
            if (unite(row, columnVertex)) {
                result.treeEdges.push_back({row, column});
            } else {
                result.nonTreeEdges.push_back({row, column});
            }
        }
    }

    result.activeRows.assign(activeRows.begin(), activeRows.end());
    result.activeColumns.assign(
        activeColumns.begin(), activeColumns.end());
    std::sort(result.activeRows.begin(), result.activeRows.end());
    std::sort(result.activeColumns.begin(),
              result.activeColumns.end());
    result.metrics.vertices =
        result.activeRows.size() + result.activeColumns.size();

    struct ComponentData {
        set<int> rows;
        set<int> columns;
        uint64_t entries = 0;
    };
    std::unordered_map<int, ComponentData> componentsByRoot;
    for (int row : result.activeRows) {
        componentsByRoot[find(row)].rows.insert(row);
    }
    for (int column : result.activeColumns) {
        componentsByRoot[find(ROWS + column)].columns.insert(column);
    }
    for (int column : result.activeColumns) {
        ColumnHeader* header = getColumnHeader(column);
        const int root = find(ROWS + column);
        for (Node* rowNode = header->down;
             rowNode != header; rowNode = rowNode->down) {
            ++componentsByRoot[root].entries;
        }
    }

    result.components.reserve(componentsByRoot.size());
    for (auto& [root, data] : componentsByRoot) {
        (void)root;
        Block component;
        component.rows = std::move(data.rows);
        component.cols = std::move(data.columns);
        component.activeEntries = data.entries;
        component.activeEntriesValid = true;
        result.components.push_back(std::move(component));
    }
    std::sort(
        result.components.begin(), result.components.end(),
        [](const Block& lhs, const Block& rhs) {
            if (lhs.rows != rhs.rows) return lhs.rows < rhs.rows;
            return lhs.cols < rhs.cols;
        });
    return result;
}

vector<Block> DancingMatrix::getComponentsByETT(const set<int>& target_cols) {
    // 使用当前线程的 components；每个线程只持有当前递归块的 ETT 森林。
    auto& comps = getComponents();

    vector<Block> blocks;
    blocks.reserve(comps.size());
    for (const auto& tree : comps) {
        if (!tree || tree->isEmpty()) continue;

        unordered_set<int> comp_rows = tree->getVertices();
        set<int> block_rows(comp_rows.begin(), comp_rows.end());
        set<int> block_cols;
        uint64_t activeEntries = 0;

        for (int r : block_rows) {
            auto rowIt = row_to_cols.find(r);
            if (rowIt == row_to_cols.end()) continue;

            for (int c : rowIt->second) {
                if (target_cols.count(c)) {
                    block_cols.insert(c);
                    ++activeEntries;
                }
            }
        }

        if (!block_rows.empty() && !block_cols.empty()) {
            blocks.emplace_back(std::move(block_rows), std::move(block_cols));
            blocks.back().activeEntries = activeEntries;
            blocks.back().activeEntriesValid = true;
        }
    }
    return blocks;
};

void DancingMatrix::testReRoot(int v) {
    
    splaytree::EulerTourTree* tree = findEulerTourTree(v);
    if (!tree) {
        std::cout << "Error: Vertex " << v << " not found in any component.\n";
        return;
    }
    
    std::cout << "Before rerooting at vertex " << v << ":\n";
    tree->printEulerTour();
    
    tree->reroot(v);
    
    std::cout << "After rerooting at vertex " << v << ":\n";
    tree->printEulerTour();
}

void DancingMatrix::testSplay(int v) {

    splaytree::EulerTourTree* tree = findEulerTourTree(v);

    if (!tree) {
        std::cout << "Error: Vertex " << v << " not found in any component.\n";
        return;
    }
    
    tree->testSplay(v);
}

//插入元素到双向十字链表中
void DancingMatrix::insert( int r, int c )  
{  
    ColIndex[c].size++;  
    RowIndex[r].size++;
    // ColIndex[c].rows.insert(r);
    // RowIndex[r].cols.insert(c);
    auto newNode = std::make_unique<Node>(r, c);  
    Node* newNodePtr = newNode.get();

    // 1. 垂直方向插入 (直接插在列的末尾)
    // ColIndex[c].up 永远指向当前列的最后一个节点
    Node* colHead = &ColIndex[c];
    Node* colTail = colHead->up; 

    newNodePtr->down = colHead;
    newNodePtr->up = colTail;
    colTail->down = newNodePtr;
    colHead->up = newNodePtr;

    if( RowIndex[r].right == nullptr ) {  
        RowIndex[r].right = newNodePtr; 
        newNodePtr->row_first_node = true;
        newNode->left = newNodePtr;  
        newNode->right = newNodePtr;  
    } else {  
        Node* rowHead = RowIndex[r].right;  
        // rowHead->left 永远指向当前行的最后一个节点
        Node* rowTail = rowHead->left; 

        newNodePtr->right = rowHead;  
        newNodePtr->left = rowTail;  
        rowTail->right = newNodePtr;  
        rowHead->left = newNodePtr; 
    }  

    dataNodes.push_back(std::move(newNode));
}

size_t DancingMatrix::encodeColState(vector<int>* exactCols){
    if (exactCols) exactCols->clear();
    size_t state = 0;
    ColumnHeader* cur = (ColumnHeader*)root->right;
    while (cur != root) {
        state ^= std::hash<int>()(cur->col) + 0x9e3779b9 + (state << 6) + (state >> 2);
        if (exactCols) exactCols->push_back(cur->col);
        cur = (ColumnHeader*)cur->right;
    }
    return state;
}

size_t DancingMatrix::hashBlockState(const set<int>& cols,
                                     vector<int>* exactCols) {
    if (exactCols) {
        exactCols->clear();
        exactCols->reserve(cols.size());
    }
    size_t hash = 0;
    for(int col : cols) {
        hash ^= std::hash<int>()(col) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        if (exactCols) exactCols->push_back(col);
    }
    return hash;
}

//获取当前列的状态
size_t DancingMatrix::getColumnState() const {
    size_t hash = 0;
    ColumnHeader* cur = (ColumnHeader*)root->right;
    while (cur != root) {
        hash ^= std::hash<int>()(cur->col) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        cur = (ColumnHeader*)cur->right;
    }
    return hash;
}

// 创建当前状态的签名
Signature DancingMatrix::getColumnSignature() const {
    Signature sig;
    sig.covered.resize(COLS + 1, false);
    
    // 标记已覆盖的列
    for (int j = 1; j <= COLS; j++) {
        bool found = false;
        for (int k = root->right->col; k != 0; k = ColIndex[k].right->col) {
            if (k == j) {
                found = true;
                break;
            }
        }
        sig.covered[j] = !found;
    }
    
    return sig;
}

void DancingMatrix::build_mapping_from_cols(const unordered_set<int>& blockCols, unordered_map<int, set<int>>& rowToCols, unordered_map<int, set<int>>& colToRows)
{
    for (auto col : blockCols) {
        ColumnHeader* c = &ColIndex[col];
        Node* curR = c->down;
        while (curR != c) {
            rowToCols[curR->row].insert(col);
            colToRows[col].insert(curR->row);
            curR = curR->down;
        }
    }
}

void DancingMatrix::printMatrix() const
{
    std::cout<< "Remain Matrix Nodes: " << std::endl;
    ColumnHeader* current = (ColumnHeader*)root->right;
    while(current != root)
    {
        std::cout << "Column " << current->col << " size: " << current->size << " ";
        if(current->size > 0){
            Node* cur = current->down;
            std::cout << "{ Rows: ( ";
            while(cur != current)
            {
                std::cout << cur->row + 1;
                cur = cur->down;
                if(cur != current)
                    std::cout << ", ";
            }
            std::cout << " ) } " << std::endl;
        }
        current = (ColumnHeader*)current->right;
    }
    std::cout << std::endl;
}

void DancingMatrix::printBlocks( vector<Block>& blocks) const {
    int i = 1;
    for (auto& block : blocks) {
        block.printBlock(i++);
    }
}

void DancingMatrix::cover( int c )  
{  
    ColumnHeader* col = &ColIndex[c];  
    col->right->left = col->left;  
    col->left->right = col->right; 
    // colsSet.erase(c);

    Node* curR, *curC;  
    curC = col->down;  
    while( curC != col )  
    {   
        Node* noteR = curC;  

        curR = noteR->right;  
        while( curR != noteR )  
        {  
            curR->down->up = curR->up;  
            curR->up->down = curR->down;  
            --ColIndex[curR->col].size;  

            curR = curR->right;  
        }  
        // rowsSet.erase(curC->row);
        curC = curC->down;  
    }  
}

void DancingMatrix::uncover( int c )  
{  
    Node* curR, *curC;  
    ColumnHeader* col = &ColIndex[c];  
    curC = col->up;  
    while( curC != col )  
    {  
        Node* noteR = curC;  

        curR = curC->left;  
        while( curR != noteR )  
        {  
            ++ColIndex[curR->col].size;  
            curR->down->up = curR;  
            curR->up->down = curR;  

            curR = curR->left;  
        }  
        // rowsSet.insert(curC->row);
        curC = curC->up;  
    }  
    col->right->left = col;  
    col->left->right = col;  
    // colsSet.insert(c);
}

void DancingMatrix::coverInBlock(int c, Block& block, set<int>& removed_rows,
                                 bool maintainRows) {

    ColumnHeader* col = &ColIndex[c];  
    if (isDXZMode()) {
        col->right->left = col->left;
        col->left->right = col->right;
    }

    block.cols.erase(c); // 从块中移除列

    Node* curR, *curC;  
    curC = col->down;  

    while( curC != col )  
    {    
        int row_id = curC->row;

        if (maintainRows) {
            block.rows.erase(row_id);
            removed_rows.insert(row_id);
        } else if (block.trackBfsRows) {
            uint32_t& coverCount = block.rowCoverCounts[row_id];
            if (coverCount++ == 0 && block.activeRowCount > 0) {
                --block.activeRowCount;
            }
        }

        curR = curC->right;  
        while( curR != curC )  
        {          
            curR->down->up = curR->up;  
            curR->up->down = curR->down;  
            --ColIndex[curR->col].size;
            curR = curR->right;  
        }  

        curC = curC->down;  
    }
}

void DancingMatrix::uncoverInBlock(int c, Block& block, bool maintainRows) {
    ColumnHeader* col = &ColIndex[c];
    Node* curC = col->up; 

    while( curC != col )  
    {
        int row_id = curC->row;
 
        Node* curR = curC->left;  
        while( curR != curC )  
        {  
            ++ColIndex[curR->col].size;
            curR->down->up = curR;  
            curR->up->down = curR;  
            curR = curR->left;  
        }

        if (maintainRows) {
            block.rows.insert(row_id);
        } else if (block.trackBfsRows) {
            auto countIt = block.rowCoverCounts.find(row_id);
            if (countIt != block.rowCoverCounts.end() &&
                countIt->second > 0 &&
                --countIt->second == 0) {
                block.rowCoverCounts.erase(countIt);
                ++block.activeRowCount;
            }
        }

        curC = curC->up;  
    }  

    if (isDXZMode()) {
        col->right->left = col;
        col->left->right = col;
    }

    block.cols.insert(c);
}

ColumnHeader* DancingMatrix::selectColumnHeuristic(const set<int>& cols) {
    ColumnHeader* chosen = nullptr;
    int minSize = INT_MAX;

    for (int col : cols) {
        if (col < 1 || col > COLS) continue; // 跳过无效列
        int sz = getColSize(col);

        if (sz < minSize) {
            minSize = sz;
            chosen = getColumnHeader(col);
        }
    }
    return chosen;
}

ColumnHeader* DancingMatrix::selectColumnByLinear(const set<int>& cols, int threshold) {
    
    ColumnHeader* bestCol = getColumnHeader(*cols.begin()); // 保底选择第一列
    int bestSize = getColSize(bestCol->col);
    
    for (int colId : cols) {
        int size = getColSize(colId);
        
        if (size < bestSize) {
            bestCol = getColumnHeader(colId);
            bestSize = size;
        }
    }
    
    return bestCol; // 保证非空返回
}

ColumnHeader* DancingMatrix::selectColumnByMinHeap(const set<int>& cols, int threshold) {
    
    // 使用小根堆选择最小列
    priority_queue<ColumnInfo, vector<ColumnInfo>, greater<ColumnInfo>> minHeap;
    
    for (int colId : cols) {
        int size = getColSize(colId);
        minHeap.push({colId, size, getColumnHeader(colId)});
    }
    
    // 保底返回：记录第一个（最小size）列
    ColumnInfo firstCol = minHeap.top();
    ColumnHeader* bestCol = firstCol.header;
    int bestDistance = abs(firstCol.size - threshold);
    
    // 从小到大取出元素，寻找最接近阈值的列
    while (!minHeap.empty()) {
        ColumnInfo current = minHeap.top();
        minHeap.pop();
        
        int distance = abs(current.size - threshold);
        
        // 更新最优列
        if (distance < bestDistance) {
            bestDistance = distance;
            bestCol = current.header;
        }
        
        // 早期退出优化：
        // 1. 找到精确匹配
        if (distance == 0) break;
        
        // 2. 当前size已经超过阈值，且距离开始增大时停止
        //    因为后续元素size更大，距离只会越来越大
        if (current.size > threshold && distance > bestDistance) {
            break;
        }
    }
    
    return bestCol;  // 保证非空
}

ColumnHeader* DancingMatrix::selectOptimalColumn(const set<int>& cols) {
    
    // if (cols.size() <= HEAP_THRESHOLD) {
    //     return selectColumnByLinear(cols, TARGET_THRESHOLD);
    // }
    // if (single_thread_mode) {
    //     return selectColumnByMinHeap(cols, TARGET_THRESHOLD);
    // }

    return selectColumnByLinear(cols, TARGET_THRESHOLD);
}

ColumnHeader* DancingMatrix::selectCol()
{
    ColumnHeader* choose = getColumnHeader(root->right->col), *cur=choose;  
    while( cur != root )  
    {   //选择元素最少的列
        int chooseSize = getColSize(choose->col);
        int curSize = getColSize(cur->col);
        if( chooseSize > curSize )  
            choose = cur;  
        cur = getColumnHeader(cur->right->col);  
    } 
    return choose;
}

col_id DancingMatrix::getClosedSizeCol(const int expected_size) {
    ColumnHeader* choose = (ColumnHeader*)root->right, *cur=choose;  
    while( cur != root )  
    {   //选择接近预期大小的列
        if( abs(choose->size - expected_size) > abs(cur->size - expected_size) )  
            choose = cur;  
        cur = (ColumnHeader*)cur->right;  
    } 
    return choose->col;
}

col_id DancingMatrix::getSmallestSizeCol() {
    ColumnHeader* choose = (ColumnHeader*)root->right, *cur=choose;  
    while( cur != root )  
    {   //选择元素最少的列
        if( choose->size > cur->size )  
            choose = cur;  
        cur = (ColumnHeader*)cur->right;  
    } 
    return choose->col;
}

void PreProccess::extractNM(const std::string& line, int& n, int& m) {
    std::istringstream iss(line);
    std::string token;

    // 读取 "c"
    iss >> token;  // 读取 "c"

    iss >> token;  // 读取 "n"
    if (token == "n") {
        iss >> token;  // 读取 "="
        iss >> n;      // 读取 n 的值
    } else {
        throw std::runtime_error("格式错误: 未找到 'n'");
    }

    // 读取 "m" 和其值
    iss >> token;  // 读取 ","
    iss >> token;  // 读取 "m"
    if (token == "m") {
        iss >> token;  // 读取 "="
        iss >> m;      // 读取 m 的值
    } else {
        throw std::runtime_error("格式错误: 未找到 'm'");
    }
}

int** PreProccess::processFileToMatrix1(const std::string& filename, int& r, int& c) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("无法打开文件");
    }

    std::string line;
    std::getline(file, line);  // 读取第一行

    int n, m;
    extractNM(line, n, m);  // 读取第一行的 n 和 m

    r = m;
    c = n;

    std::getline(file, line);  // 跳过第二行(第二个数据集不用跳过)

    // 动态分配二维数组
    int** matrix = new int*[r];
    for (int i = 0; i < r; ++i) {
        matrix[i] = new int[c];
        for (int j = 0; j < c; ++j) {
            matrix[i][j] = 0;  // 初始化为0
        }
    }

    int row = 0;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] != 's') continue;  // 忽略不是以 's' 开头的行
        std::istringstream iss(line.substr(2));  // 跳过前两个字符's '
        //std::istringstream iss(line);
        int col;
        while (iss >> col) {
            if (col > 0 && col <= n) {  // 确保列索引在有效范围内
                matrix[row][col - 1] = 1;  // 由于数组索引从0开始，所以减1
            }
        }
        ++row;
    }

    file.close();
    return matrix;
}

int** PreProccess::processFileToMatrix2(const fs::path& filename, int& r, int& c) {
    std::ifstream file(filename.string());
    if (!file.is_open()) {
        throw std::runtime_error("无法打开文件");
    }

    std::string line;
    std::getline(file, line);  // 读取第一行

    int n, m;
    std::istringstream iss(line);
    iss >> n >> m;  // 假设第一行格式为 "n m"  
    c = n;
    r = m;

    // 动态分配二维数组
    vector<vector<int>> tmp_matrix;

    while (std::getline(file, line)) {
        vector<int> tmp_row(c, 0);  // 初始化当前行
        std::istringstream iss(line);
        int tmp;
        iss >> tmp;   // 跳过开头的数字
        iss >> tmp;  // 读取该行“1”的个数

        int col;
        for (int i = 0; i < tmp; ++i) {
            iss >> col;  // 读取列索引
            if (col > 0 && col <= n) {  // 确保列索引在有效范围内
                tmp_row[col - 1] = 1;  // 由于数组索引从0开始，所以减1
            }
        }
        tmp_matrix.push_back(tmp_row);
    }
    file.close();
    
    r = tmp_matrix.size();
    // 动态分配二维数组
    int** matrix = new int*[r];
    for(int i = 0; i < r; ++i) {
        matrix[i] = new int[c];
        for(int j = 0; j < c; ++j) {
            matrix[i][j] = tmp_matrix[i][j];  // 将1填入矩阵
        }
    }
    return matrix;
}

int** PreProccess::processFileToMatrix3(const std::string& filename, int& r, int& c) { 
    std::ifstream file(filename);
    if(!file.is_open()) {
        throw std::runtime_error("无法打开文件");
    }

    std::string line;
    std::getline(file, line);  // 读取第一行
    std::istringstream iss(line);
    int n, m;
    iss >> n >> m;  // 假设第一行格式为 "n m"
    c = n;
    r = m;

    // 动态分配二维数组
    int** matrix = new int*[r];
    for(int i = 0; i < r; ++i) {
        matrix[i] = new int[c];
        for(int j = 0; j < c; ++j) {
            matrix[i][j] = 0;
        }
    }

    int row = 0;
    while (std::getline(file, line))
    {
        std::istringstream rowStream(line);
        int col, count;

        // 读取该行“1”的个数
        rowStream >> count;

        for (int i = 0; i < count; ++i) {
            rowStream >> col;  // 读取列索引
            if (col > 0 && col <= n) {  // 确保列索引在有效范围内
                matrix[row][col - 1] = 1;  // 由于数组索引从0开始，所以减1
            }
        }
        row++;
    }
    
    file.close();
    return matrix;
}

void PreProccess::freeMatrix(int** matrix, int rows) {
    for (int i = 0; i < rows; ++i) {
        delete[] matrix[i];
    }
    delete[] matrix;
}
