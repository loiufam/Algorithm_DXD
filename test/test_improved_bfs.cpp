#include "improved_bfs.h"

#include <algorithm>
#include <iostream>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using improved_bfs::ClassificationResult;
using improved_bfs::ComponentBlock;
using improved_bfs::EdgeCode;
using improved_bfs::IncidenceMatrix;

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

void check(bool condition, const std::string& message) {
    if (!condition) fail(message);
}

void checkColumns(
    const IncidenceMatrix& matrix,
    const ClassificationResult& result) {
    std::vector<int> seen(
        static_cast<std::size_t>(matrix.columnCount + 1), 0);
    for (const ComponentBlock& block : result.blocks) {
        for (int column : block.columns) {
            if (column < 1 || column > matrix.columnCount) {
                fail("component contains an invalid column");
            }
            ++seen[static_cast<std::size_t>(column)];
        }
    }
    for (int column = 1; column <= matrix.columnCount; ++column) {
        const bool active =
            !matrix.columns[static_cast<std::size_t>(column - 1)].empty();
        const int count = seen[static_cast<std::size_t>(column)];
        if ((active && count != 1) || (!active && count != 0)) {
            fail("component columns do not partition active columns");
        }
    }
}

void compareSynthetic(
    const std::string& name, int rowCount, int columnCount,
    const std::vector<std::vector<int>>& rowColumns,
    bool printReport) {
    const IncidenceMatrix matrix =
        improved_bfs::makeIncidenceMatrix(
            rowCount, columnCount, rowColumns);
    const ClassificationResult improved =
        improved_bfs::classifyAndFindComponents(matrix);
    const ClassificationResult compressed =
        improved_bfs::classifyAndFindComponents(matrix, false);
    const auto baseline = improved_bfs::naiveBfs(matrix);
    const auto reference = improved_bfs::referenceClassify(matrix);

    std::string error;
    check(
        improved_bfs::validateClassification(matrix, improved, &error),
        name + ": " + error);
    check(
        improved_bfs::validateCompressedClassification(
            matrix, compressed, &error),
        name + ": compressed " + error);
    checkColumns(matrix, improved);
    check(
        improved_bfs::sameComponents(
            improved.blocks, baseline.components),
        name + ": improved components differ from BFS");
    check(
        improved_bfs::sameComponents(
            improved.blocks, reference.components),
        name + ": improved components differ from reference");
    check(
        improved.treeEdges == reference.treeEdges,
        name + ": tree edge sets differ");
    check(
        improved.nonTreeEdges == reference.nonTreeEdges,
        name + ": non-tree edge sets differ");
    check(
        compressed.treeEdges == improved.treeEdges,
        name + ": compressed tree edge sets differ");
    check(
        improved_bfs::sameComponents(
            compressed.blocks, baseline.components),
        name + ": compressed components differ from BFS");
    check(
        compressed.nonTreeOccurrences >= improved.nonTreeEdges.size(),
        name + ": compressed non-tree occurrence count is too small");

    if (printReport) {
        const double speedup = improved.milliseconds > 0.0
            ? baseline.milliseconds / improved.milliseconds
            : 0.0;
        std::cout
            << "case=" << name
            << " rows=" << matrix.rowCount
            << " cols=" << matrix.columnCount
            << " entries=" << matrix.activeEntries
            << " pairs=" << improved.generatedPairs
            << " components=" << improved.blocks.size()
            << " unique_edges="
            << improved.treeEdges.size() + improved.nonTreeEdges.size()
            << " tree=" << improved.treeEdges.size()
            << " non_tree=" << improved.nonTreeEdges.size()
            << " baseline_ms=" << baseline.milliseconds
            << " improved_ms=" << improved.milliseconds
            << " reference_ms=" << reference.milliseconds
            << " speedup=" << speedup
            << " duplicate_pairs=" << improved.duplicatePairs
            << '\n';
    }
}

