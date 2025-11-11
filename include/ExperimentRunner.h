// ExperimentRunner.h
#ifndef EXPERIMENT_RUNNER_H
#define EXPERIMENT_RUNNER_H

#include "ExperimentFramework.h"


// ==================== 通用实验运行器 ====================
template<typename SolverAdapter>
class ExperimentRunner {
private:
    ExperimentConfig config;
    Logger& logger;
    std::unique_ptr<ICSVWriter> csvWriter;
    std::unique_ptr<SolverAdapter> solverAdapter;
    ExperimentStats stats;
    int processedCount;
    
    // 回调函数
    ProgressCallback progressCallback;
    ResultCallback resultCallback;
    
public:
    ExperimentRunner(const ExperimentConfig& cfg,
                    Logger& log,
                    std::unique_ptr<ICSVWriter> csv,
                    std::unique_ptr<SolverAdapter> solver = nullptr)
        : config(cfg), 
          logger(log), 
          csvWriter(std::move(csv)),
          solverAdapter(std::move(solver)),
          processedCount(0) {
        
        initialize();
    }
    
    // 设置回调函数
    void setProgressCallback(ProgressCallback cb) {
        progressCallback = cb;
    }
    
    void setResultCallback(ResultCallback cb) {
        resultCallback = cb;
    }
    
    // 运行实验
    void runExperiment(const std::vector<std::string>& folderPaths) {
        logExperimentStart();
        
        for (const auto& folder : folderPaths) {
            processFolder(folder);
        }
        
        logExperimentEnd();
    }
    
    // 处理单个文件夹
    void processFolder(const std::string& folderPath, char delimiter = '&') {
        auto folderInfo = ExperimentUtils::parseFolderPath(folderPath, delimiter);
        
        logger.logLine("\n" + ExperimentUtils::separator());
        logger.logLine("开始处理文件夹: " + folderInfo.name);
        logger.logLine(ExperimentUtils::separator());
        
        std::vector<std::string> files;
        if (config.runFromList) {
            files = ExperimentUtils::getFilesFromList(config.instanceListFile, folderInfo.path);
        } else {
            files = ExperimentUtils::getFilesInFolder(folderInfo.path);
        }

        int folderInstanceCount = 0;
        
        for (const auto& filePath : files) {
            std::string fileName = fs::path(filePath).stem().string();
            processInstance(filePath, fileName, folderInfo.readMode);
            folderInstanceCount++;
        }
        
        logger.logLine("\n文件夹 " + folderInfo.name + " 处理完毕，共 " + 
                       std::to_string(folderInstanceCount) + " 个实例");
        logger.logLine(ExperimentUtils::separator() + "\n");
        
        // 每个文件夹处理完后保存一次
        saveResults();
    }
    
    // 处理单个实例
    void processInstance(const std::string& filePath, 
                        const std::string& instanceName, 
                        int readMode) {
        processedCount++;
        
        if (config.verbose) {
            logger.logLine("\n[" + std::to_string(processedCount) + "] 处理实例: " + instanceName);
        }
        
        // 通知进度
        if (progressCallback) {
            progressCallback(processedCount, -1, "Processing: " + instanceName);
        }
        
        try {
            // 运行串行版本
            auto result = runSerialInstance(filePath, instanceName, readMode);
            
            // 运行并行版本
            if (!config.runDLX_DXZ) {
                runParallelInstance(result, filePath, readMode);
            }
            
            // 更新统计
            stats.addResult(result);
            
            // 更新CSV
            updateCSV(result);
            
            // 结果回调
            if (resultCallback) {
                resultCallback(result);
            }
            
            // 定期保存
            if (processedCount % config.saveBatchSize == 0) {
                saveResults();
            }
            
        } catch (const std::exception& e) {
            logger.logLine("处理实例 " + instanceName + " 时发生错误: " + e.what());
        }
    }
    
    // 获取统计信息
    const ExperimentStats& getStats() const {
        return stats;
    }
    
    // 保存最终结果
    void finalize() {
        saveResults();
        logger.logLine("\n实验完成，所有结果已保存");
        logger.logLine(stats.getSummary());
    }
    
private:
    void initialize() {
        // 确保目录存在
        ExperimentUtils::ensureDirectoryExists(config.logFile);
        ExperimentUtils::ensureDirectoryExists(config.outputCSV);
        
        // 尝试加载已有的实验表格
        if (fs::exists(config.baseTableFile)) {
            csvWriter->readCSV(config.baseTableFile);
            logger.logLine("已加载基础实验表格: " + config.baseTableFile);
        } else {
            logger.logLine("未找到基础实验表格，将创建新表格");
        }
    }
    
    void logExperimentStart() {
        logger.logLine(ExperimentUtils::separator());
        logger.logLine("实验开始时间: " + ExperimentConfig::getCurrentTimestamp());
        logger.logLine("实验名称: " + config.experimentName);
        logger.logLine("线程数: " + std::to_string(config.threadCount));
        logger.logLine("日志文件: " + config.logFile);
        logger.logLine("输出CSV: " + config.outputCSV);
        logger.logLine("批量保存频率: 每" + std::to_string(config.saveBatchSize) + "个实例");
        logger.logLine(ExperimentUtils::separator() + "\n");
    }
    
    void logExperimentEnd() {
        logger.logLine("\n" + ExperimentUtils::separator());
        logger.logLine("所有实验完成！");
        logger.logLine("总共处理: " + std::to_string(processedCount) + " 个实例");
        logger.logLine("结果文件: " + config.outputCSV);
        logger.logLine(ExperimentUtils::separator());
    }
    
    InstanceResult runSerialInstance(const std::string& filePath,
                                    const std::string& instanceName,
                                    int readMode) {
        if (solverAdapter) {
            return solverAdapter->runSerial(filePath, instanceName, readMode, config);
        } else {
            throw std::runtime_error("未设置求解器适配器");
        }
    }
    
    void runParallelInstance(InstanceResult& result,
                            const std::string& filePath,
                            int readMode) {
        if (solverAdapter) {
            solverAdapter->runParallel(result, filePath, readMode, config);
        }
    }
    
    void updateCSV(const InstanceResult& result) {
        // 更新串行时间
        csvWriter->updateTime(result.instanceName, result.serialTime, 
                            config.serialColumnName, result.serialTimeout);
        
        // 更新并行时间
        csvWriter->updateTime(result.instanceName, result.parallelTime, 
                            config.parallelColumnName, result.parallelTimeout);
        
        // 更新其他信息（仅在非超时时）
        if (!result.parallelTimeout && result.isSuccess()) {
            csvWriter->updateMaxBlock(result.instanceName, result.maxBlockCount);
            csvWriter->updateSols(result.instanceName, result.solutionCountStr, 
                                config.parallelColumnName);
        }
    }
    
    void saveResults() {
        if (config.verbose) {
            logger.logLine("正在保存结果到: " + config.outputCSV);
        }
        csvWriter->writeCSV(config.outputCSV);
    }
};

#endif // EXPERIMENT_RUNNER_H