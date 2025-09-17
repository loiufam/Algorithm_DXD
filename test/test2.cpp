#include "../include/DancingMatrix.h"
#include "../include/DXD.h"

static Logger logger("../dxd_exp_log.txt");  // 全局日志
// 专门测试DXD
int main() { 
    try
    {
        // 文件夹路径
        const std::string folderPath1 = "../data/exact_cover_benchmark";
        const std::string folderPath2 = "../data/set_partitioning_benchmarks";
        const std::string filePath_d3x = "../data/dataset_d3x";
        std::cout << "exact_cover_benchmark 文件夹处理开始" << std::endl;
        // 遍历文件夹
        for (const auto& entry : fs::directory_iterator(folderPath1))
        {
            // break;
            if (entry.is_regular_file() && entry.path().extension() == ".txt")
            {
                // 文件路径
                // const std::string filePath = entry.path().string();
                // std::cout << "文件名: " << entry.path().filename().string() << std::endl;
                logger.logLine("文件名: " + entry.path().filename().string());

                // 从文件中提取 n 和 m 的值
                // int r, c;
                // int** matrix = PreProccess::processFileToMatrix1(filePath, r, c);

                // DanceDNNF dxdSolver(r, c, matrix);

                {
                    DanceDNNF dxdSolver(entry.path().string(), 1, logger, true);
                    dxdSolver.startDXD();
                }
                
                {
                    ExactCoverSolver ecSolver(entry.path().string(), 1, logger);
                    ecSolver.searchEC();
                }

                // 释放内存
                // PreProccess::freeMatrix(matrix, r);
            }
        }
        std::cout << "exact_cover_benchmark 文件夹处理完毕" << std::endl;

        std::cout << "set_partitioning_benchmarks 文件夹处理开始" << std::endl;
        // 遍历文件夹
        for (const auto& entry : fs::directory_iterator(folderPath2))
        {   
            // break;
            if (entry.is_regular_file() && entry.path().extension() == ".txt")
            {
                // 文件路径
                // std::cout << "文件名: " << entry.path().filename().string() << std::endl;
                logger.logLine("文件名: " + entry.path().filename().string());

                // 从文件中提取 n 和 m 的值
                // int r, c;
                // int** matrix = PreProccess::processFileToMatrix2(entry.path(), r, c);

                // DanceDNNF dxdSolver(r, c, matrix);
                {
                    DanceDNNF dxdSolver(entry.path().string(), 2, logger, true);
                    dxdSolver.startDXD();
                }
                
                {
                    ExactCoverSolver ecSolver(entry.path().string(), 2, logger);
                    ecSolver.searchEC();
                }

                // 释放内存
                // PreProccess::freeMatrix(matrix, r);
            }
        }
        std::cout << "set_partitioning_benchmarks 文件夹处理完毕" << std::endl;

        std::cout << "d3x数据集处理开始" << std::endl;
        for(const auto& entry : fs::directory_iterator(filePath_d3x))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".txt")
            {
                // 文件路径
                // std::cout << "文件名: " << entry.path().filename().string() << std::endl;
                logger.logLine("文件名: " + entry.path().filename().string());

                // 从文件中提取 n 和 m 的值
                // int r, c;
                // int** matrix = PreProccess::processFileToMatrix3(entry.path().string(), r, c);

                // DanceDNNF dxdSolver(r, c, matrix);
                {
                    DanceDNNF dxdSolver(entry.path().string(), 3, logger, true);
                    dxdSolver.startDXD();
                }
                
                {
                    ExactCoverSolver ecSolver(entry.path().string(), 3, logger);
                    ecSolver.searchEC();
                }

                // 释放内存
                // PreProccess::freeMatrix(matrix, r);
            }
        }
        std::cout << "d3x数据集处理完毕" << std::endl;
 
    }
    catch(const std::exception& e)
    {
        std::cerr << e.what() << '\n';
    }
    
    return 0;

}