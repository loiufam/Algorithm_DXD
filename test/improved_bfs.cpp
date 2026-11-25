#include "improved_bfs.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <numeric>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace improved_bfs {
namespace {

class DisjointSet {
public:
    explicit DisjointSet(int size)
        : parent_(static_cast<std::size_t>(size)),
          rank_(static_cast<std::size_t>(size), 0) {
        std::iota(parent_.begin(), parent_.end(), 0);
    }

    int find(int vertex) {
        int root = vertex;
        while (parent_[root] != root) root = parent_[root];
        while (parent_[vertex] != vertex) {
            const int next = parent_[vertex];
            parent_[vertex] = root;
            vertex = next;
        }
        return root;
    }

    bool unite(int lhs, int rhs) {
        lhs = find(lhs);
        rhs = find(rhs);
        if (lhs == rhs) return false;
        if (rank_[lhs] < rank_[rhs]) std::swap(lhs, rhs);
        parent_[rhs] = lhs;
        if (rank_[lhs] == rank_[rhs]) ++rank_[lhs];
        return true;
    }

private:
    std::vector<int> parent_;
    std::vector<unsigned char> rank_;
};

std::vector<std::vector<int>> blocksAsRows(
    const std::vector<ComponentBlock>& blocks) {
    std::vector<std::vector<int>> rows;
    rows.reserve(blocks.size());
    for (const ComponentBlock& block : blocks) {
        rows.push_back(block.rows);
    }
    return rows;
}

void sortAndCanonicalize(std::vector<std::vector<int>>& components) {
    for (auto& component : components) {
        std::sort(component.begin(), component.end());
    }
    std::sort(components.begin(), components.end());
}

std::vector<ComponentBlock> makeBlocks(
    DisjointSet& dsu, const IncidenceMatrix& matrix) {
    std::unordered_map<int, std::vector<int>> rowsByRoot;
    rowsByRoot.reserve(matrix.activeRows.size());
    for (int row : matrix.activeRows) {
        rowsByRoot[dsu.find(row)].push_back(row);
    }

    std::unordered_map<int, std::vector<int>> columnsByRoot;
    for (int column = 1; column <= matrix.columnCount; ++column) {
        const auto& rows =
            matrix.columns[static_cast<std::size_t>(column - 1)];
        if (rows.empty()) continue;
        columnsByRoot[dsu.find(rows.front())].push_back(column);
    }

    std::vector<ComponentBlock> blocks;
    blocks.reserve(rowsByRoot.size());
    for (auto& [root, rows] : rowsByRoot) {
        ComponentBlock block;
        block.rows = std::move(rows);
        auto columnIt = columnsByRoot.find(root);
        if (columnIt != columnsByRoot.end()) {
            block.columns = std::move(columnIt->second);
        }
        std::sort(block.rows.begin(), block.rows.end());
        std::sort(block.columns.begin(), block.columns.end());
        blocks.push_back(std::move(block));
    }
    std::sort(
        blocks.begin(), blocks.end(),
        [](const ComponentBlock& lhs, const ComponentBlock& rhs) {
            return lhs.rows < rhs.rows;
        });
    return blocks;
}

}  // namespace

EdgeCode makeEdge(int u, int v) {
    if (u > v) std::swap(u, v);
    if (u < 0) throw std::invalid_argument("negative row id");
    return (static_cast<EdgeCode>(static_cast<std::uint32_t>(u)) << 32) |
           static_cast<std::uint32_t>(v);
}

int edgeU(EdgeCode edge) {
    return static_cast<int>(edge >> 32);
}

int edgeV(EdgeCode edge) {
    return static_cast<int>(edge & 0xffffffffULL);
}

IncidenceMatrix makeIncidenceMatrix(
    int rowCount, int columnCount,
    const std::vector<std::vector<int>>& rowColumns) {
    if (rowCount < 0 || columnCount < 0) {
        throw std::invalid_argument("negative matrix dimensions");
    }
    IncidenceMatrix matrix;
    matrix.rowCount = rowCount;
    matrix.columnCount = columnCount;
    matrix.columns.resize(static_cast<std::size_t>(columnCount));

    for (int row = 0; row < rowCount; ++row) {
        std::unordered_set<int> seenInRow;
        for (int column : rowColumns[static_cast<std::size_t>(row)]) {
            if (column < 1 || column > columnCount) {
                throw std::out_of_range("column id outside matrix");
            }
            if (!seenInRow.insert(column).second) continue;
            matrix.columns[static_cast<std::size_t>(column - 1)]
                .push_back(row);
            ++matrix.activeEntries;
        }
        if (!seenInRow.empty()) matrix.activeRows.push_back(row);
    }
    return matrix;
}

