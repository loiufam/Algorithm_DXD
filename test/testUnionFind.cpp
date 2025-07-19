#include "../include/DancingMatrix.h"


// 示例用法
int main() {

    std::vector<std::vector<int>> X = {
            {1, 1, 1, 0, 1, 0},
            {1, 1, 0, 0, 0, 0},    
            {0, 0, 0, 1, 0, 1},
            {0, 0, 0, 1, 0, 1},
            {0, 0, 1, 0, 1, 0}
        };

    int rows = X.size();
    int cols = X[0].size();

    // 动态分配二维数组
    int** matrix = new int*[rows];
    for (int i = 0; i < rows; ++i) {
        matrix[i] = new int[cols];
        for (int j = 0; j < cols; ++j) {
            matrix[i][j] = X[i][j];
        }
    }

    std::cout << "rows: " << rows << ", cols: " << cols << std::endl;
    DancingMatrix dlm(rows, cols, matrix);

    Block block(dlm.rowsSet, dlm.colsSet);
    dlm.initBlock(block);

    // vector<Block> blocks = dlm.spilitBlock(block);

    // dlm.printBlocks(blocks);
  
    // cout << "Merged sets :" << endl;
    // for (int i = 0; i < block.connectedRows.size(); ++i) {
    //     if (!block.connectedRows[i].empty()) {
    //         cout << "Set " << i << ": { ";
    //         for (int x : block.connectedRows[i]) cout << x << " ";
    //         cout << "}" << endl;
    //     }
    // }

    // cout << "\nRow to Group Index Map:" << endl;
    // for (const auto& [val, indices] : block.rowToRowsSet) {
    //     cout << val << " -> [ ";
    //     for (int idx : indices) cout << idx << " ";
    //     cout << "]\n";
    // }

    return 0;
}
