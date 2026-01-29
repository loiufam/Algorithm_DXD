#include "../include/DancingMatrix.h"

int main() {
    // 测试矩阵到图的转换
    DancingMatrix dm("../input_matrix.txt", 3, false, true);
    // dm.testCutEdge(2, 4);
    // dm.testReRoot(5);
    // dm.testSplay(3);
    // dm.printComponents();
    // 不分裂
    // std::cout << "测试不分裂情况：" << std::endl;
    // dm.testDynamicUpdateCC({0, 1});
    // 分裂
    std::cout << "测试分裂情况：" << std::endl;
    dm.testDynamicUpdateCC({2, 3, 4});
    return 0;
}