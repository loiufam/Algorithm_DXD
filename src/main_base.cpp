#include "../include/ExperimentFramework.h"
#include "../include/ExperimentRunner.h"
#include "../include/DXZSolverAdapter.h"
#include "../include/DXZ.h"
#include "../include/MyCSV.h"

// ==================== 实验场景1: DLX ====================
void runDLX_Experiment(std::string mode = "1") {
    // 配置实验
    auto config = ExperimentConfig::forDLX();

    if (mode == "2") {
        config.setInstanceFile("../instances.txt");
        config.runFromList = true;
    }

    config.runDLX_DXZ = true;

    // 创建日志和CSV
    Logger baseLogger(config.logFile);
    MyCSV baseCSV;
    
    // 创建适配器
    auto csvAdapter = std::make_unique<MyCSVAdapter>(baseCSV);
    auto solverAdapter = std::make_unique<DLXSolverAdapter>(baseLogger);
    
    // 创建实验运行器
    ExperimentRunner<DLXSolverAdapter> runner(
        config,
        baseLogger,
        std::move(csvAdapter),
        std::move(solverAdapter)
    );
    
    // 定义测试文件夹
    std::vector<std::string> folders = {
        "../data/graph_matrix/m_blocks&3"
    };
    
    // 运行实验
    runner.runExperiment(folders);
    runner.finalize();
}

// ==================== 实验场景2: DXZ ====================
void runDXZ_Experiment(std::string mode = "1") {
    // 配置实验
    auto config = ExperimentConfig::forDXZ();
   
    if (mode == "2") {
        config.setInstanceFile("../instances.txt");
        config.runFromList = true;
    }

    config.runDLX_DXZ = true;

    // 创建日志和CSV
    Logger baseLogger(config.logFile);
    MyCSV baseCSV;
    
    // 创建适配器
    auto csvAdapter = std::make_unique<MyCSVAdapter>(baseCSV);
    auto solverAdapter = std::make_unique<DXZSolverAdapter>(baseLogger);
    
    // 创建实验运行器
    ExperimentRunner<DXZSolverAdapter> runner(
        config,
        baseLogger,
        std::move(csvAdapter),
        std::move(solverAdapter)
    );
    
    // 定义测试文件夹
    std::vector<std::string> folders = {
        "../data/graph_matrix/m_blocks&3"
    };
    
    // 运行实验
    runner.runExperiment(folders);
    runner.finalize();
}

// ==================== 主函数 ====================
int main(int argc, char* argv[]) {
    try {
        if (argc < 2) {
            std::cout << "用法:\n";
            std::cout << "  " << argv[0] << " dlx mode  - 运行DLX实验\n";
            std::cout << "  " << argv[0] << " dxz mode  - 运行DXZ实验\n";
            std::cout << "  " << argv[0] << " mode 1: run from folder; mode 2: run from list.txt \n";
            return 1;
        }
        
        std::string alg = argv[1];
        std::string mode = (argc >= 3) ? argv[2] : "1";
        
        if (alg == "dlx") {
            runDLX_Experiment(mode);
        } else if (alg == "dxz") {
            runDXZ_Experiment(mode);
        } else {
            std::cerr << "未知算法: " << alg << std::endl;
            return 1;
        }
        
        std::cout << "\n✓ 实验成功完成！" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "致命错误: " << e.what() << std::endl;
        return 1;
    }
}