IncidenceMatrix parseMatrixFile(const std::string& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot open matrix: " + path);

    int columnCount = 0;
    int rowCount = 0;
    if (!(input >> columnCount >> rowCount)) {
        throw std::runtime_error("missing matrix dimensions: " + path);
    }
    if (rowCount < 0 || columnCount < 0) {
        throw std::runtime_error("negative matrix dimensions: " + path);
    }

    IncidenceMatrix matrix;
    matrix.rowCount = rowCount;
    matrix.columnCount = columnCount;
    matrix.columns.resize(static_cast<std::size_t>(columnCount));

    std::string line;
    std::getline(input, line);
    int row = 0;
    while (row < rowCount && std::getline(input, line)) {
        std::istringstream tokens(line);
        std::vector<int> values;
        int value = 0;
        while (tokens >> value) values.push_back(value);
        if (values.empty()) continue;

        std::size_t skip = 1;
        if (values.size() ==
            static_cast<std::size_t>(values.front() + 1)) {
            skip = 1;
        } else if (values.size() >= 2 &&
                   values.size() ==
                       static_cast<std::size_t>(values[1] + 2)) {
            skip = 2;
        }

        std::unordered_set<int> seenInRow;
        for (std::size_t i = skip; i < values.size(); ++i) {
            const int column = values[i];
            if (column < 1 || column > columnCount) {
                throw std::runtime_error(
                    "column id outside matrix in " + path);
            }
            if (!seenInRow.insert(column).second) continue;
            matrix.columns[static_cast<std::size_t>(column - 1)]
                .push_back(row);
            ++matrix.activeEntries;
        }
        if (!seenInRow.empty()) matrix.activeRows.push_back(row);
        ++row;
    }
    if (row != rowCount) {
        throw std::runtime_error(
            "matrix has fewer rows than declared: " + path);
    }
    return matrix;
}

ClassificationResult classifyAndFindComponents(
    const IncidenceMatrix& matrix, bool materializeNonTree) {
    const auto started = std::chrono::steady_clock::now();
    ClassificationResult result;
    result.nonTreeMaterialized = materializeNonTree;
    DisjointSet dsu(matrix.rowCount);
    std::unordered_set<EdgeCode> seenEdges;
    if (materializeNonTree) {
        seenEdges.reserve(
            static_cast<std::size_t>(matrix.activeEntries * 3 + 1));
    }

    for (std::size_t columnIndex = 0;
         columnIndex < matrix.columns.size(); ++columnIndex) {
        const auto& rows = matrix.columns[columnIndex];
        const std::size_t count = rows.size();
        if (count < 2) continue;

        std::vector<int> roots(count);
        std::unordered_map<int, int> firstIndexByRoot;
        firstIndexByRoot.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            roots[i] = dsu.find(rows[i]);
            ++result.dsuFinds;
            firstIndexByRoot.emplace(roots[i], static_cast<int>(i));
        }

        std::vector<EdgeCode> selectedTreeEdges;
        selectedTreeEdges.reserve(firstIndexByRoot.size() - 1);
        for (std::size_t i = 1; i < count; ++i) {
            if (firstIndexByRoot.at(roots[i]) != static_cast<int>(i)) {
                continue;
            }
            const EdgeCode edge = makeEdge(rows.front(), rows[i]);
            selectedTreeEdges.push_back(edge);
            result.treeEdges.push_back(edge);
            if (materializeNonTree) seenEdges.insert(edge);
            dsu.unite(rows.front(), rows[i]);
            ++result.unionAttempts;
        }

        std::sort(selectedTreeEdges.begin(), selectedTreeEdges.end());
        const std::uint64_t allPairs =
            static_cast<std::uint64_t>(count) *
            static_cast<std::uint64_t>(count - 1) / 2;
        const std::uint64_t nonTreeCount =
            allPairs - selectedTreeEdges.size();
        result.nonTreeOccurrences += nonTreeCount;

        if (!materializeNonTree) {
            result.generatedPairs += allPairs;
            NonTreeBucket bucket;
            bucket.column = static_cast<int>(columnIndex + 1);
            bucket.rows = rows;
            bucket.treeEdges = std::move(selectedTreeEdges);
            bucket.nonTreeOccurrences = nonTreeCount;
            result.nonTreeBuckets.push_back(std::move(bucket));
            continue;
        }

        std::size_t treeCursor = 0;
        for (std::size_t i = 0; i < count; ++i) {
            for (std::size_t j = i + 1; j < count; ++j) {
                ++result.generatedPairs;
                const EdgeCode edge = makeEdge(rows[i], rows[j]);
                while (treeCursor < selectedTreeEdges.size() &&
                       selectedTreeEdges[treeCursor] < edge) {
                    ++treeCursor;
                }
                if (treeCursor < selectedTreeEdges.size() &&
                    selectedTreeEdges[treeCursor] == edge) {
                    continue;
                }
                if (!seenEdges.insert(edge).second) {
                    ++result.duplicatePairs;
                    continue;
                }
                result.nonTreeEdges.push_back(edge);
            }
        }
    }

    result.blocks = makeBlocks(dsu, matrix);
    std::sort(result.treeEdges.begin(), result.treeEdges.end());
    std::sort(result.nonTreeEdges.begin(), result.nonTreeEdges.end());
    if (materializeNonTree) {
        result.nonTreeOccurrences = result.nonTreeEdges.size();
    }
    result.milliseconds =
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started)
            .count();
    return result;
}

