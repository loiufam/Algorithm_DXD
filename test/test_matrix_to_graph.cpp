#include "../include/DancingMatrix.h"

int main() {
    // 测试矩阵到图的转换
    DancingMatrix dm("../input_matrix.txt", 3, false, true);
    dm.testCutEdge(2, 4);
    // dm.printComponents();
    return 0;
}