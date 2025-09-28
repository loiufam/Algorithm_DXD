#include "../include/DancingMatrix.h"
#include "../include/DXD.h"
#include <string>

static Logger logger("../dxd_exp_log.txt");  // 全局日志

// 使用分隔符分割字符串
std::vector<std::string> split(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string item;

    while (std::getline(ss, item, delimiter)) {
        tokens.push_back(item);
    }

    return tokens;
}

// 专门测试DXD
int main() { 
        // 文件夹路径
        std::vector<std::string> filePaths;
        const std::string folderPath1 = "../data/exact_cover_benchmark&1";
        const std::string folderPath2 = "../data/set_partitioning_benchmarks&2";
        const std::string folderPath3 = "../data/graph_dataset&3";
        const std::string folderPath4 = "../data/extra_matrix&1";
        filePaths.insert(filePaths.end(), {folderPath4});
        char delimiter = '&'; 

        for (auto& fp : filePaths) {
            std::vector<std::string> result = split(fp, delimiter);
            std::string file_path = result[0];
            int read_mode = std::stoi(result[1]);
            std::string fileFolderName = file_path.substr(file_path.find_last_of("/\\") + 1);
            logger.logLine("处理文件夹: " + fileFolderName);
            // 遍历文件夹
            for (const auto& entry : fs::directory_iterator(file_path))
            {
                if (entry.is_regular_file())
                {
                    std::string fileName = entry.path().stem().string();
                    if (fileName == ".DS_Store") continue;
                    if(entry.path().stem().extension() == ".in") continue; // 跳过.in文件

                    // 文件路径  
                    if (entry.path().filename().string() == ".DS_Store") continue;          
                    logger.logLine("文件名: " + entry.path().stem().string());

                    try {
                        DanceDNNF dxdSolver(entry.path().string(), read_mode, logger, true);
                        dxdSolver.startDXD();
                    } catch (const std::exception& e) {
                        logger.logLine(std::string("处理文件时出错: ") + e.what());
                    }

                    try{
                        ExactCoverSolver ecSolver(entry.path().string(), read_mode, logger, 16);
                        ecSolver.searchEC();
                    } catch (const std::exception& e) {
                        logger.logLine(std::string("处理文件时出错: ") + e.what());
                    }

                }
            }
            logger.logLine("处理文件夹完毕: " + fileFolderName);
        }

        logger.logLine("所有文件处理完毕");
    return 0;

}