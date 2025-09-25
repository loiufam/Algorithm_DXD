#include "../include/DancingMatrix.h"
#include "../include/DXZ.h"

static Logger logger("../dxz_exp_log.txt");  // 全局日志

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

// 专门测DLX和DXZ的代码
int main() { 
        // 文件夹路径
        std::vector<std::string> filePaths;
        std::string folderPath1 = "../data/exact_cover_benchmark&1";
        std::string folderPath2 = "../data/set_partitioning_benchmarks&2";
        std::string folderPath3 = "../data/graph_dataset&3";
        std::string folderPath4 = "../data/exrta_matrix&1";
        filePaths.insert(filePaths.end(), {folderPath3, folderPath4});
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
                    // 文件路径          
                    if (entry.path().filename().string() == ".DS_Store") continue;     
                    logger.logLine("文件名: " + entry.path().filename().string());

                    try {
                        DanceZDD dxzSolver(entry.path().string(), read_mode, logger);
                        dxzSolver.startDLX();
                        dxzSolver.startDXZ();
                    } catch (const std::exception& e) {
                        logger.logLine(std::string("处理文件时出错: ") + e.what());
                    }

                }
            }
            logger.logLine("处理文件夹完毕: " + fileFolderName);
        }
        std::cout << "所有文件处理完毕" << std::endl;
    return 0;
}