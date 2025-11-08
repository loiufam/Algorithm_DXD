#ifndef MYCSV_H
#define MYCSV_H

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <set>
#include <map>
#include <string>
#include <filesystem>
#include <iomanip>

class MyCSV {
private:
    std::vector<std::string> headers;
    std::vector<std::vector<std::string>> rows;

public:
    MyCSV() = default;
    ~MyCSV() = default;

    // 读取CSV文件
    bool readCSV(const std::string& filename) {
        std::ifstream file(filename);

        if (!file.is_open()) {
            std::cerr << "Failed to open file: " << filename << std::endl;
            return false;
        }

        std::string line;

        // 读取表头
        if (std::getline(file, line)) {
            std::stringstream ss(line);
            std::string cell;
            while (std::getline(ss, cell, ',')) {
                headers.push_back(cell);
            }
        } else {
            std::cerr << "Empty CSV file: " << filename << std::endl;
            return false;
        }
        
        // 读取数据行
        while (std::getline(file, line)) {
            std::vector<std::string> row;
            std::stringstream ss(line);
            std::string cell;
            
            int col = 0;
            while (std::getline(ss, cell, ',')) {
                row.push_back(cell);
                col++;
            }
            
            // 确保行有足够的列（用空字符串填充）
            while (row.size() < headers.size()) {
                row.push_back("");
            }
            
            rows.push_back(row);
        }
        file.close();
        return true;
    }

    // 写入CSV文件
    bool writeCSV(const std::string& filename) {

        std::ofstream file(filename);
        if (!file.is_open()) {
            std::cerr << "Failed to open file: " << filename << std::endl;
            return false;
        }

        // 写入表头
        for (size_t i = 0; i < headers.size(); ++i) {
            file << headers[i];
            if (i < headers.size() - 1) {
                file << ",";
            }
        }
        file << "\n";

        // 写入数据行
        for (const auto& row : rows) {
            for (size_t i = 0; i < row.size(); i++) {
                file << row[i];
                if (i < row.size() - 1) file << ",";
            }
            file << "\n";
        }

        file.close();
        return true;
    }

    int findColumnIndex(const std::string& columnName) {
        for (size_t i = 0; i < headers.size(); ++i) {
            if (headers[i] == columnName) {
                return i;
            }
        }
        return -1;
    }

    void updateTime(const std::string& instanceName, 
                        double time, 
                        const std::string& columnName,
                        bool isTimeout = false) {



        int columnIndex = findColumnIndex(columnName);
        if (columnIndex == -1) {
            std::cerr << "Column not found: " << columnName << std::endl;
            return;
        }

        bool found = false;
        for (auto& row : rows) {
            // 确保行有足够的列
            while (row.size() < headers.size()) {
                row.push_back("");
            }
            
            if (!row.empty() && row[0] == instanceName) {
                if (!isTimeout) {
                    std::ostringstream oss;
                    if (time < 0.001) {
                        // 小于 1ms 的情况
                        row[columnIndex] = "<1ms";
                    } else {
                        oss << std::fixed << std::setprecision(4) << time; // 保留 4 位小数
                        row[columnIndex] = oss.str();
                    }
                } else {
                    row[columnIndex] = "timeout";
                }
                found = true;
                break;
            }
        }

        // 如果不存在，添加新记录
        if (!found) {
            std::vector<std::string> newRow(headers.size(), "");
            newRow[0] = instanceName; // 第1列：实例名
            if (!isTimeout) {
                std::ostringstream oss;
                if (time < 0.001) {
                    // 小于 1ms 的情况
                    newRow[columnIndex] = "<1ms";
                } else {
                    oss << std::fixed << std::setprecision(4) << time; // 保留 4 位小数
                    newRow[columnIndex] = oss.str();
                }
            } else {
                newRow[columnIndex] = "timeout";
            }
            rows.push_back(newRow);
        }
    }

    void updateSols(const std::string& instanceName, 
                        string& count, 
                        const std::string& algName,
                        const std::string& columnName = "#sols") {

        int columnIndex = findColumnIndex(columnName);
        if (columnIndex == -1) {
            std::cerr << "Column not found: " << columnName << std::endl;
            return;
        }

        bool found = false;
        for (auto& row : rows) {
            // 确保行有足够的列
            while (row.size() < headers.size()) {
                row.push_back("");
            }
            
            if (!row.empty() && row[0] == instanceName) {
                // 先检查原有的的解
                if (!row[columnIndex].empty() && (algName == "DLX" || algName == "DXZ")) {
                    string existingCount = row[columnIndex];
                    if (count != existingCount) {
                        std::cerr << "Warning: solve failed for instance : " << instanceName << std::endl;
                        int algIndex = findColumnIndex(algName);
                        if (algIndex != -1) {
                            row[algIndex] = "warning: solve failed";
                        }
                    }
                } else {
                    row[columnIndex] = count;
                }
                found = true;
                break;
            }
        }

        // 如果不存在，添加新记录
        if (!found) {
            std::vector<std::string> newRow(headers.size(), "");
            newRow[0] = instanceName; // 第1列：实例名
            newRow[columnIndex] = count;
            rows.push_back(newRow);
        }
    }

    void updateMaxBlock(const std::string& instanceName, 
                        size_t count, 
                        const std::string& columnName = "#Max_B") {
        
        int columnIndex = findColumnIndex(columnName);
        if (columnIndex == -1) {
            std::cerr << "Column not found: " << columnName << std::endl;
            return;
        }

        bool found = false;
        for (auto& row : rows) {
            // 确保行有足够的列
            while (row.size() < headers.size()) {
                row.push_back("");
            }
            
            if (!row.empty() && row[0] == instanceName) {
                row[columnIndex] = std::to_string(count);
                found = true;
                break;
            }
        }

        // 如果不存在，添加新记录
        if (!found) {
            std::vector<std::string> newRow(headers.size(), "");
            newRow[0] = instanceName; // 第1列：实例名
            newRow[columnIndex] = std::to_string(count);
            rows.push_back(newRow);
        }
    }    
};


#endif // MYCSV_H