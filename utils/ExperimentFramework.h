// ExperimentFramework.h
#ifndef EXPERIMENT_FRAMEWORK_H
#define EXPERIMENT_FRAMEWORK_H

#include "DancingMatrix.h"
#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <functional>
#include <map>

namespace fs = std::filesystem;

// ==================== 实验类型枚举 ====================
enum class ExperimentType {
    DXD_ETT,      // DXD with ETT
    DXD_IG,       // DXD with Incremental Graph
    CUSTOM,        // 自定义实验
    DLX,           // DLX算法
    DXZ           // DXZ算法
};

// ==================== 配置管理 ====================
class ExperimentConfig {
public:
    // 核心配置
    ExperimentType type;
    std::string experimentName;
    std::string logFile;
    std::string outputCSV;
    std::string baseTableFile;
    std::string instanceListFile;
    
    // 算法参数
    int threadCount;
    bool useETT;
    bool useIG;
    bool runFromList = false;
    bool runDLX_DXZ = false;
    
    // 运行参数
    int saveBatchSize;
    double timeoutSeconds;
    bool verbose;
    
    // 列名配置
    std::string serialColumnName;
    std::string parallelColumnName;
    
    // 默认构造
    ExperimentConfig() 
        : type(ExperimentType::CUSTOM),
          experimentName("experiment"),
          baseTableFile("../exp_results.csv"),
          threadCount(24),
          useETT(false),
          useIG(false),
          saveBatchSize(5),
          timeoutSeconds(3600),
          verbose(true) {}
    
    // ETT实验配置
    static ExperimentConfig forETT(int threads = 24, int batchSize = 5) {
        ExperimentConfig config;
        config.type = ExperimentType::DXD_ETT;
        config.experimentName = "dxd_ett";
        config.useETT = true;
        config.useIG = false;
        config.threadCount = threads;
        config.saveBatchSize = batchSize;
        config.serialColumnName = "IDXD_S";
        config.parallelColumnName = "IDXD_M";
        config.setupFiles();
        return config;
    }
    
    // IG实验配置
    static ExperimentConfig forIG(int threads = 24, int batchSize = 5) {
        ExperimentConfig config;
        config.type = ExperimentType::DXD_IG;
        config.experimentName = "dxd_ig";
        config.useETT = false;
        config.useIG = true;
        config.threadCount = threads;
        config.saveBatchSize = batchSize;
        config.serialColumnName = "DXD_S";
        config.parallelColumnName = "DXD_M";
        config.setupFiles();
        return config;
    }
    
    // 自定义实验配置
    static ExperimentConfig custom(const std::string& name) {
        ExperimentConfig config;
        config.type = ExperimentType::CUSTOM;
        config.experimentName = name;
        config.setupFiles();
        return config;
    }

    static ExperimentConfig forDLX() {
        ExperimentConfig config;
        config.type = ExperimentType::DLX;
        config.experimentName = "dlx";
        config.serialColumnName = "DLX";
        config.setBatchSize(5);
        config.setTimeout(1200);
        config.setupFiles();
        return config;
    };

    static ExperimentConfig forDXZ() {
        ExperimentConfig config;
        config.type = ExperimentType::DXZ;
        config.experimentName = "dxz";
        config.serialColumnName = "DXZ";
        config.setBatchSize(5);
        config.setTimeout(1200);
        config.setupFiles();
        return config;
    };
    
    // 设置文件路径
    void setupFiles() {
        auto timestamp = getCurrentTimestamp();
        logFile = "../logs/" + experimentName + "_" + timestamp + ".txt";
        outputCSV = "../results/" + experimentName + "_" + timestamp + ".csv";
    }
    
    // 获取当前时间戳
    static std::string getCurrentTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time), "%Y%m%d_%H%M%S");
        return ss.str();
    }
    
    // 链式配置方法
    ExperimentConfig& setThreads(int threads) {
        threadCount = threads;
        return *this;
    }
    
    ExperimentConfig& setBatchSize(int size) {
        saveBatchSize = size;
        return *this;
    }
    
    ExperimentConfig& setTimeout(double seconds) {
        timeoutSeconds = seconds;
        return *this;
    }
    
    ExperimentConfig& setVerbose(bool v) {
        verbose = v;
        return *this;
    }
    
    ExperimentConfig& setColumns(const std::string& serial, const std::string& parallel) {
        serialColumnName = serial;
        parallelColumnName = parallel;
        return *this;
    }
    
    ExperimentConfig& setBaseTable(const std::string& file) {
        baseTableFile = file;
        return *this;
    }

    ExperimentConfig& setInstanceFile(const std::string& file) {
        instanceListFile = file;
        return *this;
    }
};

