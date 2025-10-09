#include "../include/DancingMatrix.h"

int main(){
        std::vector<std::vector<int>> X = {
            {0, 1, 1, 0, 0, 0},
            {1, 1, 0, 0, 0, 0},    
            {0, 0, 1, 0, 0, 1},
            {0, 0, 0, 0, 1, 0},
            {0, 0, 0, 1, 1, 0}
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

        DancingMatrix dlm(rows, cols, matrix, true);

        dlm.printGraph();
        // auto block = dlm.InitBlock;
        // dlm.coverInBlock(1, block);
        // dlm.coverInBlock(2, block);
        // auto components = dlm.getComponents();
        // std::cout << "组件个数: " << components.size() << std::endl;
        // for (auto& component : components) {
        //     component.printComponent();
        // }

        // 释放二维数组内存
        for (int i = 0; i < rows; ++i) {
            delete[] matrix[i];
        }
        delete[] matrix;

        return 0;
}