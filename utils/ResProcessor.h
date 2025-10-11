#ifndef EXPERIMENT_RESULT_PROCESSOR_H
#define EXPERIMENT_RESULT_PROCESSOR_H

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <iomanip>
#include <algorithm>
#include <memory>

// 算法类型枚举
enum class AlgorithmType {
    DLX,
    DXZ,
    D3X,
    DXD_S,
    DXD_M,
    Model_Count
};

// 实验结果基类
class ExperimentResult {
public:
    std::string instance_name;
    std::string runtime;
    int solution_count;
    bool valid;
    
    ExperimentResult() : runtime(""), solution_count(-1), valid(true) {}
    virtual ~ExperimentResult() = default;
};

// DXD算法特有的结果（包含Max_B）
class DXDResult : public ExperimentResult {
public:
    int max_blocks;
    
    DXDResult() : max_blocks(-1) {}
};

// 表格行数据
class TableRow {
public:
    std::string instance;
    int cols;
    int rows;
    
    // 各算法的运行时间
    std::string dlx_time;
    std::string dxz_time;
    std::string d3x_time;
    std::string dxd_s_time;
    std::string dxd_m_time;
    
    // 解数和最大块数
    int solution_count;
    int max_blocks;
    
    // 标记是否有失败的结果
    bool solution_failed;
    
    TableRow() : cols(-1), rows(-1), 
                 dlx_time(""), dxz_time(""), d3x_time(""), 
                 dxd_s_time(""), dxd_m_time(""),
                 solution_count(-1), max_blocks(-1), 
                 solution_failed(false) {}
};

// Excel表格处理器
class ExcelTableProcessor {
private:
    std::map<std::string, TableRow> table_data;
    std::vector<std::string> instance_order; // 保持实例顺序
    
    // 算法列映射
    std::map<AlgorithmType, int> column_map = {
        {AlgorithmType::DLX, 3},
        {AlgorithmType::DXZ, 4},
        {AlgorithmType::D3X, 5},
        {AlgorithmType::DXD_S, 6},
        {AlgorithmType::DXD_M, 7}
    };
    
public:
    // 从CSV文件读取现有表格
    bool loadFromCSV(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            std::cerr << "Warning: Cannot open file " << filename << " for reading." << std::endl;
            return false;
        }
        
        std::string line;
        // 跳过表头
        std::getline(file, line);
        
        while (std::getline(file, line)) {
            TableRow row = parseCSVLine(line);
            if (!row.instance.empty()) {
                table_data[row.instance] = row;
                instance_order.push_back(row.instance);
            }
        }
        
        file.close();
        return true;
    }
    
    // 保存到CSV文件
    bool saveToCSV(const std::string& filename) {
        std::ofstream file(filename);
        if (!file.is_open()) {
            std::cerr << "Error: Cannot open file " << filename << " for writing." << std::endl;
            return false;
        }
        
        // 写入表头
        file << "Instance,#cols,#rows,DLX,DXZ,D3X(+c),DXD_S,DXD_M(16),#sols,#Max_B" << std::endl;
        
        // 写入数据
        for (const auto& instance : instance_order) {
            const TableRow& row = table_data[instance];
            file << std::fixed << std::setprecision(6);
            
            file << row.instance << ",";
            file << (row.cols >= 0 ? std::to_string(row.cols) : "") << ",";
            file << (row.rows >= 0 ? std::to_string(row.rows) : "") << ",";
            file << (row.dlx_time != "" ? row.dlx_time : "") << ",";
            file << (row.dxz_time != "" ? row.dxz_time : "") << ",";
            file << (row.d3x_time != "" ? row.d3x_time : "") << ",";
            file << (row.dxd_s_time != "" ? row.dxd_s_time : "") << ",";
            file << (row.dxd_m_time != "" ? row.dxd_m_time : "") << ",";
            
            // 处理解数
            // if (row.solution_failed) {
            //     file << "failed,";
            // } else if (row.solution_count >= 0) {
            file << row.solution_count << ",";
            // } else {
            //     file << ",";
            // }
            
            // Max_B
            file << (row.max_blocks >= 0 ? std::to_string(row.max_blocks) : "");
            file << std::endl;
        }
        
        file.close();
        return true;
    }
    
    // 更新算法结果
    void updateResult(const std::string& instance_name, 
                     AlgorithmType algo_type, 
                     const ExperimentResult& result) {
        
        // 如果实例不存在，添加新行
        if (table_data.find(instance_name) == table_data.end()) {
            table_data[instance_name] = TableRow();
            table_data[instance_name].instance = instance_name;
            instance_order.push_back(instance_name);
        }
        
        TableRow& row = table_data[instance_name];
        
        // 验证解数一致性
        if (result.solution_count >= 0) {
            if (row.solution_count != result.solution_count 
                    && row.solution_count != -1) {
                row.solution_failed = true;
                std::cerr << "Warning: Solution count mismatch for " << instance_name 
                         << " (existing: " << row.solution_count 
                         << ", new: " << result.solution_count << ")" << std::endl;
            }
        }
        
        // 根据算法类型更新对应的时间
        switch (algo_type) {
            case AlgorithmType::DLX:
                row.dlx_time = result.runtime;
                break;
            case AlgorithmType::DXZ:
                row.dxz_time = result.runtime;
                break;
            case AlgorithmType::D3X:
                row.d3x_time = result.runtime;
                break;
            case AlgorithmType::DXD_S:
                row.dxd_s_time = result.runtime;
                // DXD算法特有的Max_B
                if (auto dxd_result = dynamic_cast<const DXDResult*>(&result)) {
                    row.max_blocks = dxd_result->max_blocks;
                }
                break;
            case AlgorithmType::DXD_M:
                row.dxd_m_time = result.runtime;
                break;
            default:
                std::cerr << "Unknown algorithm type" << std::endl;
                break;
        }
    }
    
    // 批量更新结果
    void batchUpdate(const std::vector<std::pair<std::string, std::shared_ptr<ExperimentResult>>>& results,
                    AlgorithmType algo_type) {
        for (const auto& [instance, result] : results) {
            updateResult(instance, algo_type, *result);
        }
    }
    
