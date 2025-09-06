#include "../include/DancingMatrix.h"
#include "../include/DXD.h"
#include "../include/ConnectedGraph.h"

int main(){
    std::vector<std::vector<int>> X = {
            {1, 1, 0, 0, 0, 0},
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
    {
    DanceDNNF dxd(rows, cols, matrix);
    // Block initBlock = dxd.getBlock();

    // dxd.coverInBlock(3, initBlock);
    // dxd.coverInBlock(5, initBlock);
    // set<int> existRowSet(initBlock.rows.begin(), initBlock.rows.end());
    // vector<vector<int>> components = dxd.connectedGraph->getComponents(existRowSet);
    
    // for(auto& comp : components) {
    //     std::cout << "组件: ";
    //     for(auto row : comp) {
    //         std::cout << row << " ";
    //     }
    //     std::cout << std::endl;
    // }

    // dxd.printBlocks(dxd.spilit(components));


    // // 舞蹈链合并行集合
    // for(auto row_set : row_sets) {
    //     std::cout << "行集合: ";
    //     for(auto row : row_set) {
    //         std::cout << row << " ";
    //     }
    //     std::cout << std::endl;
    // }
    // std::cout << std::endl;
    // vector<Block> blocks = dlm.spilitBlockParallel(row_sets);
    // dlm.printBlocks(blocks);
    
    // dxd.startDXD();

    dxd.startMultiThreadDXD();
    }
    // dlm.printDetectedBlocks();
    // 释放二维数组内存
    for (int i = 0; i < rows; ++i) {
        delete[] matrix[i];
    }
    delete[] matrix;

    return 0;
}