ReferenceClassificationResult referenceClassify(
    const IncidenceMatrix& matrix) {
    const auto started = std::chrono::steady_clock::now();
    ReferenceClassificationResult result;
    DisjointSet dsu(matrix.rowCount);
    std::unordered_set<EdgeCode> seenEdges;
    seenEdges.reserve(
        static_cast<std::size_t>(matrix.activeEntries * 3 + 1));

    for (const auto& rows : matrix.columns) {
        for (std::size_t i = 0; i < rows.size(); ++i) {
            for (std::size_t j = i + 1; j < rows.size(); ++j) {
                ++result.generatedPairs;
                const EdgeCode edge = makeEdge(rows[i], rows[j]);
                if (!seenEdges.insert(edge).second) continue;
                ++result.dsuFinds;
                if (dsu.unite(rows[i], rows[j])) {
                    result.treeEdges.push_back(edge);
                } else {
                    result.nonTreeEdges.push_back(edge);
                }
            }
        }
    }

    std::unordered_map<int, std::vector<int>> rowsByRoot;
    for (int row : matrix.activeRows) {
        rowsByRoot[dsu.find(row)].push_back(row);
    }
    for (auto& [root, rows] : rowsByRoot) {
        (void)root;
        std::sort(rows.begin(), rows.end());
        result.components.push_back(std::move(rows));
    }
    sortAndCanonicalize(result.components);
    std::sort(result.treeEdges.begin(), result.treeEdges.end());
    std::sort(result.nonTreeEdges.begin(), result.nonTreeEdges.end());
    result.milliseconds =
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started)
            .count();
    return result;
}

BaselineBfsResult naiveBfs(const IncidenceMatrix& matrix) {
    const auto started = std::chrono::steady_clock::now();
    BaselineBfsResult result;
    std::unordered_set<EdgeCode> edges;
    edges.reserve(
        static_cast<std::size_t>(matrix.activeEntries * 3 + 1));

    for (const auto& rows : matrix.columns) {
        for (std::size_t i = 0; i < rows.size(); ++i) {
            for (std::size_t j = i + 1; j < rows.size(); ++j) {
                ++result.generatedPairs;
                edges.insert(makeEdge(rows[i], rows[j]));
            }
        }
    }
    result.uniqueEdges = edges.size();

    std::vector<std::vector<int>> adjacency(
        static_cast<std::size_t>(matrix.rowCount));
    for (EdgeCode edge : edges) {
        const int u = edgeU(edge);
        const int v = edgeV(edge);
        adjacency[static_cast<std::size_t>(u)].push_back(v);
        adjacency[static_cast<std::size_t>(v)].push_back(u);
    }

    std::vector<char> visited(
        static_cast<std::size_t>(matrix.rowCount), false);
    for (int start : matrix.activeRows) {
        if (visited[static_cast<std::size_t>(start)]) continue;
        std::vector<int> component;
        std::queue<int> pending;
        pending.push(start);
        visited[static_cast<std::size_t>(start)] = true;
        while (!pending.empty()) {
            const int row = pending.front();
            pending.pop();
            component.push_back(row);
            for (int neighbor :
                 adjacency[static_cast<std::size_t>(row)]) {
                if (visited[static_cast<std::size_t>(neighbor)]) continue;
                visited[static_cast<std::size_t>(neighbor)] = true;
                pending.push(neighbor);
            }
        }
        std::sort(component.begin(), component.end());
        result.components.push_back(std::move(component));
    }
    sortAndCanonicalize(result.components);
    result.milliseconds =
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started)
            .count();
    return result;
}