// ==================== 实验结果结构 ====================
struct InstanceResult {
    std::string instanceName;
    std::string folderName;
    
    // 时间统计
    double serialTime;
    double parallelTime;
    
    // 解数统计
    uint64_t solutionCount;
    std::string solutionCountStr;
    bool solutionOverflowed;
    
    // 其他统计
    int maxBlockCount;
    int nodeCount;
    
    // 状态标记
    bool serialTimeout;
    bool parallelTimeout;
    bool serialError;
    bool parallelError;
    
    // 额外信息
    std::map<std::string, std::string> metadata;
    
    InstanceResult(const std::string& name) 
        : instanceName(name), 
          serialTime(0), parallelTime(0), 
          solutionCount(0), solutionOverflowed(false),
          maxBlockCount(0), nodeCount(0),
          serialTimeout(false), parallelTimeout(false),
          serialError(false), parallelError(false) {}
    
    // 添加元数据
    void addMetadata(const std::string& key, const std::string& value) {
        metadata[key] = value;
    }
    
    // 获取加速比
    double getSpeedup() const {
        if (parallelTime > 0 && serialTime > 0) {
            return serialTime / parallelTime;
        }
        return 0.0;
    }
    
    // 判断是否成功
    bool isSuccess() const {
        return !serialTimeout && !parallelTimeout && !serialError && !parallelError;
    }
};

// ==================== 工具函数类 ====================
class ExperimentUtils {
public:
    // 分割字符串
    static std::vector<std::string> split(const std::string& str, char delimiter) {
        std::vector<std::string> tokens;
        std::stringstream ss(str);
        std::string item;
        while (std::getline(ss, item, delimiter)) {
            if (!item.empty()) {
                tokens.push_back(item);
            }
        }
        return tokens;
    }
    
    // 确保目录存在
    static void ensureDirectoryExists(const std::string& filePath) {
        std::string dir = filePath.substr(0, filePath.find_last_of("/\\"));
        if (!dir.empty() && !fs::exists(dir)) {
            fs::create_directories(dir);
        }
    }
    
    // 格式化时间
    static std::string formatTime(double seconds) {
        if (seconds < 1.0) {
            return std::to_string(static_cast<int>(seconds * 1000)) + "ms";
        } else if (seconds < 60.0) {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(3) << seconds << "s";
            return oss.str();
        } else {
            int mins = static_cast<int>(seconds / 60);
            int secs = static_cast<int>(seconds) % 60;
            return std::to_string(mins) + "m" + std::to_string(secs) + "s";
        }
    }
    
    // 格式化大数字
    static std::string formatNumber(uint64_t num) {
        std::string str = std::to_string(num);
        std::string result;
        int count = 0;
        for (auto it = str.rbegin(); it != str.rend(); ++it) {
            if (count > 0 && count % 3 == 0) {
                result = ',' + result;
            }
            result = *it + result;
            count++;
        }
        return result;
    }
    
    // 生成分隔线
    static std::string separator(int length = 80, char ch = '=') {
        return std::string(length, ch);
    }
    
    // 解析文件夹路径（支持 path&readMode 格式）
    struct FolderInfo {
        std::string path;
        int readMode;
        std::string name;
    };
    
    static FolderInfo parseFolderPath(const std::string& folderPath, char delimiter = '&') {
        FolderInfo info;
        auto parts = split(folderPath, delimiter);
        
        if (parts.size() >= 2) {
            info.path = parts[0];
            info.readMode = std::stoi(parts[1]);
        } else {
            info.path = folderPath;
            info.readMode = 0; // 默认读取模式
        }
        
        info.name = info.path.substr(info.path.find_last_of("/\\") + 1);
        return info;
    }
    