void runUnitTests() {
    compareSynthetic(
        "single-edge", 3, 2,
        {{1, 2}, {1}, {2}}, false);
    compareSynthetic(
        "duplicate-across-columns", 4, 3,
        {{1, 2}, {2, 3}, {1, 2, 3}, {3}}, false);
    compareSynthetic(
        "dense-disconnected", 7, 5,
        {{1, 2, 3, 4, 5},
         {1, 2, 3, 4, 5},
         {1, 2, 3, 4, 5},
         {1, 2, 3, 4, 5},
         {1, 2, 3, 4, 5},
         {1, 2, 4},
         {1, 3, 5}},
        true);

    std::mt19937 generator(20261118);
    for (int trial = 0; trial < 300; ++trial) {
        const int rows = 1 + static_cast<int>(generator() % 14);
        const int columns = 1 + static_cast<int>(generator() % 10);
        std::vector<std::vector<int>> rowColumns(
            static_cast<std::size_t>(rows));
        const unsigned density = 2 + generator() % 9;
        for (int row = 0; row < rows; ++row) {
            for (int column = 1; column <= columns; ++column) {
                if (generator() % 10 < density) {
                    rowColumns[static_cast<std::size_t>(row)]
                        .push_back(column);
                }
            }
        }
        compareSynthetic(
            "random-" + std::to_string(trial),
            rows, columns, rowColumns, false);
    }
    std::cout << "unit_tests=passed randomized_cases=300\n";
}

void runFileBenchmark(const std::string& path) {
    std::cerr << "phase=parse file=" << path << '\n';
    const IncidenceMatrix matrix = improved_bfs::parseMatrixFile(path);
    std::uint64_t estimatedPairs = 0;
    for (const auto& rows : matrix.columns) {
        estimatedPairs +=
            static_cast<std::uint64_t>(rows.size()) *
            static_cast<std::uint64_t>(rows.size() - (rows.empty() ? 0 : 1)) /
            2;
    }
    const bool materializeNonTree = estimatedPairs <= 10000000ULL;
    std::cerr << "phase=improved file=" << path
              << " entries=" << matrix.activeEntries
              << " rows=" << matrix.activeRows.size()
              << " cols=" << matrix.columnCount
              << " non_tree_mode="
              << (materializeNonTree ? "explicit" : "compressed")
              << '\n';
    const ClassificationResult improved =
        improved_bfs::classifyAndFindComponents(
            matrix, materializeNonTree);
    std::cerr << "phase=baseline file=" << path << '\n';
    const auto baseline = improved_bfs::incidenceBfs(matrix);
    std::cerr << "phase=validate file=" << path << '\n';

    std::string error;
    if (materializeNonTree) {
        check(
            improved_bfs::validateClassification(
                matrix, improved, &error),
            path + ": " + error);
        const auto reference =
            improved_bfs::referenceClassify(matrix);
        check(
            improved.treeEdges == reference.treeEdges,
            path + ": tree edge sets differ from reference");
        check(
            improved.nonTreeEdges == reference.nonTreeEdges,
            path + ": non-tree edge sets differ from reference");
    } else {
        check(
            improved_bfs::validateCompressedClassification(
                matrix, improved, &error),
            path + ": " + error);
    }
    checkColumns(matrix, improved);
    check(
        improved_bfs::sameComponents(
            improved.blocks, baseline.components),
        path + ": improved components differ from BFS");

    const double speedup = improved.milliseconds > 0.0
        ? baseline.milliseconds / improved.milliseconds
        : 0.0;
    std::cout
        << "file=" << path
        << " rows=" << matrix.rowCount
        << " cols=" << matrix.columnCount
        << " entries=" << matrix.activeEntries
        << " pairs=" << improved.generatedPairs
        << " components=" << improved.blocks.size()
        << " tree=" << improved.treeEdges.size()
        << " non_tree_buckets=" << improved.nonTreeBuckets.size()
        << " unique_non_tree_edges=" << improved.nonTreeEdges.size()
        << " non_tree_occurrences=" << improved.nonTreeOccurrences
        << " incidence_bfs_ms=" << baseline.milliseconds
        << " improved_ms=" << improved.milliseconds
        << " speedup=" << speedup
        << " non_tree_mode="
        << (materializeNonTree ? "explicit" : "compressed")
        << " status=correct\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        runUnitTests();
        for (int i = 1; i < argc; ++i) {
            runFileBenchmark(argv[i]);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
}
