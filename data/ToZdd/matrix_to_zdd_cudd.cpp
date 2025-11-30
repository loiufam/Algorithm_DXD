#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <set>
#include <map>
#include <string>
#include <chrono>
#include <filesystem>
#include <cudd.h>

const int TIME_COLUMN_INDEX = 15; 
const std::string INSTANCE_COLUMN = "Instance";
const std::string TIME_COLUMN = "zdd_compile(s)";

struct ZDDInfo {
    std::string id;
    int level;
    std::string lo_id;
    std::string hi_id;

    ZDDInfo() = default;
};

/**
 * 使用CUDD库实现矩阵到ZDD的编译
 */
class CUDDMatrixCompiler {
private:
    DdManager* mgr;  // CUDD管理器
    std::vector<std::set<int>> rowSets;
    std::map<int, std::vector<ZDDInfo>> zddTable; // 变量到节点信息的映射

public:
    int cols;
    int rows;
    CUDDMatrixCompiler() {
        // 初始化CUDD管理器
        // 参数: numVars, numVarsZ, numSlots, cacheSize, maxMemory
        mgr = Cudd_Init(0, 0, CUDD_UNIQUE_SLOTS, CUDD_CACHE_SLOTS, 0);
        
        if (mgr == nullptr) {
            std::cerr << "CUDD initialization failed" << std::endl;
            exit(1);
        }
        
        // 启用ZDD模式
        Cudd_AutodynEnable(mgr, CUDD_REORDER_SAME);
    }
    
    ~CUDDMatrixCompiler() {
        if (mgr) {
            Cudd_Quit(mgr);
        }
    }

    // 从文件读取稀疏矩阵
    bool readFromFile(const std::string& filename, int read_mode = 1) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            std::cerr << "Cannot open file " << filename << std::endl;
            return false;
        }
        
        std::string line;
        std::getline(file, line);
        std::istringstream iss(line);
        std::string token;
        
        if (read_mode == 1) {
            // 格式: c n = cols , m = rows
            iss >> token >> token >> token >> cols;
            iss >> token >> token >> token >> rows;
            std::getline(file, line);  // 跳过第二行
        } else {
            // 格式: cols rows
            iss >> cols >> rows;
        }
        
        int count;
        // 读取每一行
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            std::istringstream iss(line);
            
            if (read_mode == 1) {
                iss >> token;  
            } else if (read_mode == 2) {
                iss >> token >> count;
            } else {
                iss >> count;
            }
            
            if (read_mode > 1 && count <= 0) continue; 

            std::set<int> colSet;
            int col;
            while (iss >> col) {
                if (col >= 1 && col <= cols) {
                    colSet.insert(col);
                }
            }
            
            if (!colSet.empty()) {
                rowSets.push_back(colSet);
            }
        }
        
        file.close();
        // std::cout << "Loaded " << rowSets.size() << " rows" << std::endl;
        return true;
    }

    // 从单个集合构建ZDD
    DdNode* buildZDDFromSet(const std::set<int>& colSet) {
        if (colSet.empty()) {
            DdNode* result = Cudd_ReadOne(mgr);
            Cudd_Ref(result);
            return result;
        }
        
        DdNode* result = Cudd_ReadOne(mgr);
        Cudd_Ref(result);
        
        // 从最大到最小构建路径
        for (auto it = colSet.rbegin(); it != colSet.rend(); ++it) {
            int var = (*it) - 1;

            // 创建单例集合 {var}
            DdNode* varNode = Cudd_zddIthVar(mgr, var);
            if (varNode == nullptr) {
                Cudd_RecursiveDerefZdd(mgr, result);
                return nullptr;
            }
            Cudd_Ref(varNode);
            
            // product操作: result × {var} Cudd_zddProduct
            DdNode* temp = Cudd_zddChange(mgr, result, var);
            if (temp == nullptr) {
                Cudd_RecursiveDerefZdd(mgr, result);
                Cudd_RecursiveDerefZdd(mgr, varNode);
                return nullptr;
            }
            Cudd_Ref(temp);
            
            // 释放旧引用
            Cudd_RecursiveDerefZdd(mgr, result);
            Cudd_RecursiveDerefZdd(mgr, varNode);
            
            result = temp;
        }
        
        return result;
    }

    // 编译整个矩阵为ZDD
    DdNode* compile() {
        if (rowSets.empty()) {
            return Cudd_ReadZero(mgr);
        }
        
        // 初始化为空ZDD（0终端）
        DdNode* result = Cudd_ReadZero(mgr);
        Cudd_Ref(result);
        
        // std::cout << "Building ZDD..." << std::endl;
        
        // 逐行构建并Union
        for (size_t i = 0; i < rowSets.size(); ++i) {
            // 为当前行构建ZDD
            DdNode* rowZdd = buildZDDFromSet(rowSets[i]);
            
            // Union操作
            DdNode* temp = Cudd_zddUnion(mgr, result, rowZdd);
            Cudd_Ref(temp);
            
            // 释放旧的引用
            Cudd_RecursiveDerefZdd(mgr, result);
            Cudd_RecursiveDerefZdd(mgr, rowZdd);
            
            result = temp;
            
            // if ((i + 1) % 100 == 0 || i == rowSets.size() - 1) {
            //     std::cout << "Processed " << (i + 1) << "/" << rowSets.size() 
            //               << " rows\r" << std::flush;
            // }
        }
        // std::cout << std::endl;
        
        return result;
    }

    // 输出ZDD统计信息
    void printStats(DdNode* zdd) {
        if (zdd == nullptr) return;
        
        int nodeCount = Cudd_zddDagSize(zdd);
        double pathCount = Cudd_zddCountMinterm(mgr, zdd, cols);
        
        std::cout << "\n=== ZDD Statistics ===" << std::endl;
        std::cout << "Node count: " << nodeCount << std::endl;
        std::cout << "Solution count: " << pathCount << std::endl;
        std::cout << "Peak nodes: " << Cudd_ReadPeakNodeCount(mgr) << std::endl;
    }

    // 遍历ZDD并输出到文件
    void outputZDD(DdNode* zdd, const std::string& filename) {
        if (zdd == nullptr) return;
        
        std::ofstream out(filename);
        if (!out.is_open()) {
            std::cerr << "Cannot open output file" << std::endl;
            return;
        }
        
        // 建立节点ID映射
        std::map<DdNode*, std::string> nodeIds;
        int idCounter = 1 + cols + rows;
        
        // 递归遍历ZDD
        traverse(zdd, nodeIds, idCounter);

        if (!zddTable.empty()) {
            for (auto it = zddTable.rbegin(); it != zddTable.rend(); ++it) {
                int var = it->first;
                for (const auto& info : it->second) {
                    out << info.id << " " << var << " " << info.lo_id << " " << info.hi_id << "\n";
                }
            }
        }
        
        out.close();
        std::cout << "ZDD written to " << filename << std::endl;
    }