    // 获取文件夹中的所有文件
    static std::vector<std::string> getFilesInFolder(const std::string& folderPath) {
        std::vector<std::string> files;
        for (const auto& entry : fs::directory_iterator(folderPath)) {
            if (entry.is_regular_file()) {
                std::string fileName = entry.path().stem().string();
                if (fileName != ".DS_Store") {
                    files.push_back(entry.path().string());
                }
            }
        }
        return files;
    }

    static std::vector<std::string> getFilesFromList(const std::string& listPath, const std::string& folderPath) {
        std::vector<std::string> files;
        std::unordered_set<std::string> names;

        // 读取列表文件到集合中，提高查找效率
        std::ifstream listFile(listPath);
        if (!listFile.is_open()) return files;

        std::string line;
        while (std::getline(listFile, line)) {
            if (!line.empty()) names.insert(line);
        }
        listFile.close();

        for (const auto& entry : fs::directory_iterator(folderPath)) {
            if (!entry.is_regular_file()) continue;

            std::string fileName = entry.path().stem().string();
            if (names.count(fileName) && fileName != ".DS_Store") {
                files.push_back(entry.path().string());
            }
        }

        return files;
    }
};

// ==================== 实验统计类 ====================
class ExperimentStats {
private:
    int totalInstances;
    int successInstances;
    int timeoutInstances;
    int errorInstances;
    double totalSerialTime;
    double totalParallelTime;
    
public:
    ExperimentStats() 
        : totalInstances(0), successInstances(0), 
          timeoutInstances(0), errorInstances(0),
          totalSerialTime(0), totalParallelTime(0) {}
    
    void addResult(const InstanceResult& result) {
        totalInstances++;
        if (result.isSuccess()) {
            successInstances++;
            totalSerialTime += result.serialTime;
            totalParallelTime += result.parallelTime;
        } else if (result.serialTimeout || result.parallelTimeout) {
            timeoutInstances++;
        } else {
            errorInstances++;
        }
    }
    
    double getAverageSpeedup() const {
        if (totalParallelTime > 0) {
            return totalSerialTime / totalParallelTime;
        }
        return 0.0;
    }
    
    std::string getSummary() const {
        std::ostringstream oss;
        oss << "统计摘要:\n";
        oss << "  总实例数: " << totalInstances << "\n";
        oss << "  成功: " << successInstances << " (" 
            << (totalInstances > 0 ? 100.0 * successInstances / totalInstances : 0) << "%)\n";
        oss << "  超时: " << timeoutInstances << "\n";
        oss << "  错误: " << errorInstances << "\n";
        if (successInstances > 0) {
            oss << "  平均串行时间: " << ExperimentUtils::formatTime(totalSerialTime / successInstances) << "\n";
            oss << "  平均并行时间: " << ExperimentUtils::formatTime(totalParallelTime / successInstances) << "\n";
            oss << "  平均加速比: " << std::fixed << std::setprecision(2) << getAverageSpeedup() << "x\n";
        }
        return oss.str();
    }
};

// ==================== 求解器接口 ====================
// 用于支持不同的求解器
template<typename SolverType>
class ISolverAdapter {
public:
    virtual ~ISolverAdapter() = default;
    
    // 运行串行求解
    virtual InstanceResult runSerial(const std::string& filePath, 
                                     const std::string& instanceName,
                                     int readMode,
                                     const ExperimentConfig& config) = 0;
    
    // 运行并行求解
    virtual void runParallel(InstanceResult& result,
                            const std::string& filePath,
                            int readMode,
                            const ExperimentConfig& config) = 0;
};


// ==================== CSV接口 ====================
class ICSVWriter {
public:
    virtual ~ICSVWriter() = default;
    virtual void readCSV(const std::string& filePath) = 0;
    virtual void writeCSV(const std::string& filePath) = 0;
    virtual void updateTime(const std::string& instance, double time, 
                           const std::string& column, bool timeout) = 0;
    virtual void updateSols(const std::string& instance, const std::string& count, 
                           const std::string& column) = 0;
    virtual void updateMaxBlock(const std::string& instance, int count) = 0;
};

// ==================== 进度回调 ====================
using ProgressCallback = std::function<void(int current, int total, const std::string& status)>;
using ResultCallback = std::function<void(const InstanceResult& result)>;

#endif // EXPERIMENT_FRAMEWORK_H