BaselineBfsResult incidenceBfs(const IncidenceMatrix& matrix) {
    const auto started = std::chrono::steady_clock::now();
    BaselineBfsResult result;
    const int vertexCount = matrix.rowCount + matrix.columnCount;
    std::vector<std::vector<int>> adjacency(
        static_cast<std::size_t>(vertexCount));

    for (int column = 1; column <= matrix.columnCount; ++column) {
        const int columnVertex = matrix.rowCount + column - 1;
        for (int row :
             matrix.columns[static_cast<std::size_t>(column - 1)]) {
            adjacency[static_cast<std::size_t>(row)].push_back(columnVertex);
            adjacency[static_cast<std::size_t>(columnVertex)].push_back(row);
        }
    }

    std::vector<char> visited(
        static_cast<std::size_t>(vertexCount), false);
    for (int start : matrix.activeRows) {
        if (visited[static_cast<std::size_t>(start)]) continue;
        std::vector<int> component;
        std::queue<int> pending;
        pending.push(start);
        visited[static_cast<std::size_t>(start)] = true;
        while (!pending.empty()) {
            const int vertex = pending.front();
            pending.pop();
            if (vertex < matrix.rowCount) component.push_back(vertex);
            for (int neighbor :
                 adjacency[static_cast<std::size_t>(vertex)]) {
                if (visited[static_cast<std::size_t>(neighbor)]) continue;
                visited[static_cast<std::size_t>(neighbor)] = true;
                pending.push(neighbor);
            }
        }
        std::sort(component.begin(), component.end());
        result.components.push_back(std::move(component));
    }
    sortAndCanonicalize(result.components);
    result.milliseconds =
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started)
            .count();
    return result;
}

bool sameComponents(
    const std::vector<ComponentBlock>& improved,
    const std::vector<std::vector<int>>& baseline) {
    return sameComponents(blocksAsRows(improved), baseline);
}

bool sameComponents(
    const std::vector<std::vector<int>>& lhs,
    const std::vector<std::vector<int>>& rhs) {
    auto left = lhs;
    auto right = rhs;
    sortAndCanonicalize(left);
    sortAndCanonicalize(right);
    return left == right;
}

bool validateClassification(
    const IncidenceMatrix& matrix,
    const ClassificationResult& result,
    std::string* error) {
    auto fail = [&](const std::string& message) {
        if (error) *error = message;
        return false;
    };

    std::unordered_set<int> allRows;
    for (const ComponentBlock& block : result.blocks) {
        if (block.rows.empty()) return fail("empty component");
        for (int row : block.rows) {
            if (!allRows.insert(row).second) {
                return fail("row appears in multiple components");
            }
        }
    }
    if (allRows.size() != matrix.activeRows.size()) {
        return fail("component row count differs from active rows");
    }

    std::unordered_set<EdgeCode> expectedEdges;
    expectedEdges.reserve(
        static_cast<std::size_t>(
            result.treeEdges.size() + result.nonTreeEdges.size()));
    for (const auto& rows : matrix.columns) {
        for (std::size_t i = 0; i < rows.size(); ++i) {
            for (std::size_t j = i + 1; j < rows.size(); ++j) {
                expectedEdges.insert(makeEdge(rows[i], rows[j]));
            }
        }
    }

    std::unordered_set<EdgeCode> classified;
    for (EdgeCode edge : result.treeEdges) {
        if (!expectedEdges.count(edge)) return fail("unknown tree edge");
        if (!classified.insert(edge).second) {
            return fail("edge appears in both classifications");
        }
    }
    for (EdgeCode edge : result.nonTreeEdges) {
        if (!expectedEdges.count(edge)) return fail("unknown non-tree edge");
        if (!classified.insert(edge).second) {
            return fail("edge appears in both classifications");
        }
    }
    if (classified.size() != expectedEdges.size()) {
        return fail("classified edges do not cover the projected graph");
    }
    if (result.treeEdges.size() + result.blocks.size() !=
        matrix.activeRows.size()) {
        return fail("tree edge count does not equal V - components");
    }

    DisjointSet treeDsu(matrix.rowCount);
    for (EdgeCode edge : result.treeEdges) {
        if (!treeDsu.unite(edgeU(edge), edgeV(edge))) {
            return fail("tree edge set contains a cycle");
        }
    }
    return true;
}