private:
    void traverse(DdNode* node,
                          std::map<DdNode*, std::string>& nodeIds, 
                          int& idCounter) {

        if (node == nullptr || Cudd_IsConstant(node)) return;
        
        if (nodeIds.find(node) != nodeIds.end()) {
            return;
        }
        
        // 分配节点ID
        std::string currentId = std::to_string(idCounter++);
        nodeIds[node] = currentId;
        
        // 获取节点信息
        int level = Cudd_NodeReadIndex(node) + 1;  // level从1开始
        DdNode* lo = Cudd_E(node);  // else分支 (low)
        DdNode* hi = Cudd_T(node);  // then分支 (high)

        if (!Cudd_IsConstant(lo)) {
            traverse(lo, nodeIds, idCounter);
        }
        if (!Cudd_IsConstant(hi)) {
            traverse(hi, nodeIds, idCounter);
        }

        // 确定子节点ID
        std::string loId, hiId;
        
        if (Cudd_IsConstant(lo)) {
            loId = (lo == Cudd_ReadZero(mgr)) ? "B" : "T";
        } else {
            loId = nodeIds[Cudd_Regular(lo)];
        }
        
        if (Cudd_IsConstant(hi)) {
            hiId = (hi == Cudd_ReadZero(mgr)) ? "B" : "T";
        } else {
            hiId = nodeIds[Cudd_Regular(hi)];
        }
        
        zddTable[level].push_back({currentId, level, loId, hiId});
    }

