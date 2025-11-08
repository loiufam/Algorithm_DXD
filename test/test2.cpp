#include "../include/DancingMatrix.h"
#include "../include/DXD.h"
#include "../include/MyCSV.h"
#include "../utils/ResProcessor.h"

static Logger logger("../dxd_exp_log.txt");  // 全局日志
const string table_file = "../exp_results.csv"; // 结果表格文件
const string output_ett_file = "../dxd_ett.csv";
const string output_ig_file = "..dxd_ig.csv";

const string DXD_COLUMN_NAME = "DXD_S";
const string MDXD_COLUMN_NAME = "DXD_M";
const string NEW_DXD_COLUMN_NAME = "IDXD_S";
const string NEW_MDXD_COLUMN_NAME = "IDXD_M";

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
int main(int argc, char* argv[]) { 

        bool useETT = std::string(argv[1]) == "ett"; // 是否使用ETT
        string outputCSV;
        
        if (useETT) {
            outputCSV = output_ett_file;
            logger.logLine("使用ETT进行独立块检测");
        } else {
            outputCSV = output_ig_file;
            logger.logLine("使用增量图进行独立块检测");
        }

        // 文件夹路径
        std::vector<std::string> filePaths;
        const std::string folderPath1 = "../data/exact_cover_benchmark&1";
        const std::string folderPath2 = "../data/set_partitioning_benchmarks&2";
        const std::string folderPath3 = "../data/graph_matrix&3";
        std::string folderPath4 = "../data/graph_matrix/partition&3";
        std::string folderPath5 = "../data/graph_matrix/m_blocks&3";

        // filePaths.push_back(folderPath1);
        // filePaths.push_back(folderPath2);
        // filePaths.push_back(folderPath3);
        // filePaths.push_back(folderPath4);
        filePaths.push_back(folderPath5);

        char delimiter = '&'; 

        // ExperimentProcessor processor; 
        // processor.loadTable(table_file);
        MyCSV experimentCSV; // 结果CSV处理器
        experimentCSV.readCSV(table_file); // 读取结果表格
        int counter = 0;

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

                    if (useETT) {
                        // 1. use ETT
                        {
                            DanceDNNF dxdSolver(entry.path().string(), read_mode, logger, false, true);
                            dxdSolver.cur_instance = file_name;
                            dxdSolver.startDXD();
                            experimentCSV.updateTime(file_name, dxdSolver.searchTime, NEW_DXD_COLUMN_NAME, dxdSolver.timeout);
                        } 
                        
                        logger.logLine("");

                        {
                            DanceDNNF dxdSolver(entry.path().string(), read_mode, logger, false, true, 24);
                            dxdSolver.cur_instance = file_name;
                            dxdSolver.startMultiThreadDXD();
                            experimentCSV.updateTime(file_name, dxdSolver.searchTime, NEW_MDXD_COLUMN_NAME, dxdSolver.timeout);
                            if (!dxdSolver.timeout) {
                                // experimentCSV.updateMaxBlock(file_name, dxdSolver.MAX_B_COUNT);
                            }
                        }
                        
                        
                    } else {
                        // 2. use IG
                        {
                            DanceDNNF dxdSolver(entry.path().string(), read_mode, logger, true, false); 
                            dxdSolver.cur_instance = file_name;
                            dxdSolver.startDXD();
                            experimentCSV.updateTime(file_name, dxdSolver.searchTime, DXD_COLUMN_NAME, dxdSolver.timeout);
                        } 

                        logger.logLine("");

                         {
                            DanceDNNF dxdSolver(entry.path().string(), read_mode, logger, true, false, 24); 
                            dxdSolver.cur_instance = file_name;
                            dxdSolver.startMultiThreadDXD(); 
                            experimentCSV.updateTime(file_name, dxdSolver.searchTime, MDXD_COLUMN_NAME, dxdSolver.timeout);
                            if (!dxdSolver.timeout) {
                                experimentCSV.updateMaxBlock(file_name, dxdSolver.MAX_B_COUNT);
                                experimentCSV.updateSols(file_name, dxdSolver.solutionCount, MDXD_COLUMN_NAME);
                            }
                        }
                    }

                    logger.logLine("");

                    if (++counter % 5 == 0) {
                        logger.logLine("writing to csv: "+ outputCSV);
                        experimentCSV.writeCSV(outputCSV);
                    }
  
                    // ExactCoverSolver ecSolver(entry.path().string(), read_mode, logger, 16);
                    // ecSolver.cur_instance = file_name;
                    // shared_ptr<ExperimentResult> res = ecSolver.searchEC();
                }
            }
            logger.logLine("处理文件夹完毕: " + fileFolderName);
            // processor.saveTable(table_file);
        }

        logger.logLine("所有文件处理完毕");
    return 0;

}