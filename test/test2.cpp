#include "../include/DancingMatrix.h"
#include "../include/DXD.h"
#include "../include/MyCSV.h"
#include "../utils/ResProcessor.h"

static Logger logger("../dxd_exp_log.txt");  // 全局日志
const string table_file = "../exp_results.csv"; // 结果表格文件

const int DXD_COLUMN_INDEX = 6;
const int MDXD_COLUMN_INDEX = 7;
const int MAX_BLOCK_COLUMN_INDEX = 11;
const int DECOMPOSE_COUNT_INDEX = 12;

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
        const std::string folderPath3 = "../data/graph_matrix&3";
        filePaths.push_back(folderPath1);
        filePaths.push_back(folderPath2);
        filePaths.push_back(folderPath3);
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
            logger.logLine("===========处理文件夹: " + fileFolderName + "==========");
            // 遍历文件夹
            for (const auto& entry : fs::directory_iterator(file_path))
            {
                if (entry.is_regular_file())
                {                   
                    // 文件名
                    std::string file_name = entry.path().stem().string();
                    if (file_name == ".DS_Store") continue;          
                    logger.logLine("文件名: " + file_name);

                    try {
                        DanceDNNF dxdSolver(entry.path().string(), read_mode, logger, false, true);
                        dxdSolver.cur_instance = file_name;

                        shared_ptr<DXDResult> res = dxdSolver.startDXD();
                        // processor.processResultFile(res, AlgorithmType::DXD_S);
                        experimentCSV.updateTime(file_name, dxdSolver.searchTime, DXD_COLUMN_INDEX, dxdSolver.timeout);
                        if (!dxdSolver.timeout) {
                            experimentCSV.updateCount(file_name, dxdSolver.MAX_B_COUNT, MAX_BLOCK_COLUMN_INDEX);
                            experimentCSV.updateCount(file_name, dxdSolver.p_count, DECOMPOSE_COUNT_INDEX);
                        }
                        logger.logLine("");
                    } catch (const std::exception& e) {
                        logger.logLine(std::string("处理文件时出错: ") + e.what());
                    }

                    std::cout << std::endl;
  
                    try{
                        // ExactCoverSolver ecSolver(entry.path().string(), read_mode, logger, 16);
                        // ecSolver.cur_instance = file_name;
                        // shared_ptr<ExperimentResult> res = ecSolver.searchEC();
                        DanceDNNF dxdSolver(entry.path().string(), read_mode, logger, false, true, 24);
                        dxdSolver.cur_instance = file_name;

                        shared_ptr<DXDResult> res = dxdSolver.startMultiThreadDXD();
                        // processor.processResultFile(res, AlgorithmType::DXD_M);
                        experimentCSV.updateTime(file_name, dxdSolver.searchTime, MDXD_COLUMN_INDEX, dxdSolver.timeout);
                        if (!dxdSolver.timeout) {
                            experimentCSV.updateCount(file_name, dxdSolver.MAX_B_COUNT, MAX_BLOCK_COLUMN_INDEX);
                            experimentCSV.updateCount(file_name, dxdSolver.p_count, DECOMPOSE_COUNT_INDEX);
                        }
                        logger.logLine("");
                    } catch (const std::exception& e) {
                        logger.logLine(std::string("处理文件时出错: ") + e.what());
                    }

                    std::cout << std::endl;
                }
            }
            experimentCSV.writeCSV(table_file);
            logger.logLine("处理文件夹完毕: " + fileFolderName);
            // processor.saveTable(table_file);
        }

        logger.logLine("所有文件处理完毕");
    return 0;

}