public:
    // 导出为DOT格式（可视化）
    void exportDot(DdNode* zdd, const std::string& filename) {
        if (zdd == nullptr) return;
        
        FILE* fp = fopen(filename.c_str(), "w");
        if (fp == nullptr) {
            std::cerr << "Cannot open DOT file" << std::endl;
            return;
        }
        
        // 创建变量名数组，显示level值
        char** inames = new char*[cols];
        for (int i = 0; i < cols; ++i) {
            inames[i] = new char[20];
            snprintf(inames[i], 20, "v%d", i);
        }
        
        char** onames = nullptr;
        
        Cudd_zddDumpDot(mgr, 1, &zdd, inames, onames, fp);
        fclose(fp);
        
        // 释放内存
        for (int i = 0; i < cols; ++i) {
            delete[] inames[i];
        }
        delete[] inames;
        
        std::cout << "DOT file written to " << filename << std::endl;
    }

    // 自定义DOT导出，区分边样式
    void exportDotCustom(DdNode* zdd, const std::string& filename) {
        if (zdd == nullptr) return;
        
        std::ofstream out(filename);
        if (!out.is_open()) {
            std::cerr << "Cannot open DOT file" << std::endl;
            return;
        }
        
        out << "digraph ZDD {\n";
        out << "  rankdir=TB;\n";
        out << "  node [shape=circle];\n";
        out << "  edge [arrowhead=none];\n\n";
        
        // 终端节点
        out << "  F [shape=box, label=\"0\"];\n";
        out << "  T [shape=box, label=\"1\"];\n\n";
        
        std::map<DdNode*, int> nodeIds;
        int idCounter = 1 + cols + rows;
        
        // 递归生成DOT
        generateDotNodes(zdd, out, nodeIds, idCounter);
        
        out << "}\n";
        out.close();
        
        std::cout << "Custom DOT file written to " << filename << std::endl;
    }
private:
    void generateDotNodes(DdNode* node, std::ofstream& out,
                         std::map<DdNode*, int>& nodeIds, int& idCounter) {
        if (node == nullptr || Cudd_IsConstant(node)) {
            return;
        }
        
        DdNode* regularNode = Cudd_Regular(node);
        
        if (nodeIds.find(regularNode) != nodeIds.end()) {
            return;
        }
        
        int nodeId = idCounter++;
        nodeIds[regularNode] = nodeId;
        
        int level = Cudd_NodeReadIndex(regularNode) + 1;  // level从1开始
        DdNode* lo = Cudd_E(regularNode);
        DdNode* hi = Cudd_T(regularNode);
        
        // 输出节点，标签显示level
        out << "  n" << nodeId << " [label=\"" << level << "\"];\n";
        
        // 递归处理子节点
        generateDotNodes(lo, out, nodeIds, idCounter);
        generateDotNodes(hi, out, nodeIds, idCounter);
        
        // 输出边
        if (Cudd_IsConstant(lo)) {
            std::string loTarget = (lo == Cudd_ReadZero(mgr)) ? "F" : "T";
            out << "  n" << nodeId << " -> " << loTarget 
                << " [style=dashed, color=red];\n";
        } else {
            out << "  n" << nodeId << " -> n" << nodeIds[Cudd_Regular(lo)]
                << " [style=dashed, color=red];\n";
        }
        
        if (Cudd_IsConstant(hi)) {
            std::string hiTarget = (hi == Cudd_ReadZero(mgr)) ? "F" : "T";
            out << "  n" << nodeId << " -> " << hiTarget 
                << " [style=solid, color=blue];\n";
        } else {
            out << "  n" << nodeId << " -> n" << nodeIds[Cudd_Regular(hi)]
                << " [style=solid, color=blue];\n";
        }
    }
};

