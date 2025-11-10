// main_experiment.cpp
#include "../include/ExperimentFramework.h"
#include "../include/ExperimentRunner.h"
#include "../include/DXDSolverAdapter.h"
#include "../include/DXD.h"
#include "../include/MyCSV.h"

// ==================== 实验场景1: DXD-ETT ====================
void runDXD_ETT_Experiment(int threads = 24, int batchSize = 5) {
    // 配置实验
    auto config = ExperimentConfig::forETT(threads, batchSize);
    
    // 创建日志和CSV
    Logger baseLogger(config.logFile);
    MyCSV baseCSV;
    
    // 创建适配器
    auto csvAdapter = std::make_unique<MyCSVAdapter>(baseCSV);
    auto solverAdapter = std::make_unique<DXDSolverAdapter>(baseLogger);
    
    // 创建实验运行器
    ExperimentRunner<DXDSolverAdapter> runner(
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

// ==================== 实验场景2: DXD-IG ====================
void runDXD_IG_Experiment(int threads = 24, int batchSize = 5) {
    auto config = ExperimentConfig::forIG(threads, batchSize);
    
    Logger baseLogger(config.logFile);
    MyCSV baseCSV;
    
    auto csvAdapter = std::make_unique<MyCSVAdapter>(baseCSV);
    auto solverAdapter = std::make_unique<DXDSolverAdapter>(baseLogger);
    
    ExperimentRunner<DXDSolverAdapter> runner(
        config,
        baseLogger,
        std::move(csvAdapter),
        std::move(solverAdapter)
    );
    
    std::vector<std::string> folders = {
        "../data/graph_matrix/m_blocks&3"
    };
    
    runner.runExperiment(folders);
    runner.finalize();
}

// ==================== 实验场景3: 自定义实验 ====================
void runCustomExperiment() {
    // 完全自定义配置
    auto config = ExperimentConfig::custom("my_custom_exp")
        .setThreads(32)
        .setBatchSize(10)
        .setTimeout(7200)
        .setColumns("MY_SERIAL", "MY_PARALLEL")
        .setVerbose(true);
    
    config.useETT = true;
    config.useIG = false;
    
    Logger baseLogger(config.logFile);
    MyCSV baseCSV;
    
    auto csvAdapter = std::make_unique<MyCSVAdapter>(baseCSV);
    auto solverAdapter = std::make_unique<DXDSolverAdapter>(baseLogger);
    
    ExperimentRunner<DXDSolverAdapter> runner(
        config,
        baseLogger,
        std::move(csvAdapter),
        std::move(solverAdapter)
    );
    
    // 设置回调（可选）
    runner.setProgressCallback([](int current, int total, const std::string& status) {
        std::cout << "Progress: " << status << " (" << current << ")" << std::endl;
    });
    
    runner.setResultCallback([](const InstanceResult& result) {
        if (result.isSuccess()) {
            std::cout << "✓ " << result.instanceName 
                     << " - 加速比: " << result.getSpeedup() << "x" << std::endl;
        }
    });
    
    std::vector<std::string> folders = {
        "../data/exact_cover_benchmark&1",
        "../data/set_partitioning_benchmarks&2"
    };
    
    runner.runExperiment(folders);
    runner.finalize();
}

// ==================== 实验场景4: 批量对比实验 ====================
void runComparisonExperiment() {
    std::vector<std::string> folders = {
        "../data/graph_matrix/m_blocks&3"
    };
    
    std::cout << "运行对比实验: ETT vs IG\n" << std::endl;
    
    // 运行ETT实验
    std::cout << "========== ETT 实验 ==========" << std::endl;
    runDXD_ETT_Experiment(24, 5);
    
    // 运行IG实验
    std::cout << "\n========== IG 实验 ==========" << std::endl;
    runDXD_IG_Experiment(24, 5);
    
    std::cout << "\n对比实验完成！" << std::endl;
}

// ==================== 主函数 ====================
int main(int argc, char* argv[]) {
    try {
        if (argc < 2) {
            std::cout << "用法:\n";
            std::cout << "  " << argv[0] << " ett [threads] [batch_size]  - 运行ETT实验\n";
            std::cout << "  " << argv[0] << " ig [threads] [batch_size]   - 运行IG实验\n";
            std::cout << "  " << argv[0] << " custom                       - 运行自定义实验\n";
            std::cout << "  " << argv[0] << " compare                      - 运行对比实验\n";
            return 1;
        }
        
        std::string mode = argv[1];
        int threads = (argc > 2) ? std::stoi(argv[2]) : 24;
        int batchSize = (argc > 3) ? std::stoi(argv[3]) : 5;
        
        if (mode == "ett") {
            runDXD_ETT_Experiment(threads, batchSize);
        } else if (mode == "ig") {
            runDXD_IG_Experiment(threads, batchSize);
        } else if (mode == "custom") {
            runCustomExperiment();
        } else if (mode == "compare") {
            runComparisonExperiment();
        } else {
            std::cerr << "未知模式: " << mode << std::endl;
            return 1;
        }
        
        std::cout << "\n✓ 实验成功完成！" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "致命错误: " << e.what() << std::endl;
        return 1;
    }
}