private:
    std::string trim(const std::string& str) {
        if (str.empty()) return str;
        
        size_t start = str.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        
        size_t end = str.find_last_not_of(" \t\r\n");
        return str.substr(start, end - start + 1);
    }

    // 解析CSV行
    TableRow parseCSVLine(const std::string& line) {
        TableRow row;
        std::stringstream ss(line);
        std::string field;
        int col = 0;
        
        while (std::getline(ss, field, ',')) {
            if (col > 9) break; // 多余列忽略
            
            // 安全地去除空格
            field = trim(field);

            // std::cout << "Field: '" << field << "'" << std::endl;
    
            switch (col) {
                case 0: row.instance = field; break;
                case 1: if (!field.empty()) row.cols = std::stoi(field); break;
                case 2: if (!field.empty()) row.rows = std::stoi(field); break;
                case 3: if (!field.empty()) row.dlx_time = field; break;
                case 4: if (!field.empty()) row.dxz_time = field; break;
                case 5: if (!field.empty()) row.d3x_time = field; break;
                case 6: if (!field.empty()) row.dxd_s_time = field; break;
                case 7: if (!field.empty()) row.dxd_m_time = field; break;
                case 8: 
                    if (field == "failed") {
                        row.solution_failed = true;
                    } else if (!field.empty()) {
                        row.solution_count = std::stoi(field);
                    }
                    break;
                case 9: if (!field.empty()) row.max_blocks = std::stoi(field); break;
            }
            col++;
        }
        // std::cout << "Parsed ended " << std::endl;
        
        return row;
    }
};

// 主处理类
class ExperimentProcessor {
private:
    ExcelTableProcessor table_processor;
    
public:
    
    // 加载现有表格
    bool loadTable(const std::string& filename) {
        return table_processor.loadFromCSV(filename);
    }
    
    // 保存表格
    bool saveTable(const std::string& filename) {
        return table_processor.saveToCSV(filename);
    }
    
    // 处理单个结果文件
    void processResultFile(const std::shared_ptr<ExperimentResult> result, AlgorithmType algo_type) {
        // if (parsers.find(algo_type) == parsers.end()) {
        //     std::cerr << "No parser registered for algorithm type" << std::endl;
        //     return;
        // }
        
        // auto result = parsers[algo_type]->parse(filename);
        if (result->valid) {
            table_processor.updateResult(result->instance_name, algo_type, *result);
            std::cout << "Updated " << result->instance_name << std::endl;
        }
    }


};

// 使用示例

// ExperimentProcessor processor;

//  加载现有表格（如果存在）
// std::string table_file = "experiment_results.csv";
// processor.loadTable(table_file);

//  处理DLX算法结果
// processor.processResultFile("Funet_DLX_result.txt", AlgorithmType::DLX);
// processor.processResultFile("UsSignal_DLX_result.txt", AlgorithmType::DLX);

//  批量处理DXD_S算法结果
// std::vector<std::string> dxd_files = {
//     "Funet_DXD_S_result.txt",
//     "UsSignal_DXD_S_result.txt"
// };
// processor.processBatch(dxd_files, AlgorithmType::DXD_S);

//  保存更新后的表格
// processor.saveTable(table_file);

// std::cout << "Table updated successfully!" << std::endl;



#endif // EXPERIMENT_RESULT_PROCESSOR_H