// CSV辅助函数：读取CSV文件，保留列顺序
void readCSV(const std::string& filename,
            std::vector<std::string>& headers,
            std::vector<std::vector<std::string>>& rows) {

    std::ifstream file(filename);
    
    if (!file.is_open()) {
        std::cerr << "Warning: Cannot open CSV file: " << filename << " (will create new)" << std::endl;
        return;
    }
    
    std::string line;
    
    // 读取表头
    if (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string cell;
        while (std::getline(ss, cell, ',')) {
            headers.push_back(cell);
        }
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

}

// CSV辅助函数：写入CSV文件
void writeCSV(const std::string& filename, 
              const std::vector<std::string>& headers,
              const std::vector<std::vector<std::string>>& rows) {
    std::ofstream file(filename);
    
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open CSV file for writing: " << filename << std::endl;
        return;
    }
    
    // 写入表头
    for (size_t i = 0; i < headers.size(); i++) {
        file << headers[i];
        if (i < headers.size() - 1) file << ",";
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
}

void updateRecord(  std::vector<std::string>& headers,
                    std::vector<std::vector<std::string>>& rows,
                    const std::string& instanceName,
                    double compileTime,
                    int ROWS,
                    int COLS) {

    // 查找是否已存在该实例（在第1列查找）
    bool found = false;
    for (auto& row : rows) {
        // 确保行有足够的列
        while (row.size() < headers.size()) {
            row.push_back("");
        }
        
        if (!row.empty() && row[0] == instanceName) {
            // 更新第13列的时间
            row[TIME_COLUMN_INDEX] = std::to_string(compileTime);
            found = true;
            break;
        }
    }
    
    // 如果不存在，添加新记录
    if (!found) {
        std::vector<std::string> newRow(headers.size(), "");
        newRow[0] = instanceName; // 第1列：实例名
        newRow[TIME_COLUMN_INDEX] = std::to_string(compileTime); 
        newRow[1] = std::to_string(COLS);
        newRow[2] = std::to_string(ROWS);
        rows.push_back(newRow);
    }
    
}


int main(int argc, char* argv[]) {

    // if (argc < 4) {
    //     std::cout << "Usage: " << argv[0] << " <input_matrix> <output_zdd> <read_mode> [output_dot]" << std::endl;
    //     std::cout << "Example: " << argv[0] << " matrix.txt output.zdd 1 output.dot" << std::endl;
    //     return 1;
    // }


    if (argc < 3) {
        // 文件夹路径
        std::vector<std::string> filePaths;
        // std::string folderPath1 = "../exact_cover_benchmark 1";
        // std::string folderPath2 = "../set_partitioning_benchmarks 2";
        // std::string folderPath3 = "../graph_matrix 3";
        std::string folderPath4 = "../graph_matrix/10_blocks 3";

        // filePaths.push_back(folderPath1);
        // filePaths.push_back(folderPath2);
        // filePaths.push_back(folderPath3);
        filePaths.push_back(folderPath4);

        // 读取csv文件
        std::vector<std::string> headers;
        std::vector<std::vector<std::string>> rows;

        const std::string table = "../../exp_results.csv";
        const std::string outputPath = "../../../D3X/data/10_blocks/";
        readCSV(table, headers, rows);

        // 遍历文件夹中的文件
        std::string folderName;
        int mode;
        for (const auto& folderPath : filePaths) {
            std::stringstream ss(folderPath);
            ss >> folderName;
            ss >> mode;
            std::cout << "Start compile folder: " << folderName << ", mode: " << mode << std::endl;
            for (const auto& entry : std::filesystem::directory_iterator(folderName)) {
                if (entry.is_regular_file()) {
                    std::string filePath = entry.path().string();
                    std::string fileName = entry.path().stem().string();
                    std::string outputFile = outputPath + fileName + ".zdd";

                    CUDDMatrixCompiler compiler;

                    // 加载矩阵
                    if (!compiler.readFromFile(filePath, mode)) {
                        std::cerr << "Failed to read file: " << filePath << std::endl;
                        return 1;
                    }
                    
                    std::cout << "Compiling " << fileName << std::endl;

                    // 编译为ZDD
                    auto start = std::chrono::high_resolution_clock::now();
                    auto zdd = compiler.compile();
                    auto end = std::chrono::high_resolution_clock::now();
                    
                    double elapsed = std::chrono::duration<double>(end - start).count();
                    std::cout << "Compilation time: " << elapsed << " seconds" << std::endl;

                    updateRecord(headers, rows, fileName, elapsed, compiler.rows, compiler.cols);
                    
                    // 输出ZDD文件
                    compiler.outputZDD(zdd, outputFile);
                    
                }
            }
        }    
        writeCSV(table, headers, rows); // 写入csv文件

        std::cout << "All compilations completed. Results saved to " << table << std::endl;
    } else {
        
        std::string inputFile = argv[1];
        std::string outputFile = argv[2];
        int read_mode = std::stoi(argv[3]);
        std::string dotFile = (argc > 4) ? argv[4] : "";

        CUDDMatrixCompiler compiler;

        // 加载矩阵
        if (!compiler.readFromFile(inputFile, read_mode)) {
            std::cerr << "Failed to read file: " << inputFile << std::endl;
            return 1;
        }
        
        std::cout << "Compiling " << inputFile << std::endl;

        // 编译为ZDD
        auto zdd = compiler.compile();

        // 输出ZDD文件
        compiler.outputZDD(zdd, outputFile);
        
        // 导出DOT文件用于可视化
        if (!dotFile.empty()) {
            compiler.exportDotCustom(zdd, dotFile);
        }

    }
    
    return 0;
}

// compile command
// g++ -std=c++11 -O3 -o cudd_compile matrix_to_zdd_cudd.cpp -lcudd