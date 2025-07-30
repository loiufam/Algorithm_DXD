#include "../include/DancingMatrix.h"

int main(){
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

    dlm.startSingleDXD();
    // DXD_Block dxd_block(dlm.colsSet, dlm.rowToColsSet, dlm.colToRowsSet);
    // vector<DXD_Block> blocks = dlm.detectBlocks(dxd_block);
    // if(blocks.size() > 1){
    //     for(int i = 0; i < blocks.size(); ++i){
    //         string name = "Block" + to_string(i);
    //         blocks[i].print(name);
    //     }
    // }
    // dxd_block.print("DXD_Block");
    // dlm.coverInDXDBlock(1, dxd_block);
    // dxd_block.print("DXD_Block after cover(1)");
    // dlm.coverInDXDBlock(2, dxd_block);
    // dxd_block.print("DXD_Block after cover(2)");
    // dlm.uncoverInDXDBlock(2, dxd_block);
    // dxd_block.print("DXD_Block after uncover(2)");
    // dlm.startSearch(true);
    // dlm.startDLX();
    // dlm.startSingleDXD();
    // dlm.startDXZ();
    
    // dlm.printDetectedBlocks();
    // 释放二维数组内存
    for (int i = 0; i < rows; ++i) {
        delete[] matrix[i];
    }
    delete[] matrix;

    return 0;
}