bool validateCompressedClassification(
    const IncidenceMatrix& matrix,
    const ClassificationResult& result,
    std::string* error) {
    auto fail = [&](const std::string& message) {
        if (error) *error = message;
        return false;
    };
    if (result.nonTreeMaterialized) {
        return fail("compressed result unexpectedly materialized edges");
    }
    if (!result.nonTreeEdges.empty()) {
        return fail("compressed result contains explicit non-tree edges");
    }

    std::unordered_set<int> rowsInBlocks;
    for (const ComponentBlock& block : result.blocks) {
        for (int row : block.rows) {
            if (!rowsInBlocks.insert(row).second) {
                return fail("row appears in multiple components");
            }
        }
    }
    if (rowsInBlocks.size() != matrix.activeRows.size()) {
        return fail("component row count differs from active rows");
    }
    if (result.treeEdges.size() + result.blocks.size() !=
        matrix.activeRows.size()) {
        return fail("tree edge count does not equal V - components");
    }

    std::unordered_set<EdgeCode> allTreeEdges;
    std::uint64_t treeEdgesInBuckets = 0;
    std::uint64_t nonTreeOccurrences = 0;
    for (const NonTreeBucket& bucket : result.nonTreeBuckets) {
        if (bucket.column < 1 || bucket.column > matrix.columnCount) {
            return fail("compressed bucket has invalid column");
        }
        const auto& matrixRows =
            matrix.columns[static_cast<std::size_t>(bucket.column - 1)];
        if (bucket.rows != matrixRows) {
            return fail("compressed bucket rows differ from matrix");
        }
        std::unordered_set<int> bucketRows(
            bucket.rows.begin(), bucket.rows.end());
        for (EdgeCode edge : bucket.treeEdges) {
            if (!bucketRows.count(edgeU(edge)) ||
                !bucketRows.count(edgeV(edge))) {
                return fail("bucket tree edge endpoint is outside column");
            }
            if (!allTreeEdges.insert(edge).second) {
                return fail("tree edge occurs in multiple buckets");
            }
            ++treeEdgesInBuckets;
        }
        const std::uint64_t allPairs =
            static_cast<std::uint64_t>(bucket.rows.size()) *
            static_cast<std::uint64_t>(bucket.rows.size() - 1) / 2;
        if (bucket.treeEdges.size() > allPairs ||
            bucket.nonTreeOccurrences !=
                allPairs - bucket.treeEdges.size()) {
            return fail("compressed bucket count is inconsistent");
        }
        nonTreeOccurrences += bucket.nonTreeOccurrences;
    }
    if (treeEdgesInBuckets != result.treeEdges.size()) {
        return fail("tree edges are missing from compressed buckets");
    }
    if (nonTreeOccurrences != result.nonTreeOccurrences) {
        return fail("non-tree occurrence total is inconsistent");
    }

    std::vector<EdgeCode> sortedTree(result.treeEdges);
    std::sort(sortedTree.begin(), sortedTree.end());
    if (sortedTree != result.treeEdges) {
        return fail("tree edges are not sorted");
    }

    DisjointSet treeDsu(matrix.rowCount);
    for (EdgeCode edge : result.treeEdges) {
        if (!treeDsu.unite(edgeU(edge), edgeV(edge))) {
            return fail("tree edge set contains a cycle");
        }
    }
    return true;
}

}  // namespace improved_bfs
