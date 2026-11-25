#ifndef IMPROVED_BFS_H
#define IMPROVED_BFS_H

#include <cstdint>
#include <string>
#include <vector>

namespace improved_bfs {

using EdgeCode = std::uint64_t;

struct IncidenceMatrix {
    int rowCount = 0;
    int columnCount = 0;
    std::vector<std::vector<int>> columns;
    std::vector<int> activeRows;
    std::uint64_t activeEntries = 0;
};

struct ComponentBlock {
    std::vector<int> rows;
    std::vector<int> columns;
};

struct NonTreeBucket {
    int column = 0;
    std::vector<int> rows;
    std::vector<EdgeCode> treeEdges;
    std::uint64_t nonTreeOccurrences = 0;
};

struct ClassificationResult {
    std::vector<ComponentBlock> blocks;
    std::vector<EdgeCode> treeEdges;
    std::vector<EdgeCode> nonTreeEdges;
    std::vector<NonTreeBucket> nonTreeBuckets;
    std::uint64_t nonTreeOccurrences = 0;
    bool nonTreeMaterialized = false;
    std::uint64_t generatedPairs = 0;
    std::uint64_t duplicatePairs = 0;
    std::uint64_t dsuFinds = 0;
    std::uint64_t unionAttempts = 0;
    double milliseconds = 0.0;
};

struct BaselineBfsResult {
    std::vector<std::vector<int>> components;
    std::uint64_t uniqueEdges = 0;
    std::uint64_t generatedPairs = 0;
    double milliseconds = 0.0;
};

struct ReferenceClassificationResult {
    std::vector<std::vector<int>> components;
    std::vector<EdgeCode> treeEdges;
    std::vector<EdgeCode> nonTreeEdges;
    std::uint64_t generatedPairs = 0;
    std::uint64_t dsuFinds = 0;
    double milliseconds = 0.0;
};

IncidenceMatrix parseMatrixFile(const std::string& path);

IncidenceMatrix makeIncidenceMatrix(
    int rowCount, int columnCount,
    const std::vector<std::vector<int>>& rowColumns);

EdgeCode makeEdge(int u, int v);
int edgeU(EdgeCode edge);
int edgeV(EdgeCode edge);

ClassificationResult classifyAndFindComponents(
    const IncidenceMatrix& matrix, bool materializeNonTree = true);

BaselineBfsResult naiveBfs(const IncidenceMatrix& matrix);

BaselineBfsResult incidenceBfs(const IncidenceMatrix& matrix);

ReferenceClassificationResult referenceClassify(
    const IncidenceMatrix& matrix);

bool validateClassification(
    const IncidenceMatrix& matrix,
    const ClassificationResult& result,
    std::string* error);

bool validateCompressedClassification(
    const IncidenceMatrix& matrix,
    const ClassificationResult& result,
    std::string* error);

bool sameComponents(
    const std::vector<ComponentBlock>& improved,
    const std::vector<std::vector<int>>& baseline);

bool sameComponents(
    const std::vector<std::vector<int>>& lhs,
    const std::vector<std::vector<int>>& rhs);

}  // namespace improved_bfs

#endif
