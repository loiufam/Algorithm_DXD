#include "../include/DancingMatrix.h"
#include "../src/DynamicGraph.cpp"

int main(){
        const string file_path = "../data/graph_matrix/Missouri.txt";
        const string input_file = "../input_matrix.txt";

        DancingMatrix matrix(file_path, 3);
        IncrementalConnectedGraph graph(matrix.ROWS);


        std::cout << "=== 初始化图 ===" << std::endl;
        graph.initialize(matrix);
        // graph.printStats();
        
        // 2. 计算初始连通分量
        std::cout << "\n=== 初始连通分量 ===" << std::endl;
        auto components = graph.computeComponents();
        std::cout << "连通分量数量: " << components.size() << std::endl;
        

        std::cout << "\n=== 获取列集合 ===" << std::endl;
        auto columnSets = graph.getComponentColumnSets();

        // 打印第一个分量的列
        if (!columnSets.empty()) {
            std::cout << "第一个分量的列: ";
            for (size_t i = 0; i < std::min(size_t(20), columnSets[0].size()); ++i) {
                std::cout << columnSets[0][i] << " ";
            }
            if (columnSets[0].size() > 20) std::cout << "...";
            std::cout << std::endl;
        }

        // 4. 动态删除操作
        std::cout << "\n=== 测试删除操作 ===" << std::endl;
        graph.deactivateRow(3);
        graph.deactivateRow(4);

        components = graph.computeComponents();
        std::cout << "删除后的连通分量数: " << components.size() << std::endl;
        // 打印前5个连通分量
        for (size_t i = 0; i < std::min(size_t(5), components.size()); ++i) {
            components[i].print();
        }
        
        // 5. 恢复操作
        std::cout << "\n=== 测试恢复操作 ===" << std::endl;
        graph.reactivateRows({3, 4});
        components = graph.computeComponents();
        std::cout << "恢复后的连通分量数: " << components.size() << std::endl;
        // 打印前5个连通分量
        for (size_t i = 0; i < std::min(size_t(5), components.size()); ++i) {
            components[i].print();
        }

        return 0;
}