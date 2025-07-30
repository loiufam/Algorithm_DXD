#include "../include/DancingMatrix.h"

int main(){
    std::cout << "Starting..." << std::endl;

    try
    {
        const std::string folderPath1 = "../data/exact_cover_benchmark";

        // 遍历文件夹
        for (const auto& entry : fs::directory_iterator(folderPath1))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".txt")
            {
                // 文件路径
                const std::string filePath = entry.path().string();
                std::cout << "文件名: " << entry.path().filename().string() << std::endl;

                // 从文件中提取 n 和 m 的值
                int r, c;
                int** matrix = PreProccess::processFileToMatrix1(filePath, r, c);

                DancingMatrix dmx(r, c, matrix);

                dmx.startOptimizedDXD();

                // 释放内存
                PreProccess::freeMatrix(matrix, r);
            }
        }
    }
    catch (const std::exception& e) {
        std::cerr << "An error occurred: " << e.what() << std::endl;
    }

    return 0;
}