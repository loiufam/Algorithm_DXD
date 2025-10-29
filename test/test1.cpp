#include "../include/DancingMatrix.h"
#include "../include/DXZ.h"
#include "../include/MyCSV.h"
#include "../utils/ResProcessor.h"

static Logger logger("../dxz_exp_log.txt");  // 全局日志
const string table_file = "../exp_results.csv"; // 结果表格文件
const string result_file = "../exp_results_updated1.csv"; // 结果文件

const int DLX_COLUMN_INDEX = 3;
const int DXZ_COLUMN_INDEX = 4;

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
        std::string folderPath3 = "../data/graph_matrix&3";
        std::string folderPath4 = "../data/graph_matrix/partition&3";
        std::string folderPath5 = "../data/graph_matrix/cycle&3";
        // filePaths.push_back(folderPath1);
        // filePaths.push_back(folderPath2);
        // filePaths.push_back(folderPath3);
        filePaths.push_back(folderPath4);
        filePaths.push_back(folderPath5);
        char delimiter = '&';

        // ExperimentProcessor processor; 
        // processor.loadTable(table_file);
        MyCSV experimentCSV; // 结果CSV处理器
        experimentCSV.readCSV(table_file); // 读取结果表格

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
                    std::string file_name = entry.path().stem().string();        
                    if (file_name == ".DS_Store") continue;
                    logger.logLine("文件名: " + file_name);

                    // DLX
                    try {
                        DanceZDD dxzSolver(entry.path().string(), read_mode, logger);
                        dxzSolver.cur_instance = file_name;
                        shared_ptr<ExperimentResult> res = dxzSolver.startDLX();
                        // processor.processResultFile(res, AlgorithmType::DLX);
                        experimentCSV.updateTime(file_name, dxzSolver.searchTime, DLX_COLUMN_INDEX, dxzSolver.timeout);
                        std::cout << std::endl;
                        
                    } catch (const std::exception& e) {
                        logger.logLine(std::string("处理文件时出错: ") + e.what());
                    }

                    // DXZ
                    try {
                        DanceZDD dxzSolver(entry.path().string(), read_mode, logger);
                        dxzSolver.cur_instance = file_name;
                        shared_ptr<ExperimentResult> res = dxzSolver.startDXZ();
                        // processor.processResultFile(res, AlgorithmType::DXZ);
                        experimentCSV.updateTime(file_name, dxzSolver.searchTime, DXZ_COLUMN_INDEX, dxzSolver.timeout);
                        std::cout << std::endl;
                    } catch (const std::exception& e) {
                        logger.logLine(std::string("处理文件时出错: ") + e.what());
                    }
                }
            }
            logger.logLine("处理文件夹完毕: " + fileFolderName);
            // processor.saveTable(table_file);
            experimentCSV.writeCSV(result_file);
        }
        std::cout << "所有文件处理完毕" << std::endl;
    return 0;
}