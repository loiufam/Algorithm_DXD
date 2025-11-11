// DXZSolverAdapter.h
#ifndef DXZSOLVERADAPTER_H
#define DXZSOLVERADAPTER_H

#include "ExperimentFramework.h"
#include "ExperimentRunner.h"
#include "DXZ.h"
#include "MyCSV.h"

// ==================== DLX求解器适配器 ====================
class DLXSolverAdapter : ISolverAdapter<DanceZDD> {
private:
    Logger& logger;

public:
    explicit DLXSolverAdapter(Logger& logger) : logger(logger) {}

    InstanceResult runSerial(const std::string& filePath, 
                        const std::string& instanceName,
                        int readMode,
                        const ExperimentConfig& config) override {
        InstanceResult result(instanceName);
        
        try {
            logger.logLine("  DLX 开始运行...");
            
            // 创建求解器
            DanceZDD solver(filePath, readMode, logger);

            // 运行求解
            solver.startDLX();
            
            // 提取结果
            result.serialTime = solver.searchTime;
            result.serialTimeout = solver.timeout;
            result.solutionCountStr = solver.solutionCount;
            
            // 记录日志
            logger.logLine("  DLX 完成 - 时间: " + 
                          ExperimentUtils::formatTime(result.serialTime) + 
                          ", 解数: " + result.solutionCountStr +
                          (result.serialTimeout ? " (TIMEOUT)" : ""));
        } catch (const std::exception& e) {
            logger.logLine("DLX运行失败: {}" + std::string(e.what()));
            result.serialError = false;
        }
        return result;
    }

    void runParallel(InstanceResult& result,
                        const std::string& filePath,
                        int readMode,
                        const ExperimentConfig& config) override {
        std::cout << "DLX并行版本未实现";
        return;
    }

};


// ==================== DXZ求解器适配器 ====================
class DXZSolverAdapter : ISolverAdapter<DanceZDD> {
private:
    Logger& logger;

public:
    explicit DXZSolverAdapter(Logger& logger) : logger(logger) {}

    InstanceResult runSerial(const std::string& filePath, 
                        const std::string& instanceName,
                        int readMode,
                        const ExperimentConfig& config) override {
        InstanceResult result(instanceName);
        
        try {
            logger.logLine("  DXZ 开始运行...");
            
            // 创建求解器
            DanceZDD solver(filePath, readMode, logger);

            // 运行求解
            solver.startDXZ();
            
            // 提取结果
            result.serialTime = solver.searchTime;
            result.serialTimeout = solver.timeout;
            result.solutionCountStr = solver.solutionCount;
            
            // 记录日志
            logger.logLine("  DXZ 完成 - 时间: " + 
                          ExperimentUtils::formatTime(result.serialTime) + 
                          ", 解数: " + result.solutionCountStr +
                          (result.serialTimeout ? " (TIMEOUT)" : ""));
        } catch (const std::exception& e) {
            logger.logLine("DXZ运行失败: {}" + std::string(e.what()));
            result.serialError = false;
        }
        return result;
    }

    void runParallel(InstanceResult& result,
                        const std::string& filePath,
                        int readMode,
                        const ExperimentConfig& config) override {
        std::cout << "DXZ并行版本未实现";
        return;
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

#endif // DXZSOLVERADAPTER_H