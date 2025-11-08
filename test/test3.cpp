#include "../include/DancingMatrix.h"
#include "../include/DXD.h"
#include "../include/MyCSV.h"

static Logger logger("../mdlx_exp_log.txt");  // 全局日志
const string table_file = "../exp_results.csv"; // 结果表格文件
const string output_ett_file = "../mdlx_ett.csv";
const string output_ig_file = "../mdlx.csv";

const string MDLX_COLUMN_NAME = "MDLX";

int main(int argc, char* argv[]) { 

        if (argc < 4) {
            logger.logLine("Usage: ./test3 <input_filefolder> <read_mode> <ett|ig>");
            return 1;
        }
        string filefolder = argv[1]; // 输入文件文件夹
        int read_mode = std::stoi(argv[2]); // 读取模式

        bool useETT = std::string(argv[3]) == "ett"; // 是否使用ETT
        if (useETT) {
            logger.logLine("使用ETT进行独立块检测");
        } else {
            logger.logLine("使用增量图进行独立块检测");
        }

        string outputCSV = useETT ? output_ett_file : output_ig_file;

        MyCSV experimentCSV; // 结果CSV处理器
        experimentCSV.readCSV(table_file); // 读取结果表格
        int counter = 0;

        std::string fileFolderName = filefolder.substr(filefolder.find_last_of("/\\") + 1);
        logger.logLine("===========处理文件夹: " + fileFolderName + "==========");
        // 遍历文件夹
        for (const auto& entry : fs::directory_iterator(filefolder))
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
                        DanceDNNF dxdSolver(entry.path().string(), read_mode, logger, false, true, 24);
                        dxdSolver.start_MDLX_Search();
                        experimentCSV.updateTime(file_name, dxdSolver.searchTime, MDLX_COLUMN_NAME, dxdSolver.timeout);
                        // if (!dxdSolver.timeout) {
                        //     experimentCSV.updateMaxBlock(file_name, dxdSolver.MAX_B_COUNT);
                        //     experimentCSV.updateSols(file_name, dxdSolver.solutionCount, MDLX_COLUMN_NAME);
                        // }
                    }
                    
                } else {
                    // 2. use IG
                    {
                        DanceDNNF dxdSolver(entry.path().string(), read_mode, logger, true, false, 24); 
                        dxdSolver.start_MDLX_Search();
                        experimentCSV.updateTime(file_name, dxdSolver.searchTime, MDLX_COLUMN_NAME, dxdSolver.timeout);
                        // if (!dxdSolver.timeout) {
                        //     experimentCSV.updateMaxBlock(file_name, dxdSolver.MAX_B_COUNT);
                        //     experimentCSV.updateSols(file_name, dxdSolver.solutionCount, MDLX_COLUMN_NAME);
                        // }
                    }
                }

                logger.logLine("");

                if (++counter % 5 == 0) {
                    experimentCSV.writeCSV(outputCSV);
                }

            }
        }
        logger.logLine("处理文件夹完毕: " + fileFolderName);

    return 0;

}