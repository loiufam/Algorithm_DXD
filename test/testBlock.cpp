#include "../include/DancingMatrix.h"

int main(){
        std::vector<std::vector<int>> X = {
            {1, 1, 1, 0, 1, 0},
            {1, 1, 0, 0, 0, 0},    
            {0, 0, 1, 1, 0, 1},
            {0, 0, 0, 0, 1, 0}
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

        DancingMatrix dlm(rows, cols, matrix);
        Block block(dlm.rowsSet, dlm.colsSet);
        std::cout << "开始搜索..." << std::endl;
        std::shared_ptr<DNNFNode> res = dlm.parallelDXD(block);
        std::cout << "搜索完成，找到 " << res->count << " 个解。" << std::endl;
        std::cout << "搜索耗时: " << dlm.searchTimeSeconds << " 秒。" << std::endl;
        if(dlm.isParallelSearch) {
            std::cout << "并行搜索模式已启用。" << std::endl;
        } else {
            std::cout << "并行搜索模式未启用。" << std::endl;
        }
        dlm.printDNNF(res); // 打印DNNF树


        // 释放二维数组内存
        for (int i = 0; i < rows; ++i) {
            delete[] matrix[i];
        }
        delete[] matrix;

        return 0;
}