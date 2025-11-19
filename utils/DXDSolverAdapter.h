// DXDSolverAdapter.h
#ifndef DXD_SOLVER_ADAPTER_H
#define DXD_SOLVER_ADAPTER_H

#include "ExperimentFramework.h"
#include "ExperimentRunner.h"
#include "DXD.h"
#include "MyCSV.h"

// ==================== DXD求解器适配器 ====================
class DXDSolverAdapter : public ISolverAdapter<DanceDNNF> {
private:
    Logger& logger;
    
public:
    explicit DXDSolverAdapter(Logger& log) : logger(log) {}
    
    InstanceResult runSerial(const std::string& filePath, 
                            const std::string& instanceName,
                            int readMode,
                            const ExperimentConfig& config) override {
        InstanceResult result(instanceName);
        
        try {
            logger.logLine("  [串行] 开始运行...");
            
            // 创建DXD求解器
            DanceDNNF solver(filePath, readMode, logger, config.useIG, config.useETT);
            solver.cur_instance = instanceName;
            
            // 运行求解
            solver.startDXD();
            
            // 提取结果
            result.serialTime = solver.searchTime;
            result.serialTimeout = solver.timeout;
            result.solutionCountStr = solver.solutionCount;
            result.maxBlockCount = solver.MAX_B_COUNT;
            
            // 记录日志
            logger.logLine("  [串行] 完成 - 时间: " + 
                          ExperimentUtils::formatTime(result.serialTime) + 
                          ", 解数: " + result.solutionCountStr +
                          (result.serialTimeout ? " (TIMEOUT)" : ""));
            logger.logLine("");
        } catch (const std::exception& e) {
            logger.logLine("  [串行] 错误: " + std::string(e.what()));
            result.serialError = true;
            logger.logLine("");
        }
        
        return result;
    }
    
    void runParallel(InstanceResult& result,
                    const std::string& filePath,
                    int readMode,
                    const ExperimentConfig& config) override {
        try {
            logger.logLine("  [并行] 开始运行...");
            
            // 创建多线程DXD求解器
            DanceDNNF solver(filePath, readMode, logger, 
                           config.useIG, config.useETT, config.threadCount);
            solver.cur_instance = result.instanceName;
            
            // 运行求解
            solver.startMultiThreadDXD();
            
            // 提取结果
            result.parallelTime = solver.searchTime;
            result.parallelTimeout = solver.timeout;
            
            // 验证解数一致性
            if (!solver.timeout) {
                
                // result.solutionCountStr = solver.solutionCount;
 
                // 计算加速比
                double speedup = result.getSpeedup();
                logger.logLine("  [并行] 完成 - 时间: " + 
                              ExperimentUtils::formatTime(result.parallelTime) + 
                              ", 最大块数: " + std::to_string(result.maxBlockCount) +
                              ", 加速比: " + std::to_string(speedup) + "x" +
                              (result.parallelTimeout ? " (TIMEOUT)" : ""));
            }
            logger.logLine("");
            
        } catch (const std::exception& e) {
            logger.logLine("  [并行] 错误: " + std::string(e.what()));
            result.parallelError = true;
            logger.logLine("");
        }
    }
};


// ==================== CSV适配器 ====================
class MyCSVAdapter : public ICSVWriter {
private:
    MyCSV& csv;
    
public:
    explicit MyCSVAdapter(MyCSV& c) : csv(c) {}
    
    void readCSV(const std::string& filePath) override {
        csv.readCSV(filePath);
    }
    
    void writeCSV(const std::string& filePath) override {
        csv.writeCSV(filePath);
    }
    
    void updateTime(const std::string& instance, double time, 
                   const std::string& column, bool timeout) override {
        csv.updateTime(instance, time, column, timeout);
    }
    
    void updateSols(const std::string& instance, const string& count, 
                   const std::string& column) override {
        csv.updateSols(instance, count, column);
    }
    
    void updateMaxBlock(const std::string& instance, int count) override {
        csv.updateMaxBlock(instance, count);
    }
};

#endif // DXD_SOLVER_ADAPTER_H