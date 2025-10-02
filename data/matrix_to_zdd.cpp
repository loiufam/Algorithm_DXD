#include <iostream>
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <functional>
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;
// ZDD节点结构
struct ZDDNode {
    int variable;           // 变量ID (对应列)
    std::shared_ptr<ZDDNode> low;   // 0-边 (不选择该变量)
    std::shared_ptr<ZDDNode> high;  // 1-边 (选择该变量)
    std::string id;                // 节点ID 唯一
    
    ZDDNode() = default;

    ZDDNode(int var, std::string id) : variable(var), low(nullptr), high(nullptr), id(id) {}
    
    ZDDNode(int var, std::shared_ptr<ZDDNode> l, std::shared_ptr<ZDDNode> h, std::string id) : variable(var), low(l), high(h), id(id) {}    
    
    ZDDNode(const ZDDNode& other) : variable(other.variable), low(other.low), high(other.high), id(other.id) {}

    bool isTerminal() const {
        return variable < 0;
    }

    bool operator==(const ZDDNode& other) const {
        return id == other.id;
    }
};

// 哈希函数
struct ZDDNodeHash {
    std::size_t operator()(const ZDDNode& node) const {

        return std::hash<int>()(node.variable) ^ 
               (std::hash<void*>()(node.low.get()) << 1) ^
               (std::hash<void*>()(node.high.get()) << 2);
    }
};


class ZDDConverter {

private:
    std::shared_ptr<ZDDNode> T;
    std::shared_ptr<ZDDNode> F;
    std::shared_ptr<ZDDNode> rootZDD;
    int nodeCounter;
    std::string newId = "null";

    std::map<int, std::vector<std::shared_ptr<ZDDNode>>> orderZddTable;

    // 唯一表 (unique table)
    std::unordered_set<std::shared_ptr<ZDDNode>> unique_table;
    std::unordered_map<std::string, std::shared_ptr<ZDDNode>> nodeCache;
    
    // 节点id生成
    std::string nodeId(int var, const std::shared_ptr<ZDDNode> low, const std::shared_ptr<ZDDNode> high) {
        size_t h = 0;
        std::hash<std::string> hasher;
        auto mix = [&](const std::string& s) {
            h ^= hasher(s) + 0x9e3779b9 + (h << 6) + (h >> 2);
        };

        mix(std::to_string(var));
        if(low) mix(low->id);
        if(high) mix(high->id);

        std::string id;
        for (int i = 0; i < 4; i++) {
            int digit = (h % 9) + 1; // 保证范围是 1-9
            id.push_back('0' + digit);
            h /= 9;
        }
        return std::string(id);
    }

    // 生成缓存键
    std::string make_cache_key( std::shared_ptr<ZDDNode> p, 
                                std::shared_ptr<ZDDNode> q ) {
        std::string key = p->id + "&" + q->id;
        return key;
    }
    
public:
    int fileCounter = 0;

    ZDDConverter() : nodeCounter(2) {
        // 创建终端节点
        F = std::make_shared<ZDDNode>(-1, "B");
        T = std::make_shared<ZDDNode>(-1, "T");

        unique_table.insert(T);
        unique_table.insert(F);
    }

    size_t getZddNodes() const {
        return nodeCounter;
    }

    // GetNode操作 - 实现ZDD的约简规则
    std::shared_ptr<ZDDNode> GetNode(int top, 
                                   std::shared_ptr<ZDDNode> low, 
                                   std::shared_ptr<ZDDNode> high) {
        // ZDD约简规则：如果high指向0终端，消除该节点
        if (high == F) {
            return low;
        }
        
        // 在唯一表中查找是否已存在相同节点
        // auto temp_node = std::make_shared<ZDDNode>(top, low, high);
        auto it = std::find_if(unique_table.begin(), unique_table.end(),
            [&](const std::shared_ptr<ZDDNode>& node) {
                return !node->isTerminal() && 
                       node->variable == top && 
                       node->low == low && 
                       node->high == high;
            });
        
        if (it != unique_table.end()) {
            return *it;
        }
        
        // 创建新节点并加入唯一表
        if (newId != "null") {
            int id = std::stoi(newId);
            newId = std::to_string(id + 1);
        } else {
            newId = nodeId(top, low, high);
        }
        auto new_node = std::make_shared<ZDDNode>(top, low, high, newId);
        unique_table.insert(new_node);
        return new_node;
    }

    // Union操作：集合并集 - 这是核心的合并算法
    std::shared_ptr<ZDDNode> Union(std::shared_ptr<ZDDNode> P, std::shared_ptr<ZDDNode> Q) {
        // 终止条件
        if (P == F) return Q;
        if (Q == F) return P;
        if (P == Q) return P;
        
        std::string cache_key = make_cache_key(P, Q);
        if (nodeCache.count(cache_key)) {
            return nodeCache[cache_key];
        }
        
        std::shared_ptr<ZDDNode> result;
        
        if (P->isTerminal() && Q->isTerminal()) {
            // 两个都是终端节点
            result = (P == T || Q == T) ? T : F;
        } else if (P->isTerminal()) {
            // P是终端节点
            if (P == T) {
                result = GetNode(Q->variable, Union(P, Q->low), Q->high);
            } else {
                result = Q;
            }
        } else if (Q->isTerminal()) {
            // Q是终端节点
            if (Q == T) {
                result = GetNode(P->variable, Union(P->low, Q), P->high);
            } else {
                result = P;
            }
        } else {
            // 两个都是内部节点
            if (P->variable < Q->variable) {
                result = GetNode(P->variable, Union(P->low, Q), P->high);
            } else if (P->variable > Q->variable) {
                result = GetNode(Q->variable, Union(P, Q->low), Q->high);
            } else { // P->var == Q->var
                result = GetNode(P->variable, Union(P->low, Q->low), Union(P->high, Q->high));
            }
        }
        
        nodeCache[cache_key] = result;
        return result;
    }
    
    std::shared_ptr<ZDDNode> convertMatrixToZDD(const std::string& inputfile, int read_mode) {
        
        std::ifstream file(inputfile);
        if (!file.is_open()) {
            std::cerr << "Cannot open file " << inputfile << std::endl;
            exit(1);
        }
        
        std::string line;
        std::string token;
        std::getline(file, line);
        int cols, rows;

        if (read_mode == 1) {
            std::istringstream iss(line);
            // 读取 "c"
            iss >> token;  // 读取 "c"
            iss >> token;  // 读取 "n"
            iss >> token;  // 读取 "="
            iss >> cols;      // 读取 n 的值


            // 读取 "m" 和其值
            iss >> token;  // 读取 ","
            iss >> token;  // 读取 "m"
            iss >> token;  // 读取 "="
            iss >> rows;      // 读取 m 的值
            
            std::getline(file, line);
        } else {
            std::istringstream iss(line);
            iss >> cols >> rows;
        }
        
        // 将每行转换为列的集合
        std::vector<std::set<int>> rowSets;
        while (std::getline(file, line)) {
            if(line.empty()) continue;
            std::istringstream iss(line);

            if (read_mode == 1 || read_mode == 3) {
                iss >> token;
            } else if (read_mode == 2) {
                iss >> token;
                iss >> token;
            }

            std::set<int> colSet;
            int col;
            while (iss >> col) {
                if (col < 0 || col > cols) {
                    std::cerr << "Invalid column index: " << col << std::endl;
                    exit(1);
                }
                colSet.insert(col);
            }

            if(!colSet.empty()){
                rowSets.push_back(colSet);
            }
        }
        
        std::shared_ptr<ZDDNode> resZDD;
        bool flag = true;
        for(auto& rowSet : rowSets){
            if (flag) {
                resZDD = buildZDDFromRowSet(rowSet);
                flag = false;
            } else {
                auto zdd = buildZDDFromRowSet(rowSet);
                resZDD = Union(resZDD, zdd);
            }
        }

        rootZDD = resZDD;
        return resZDD;
    }
    
    std::shared_ptr<ZDDNode> buildZDDFromRowSet(const std::set<int>& sets) {
        auto node = T;
        for(auto it = sets.crbegin(); it != sets.crend(); ++it) {
            node = GetNode(*it, F, node);
        }
        return node;
    }
    
    void printZDDByBFS() const {
        
        if (rootZDD == nullptr) {
            std::cout << "ZDD is empty" << std::endl;
            return;
        }

        std::queue<std::shared_ptr<ZDDNode>> q;  // 创建队列
        std::unordered_set<std::string> visited; // 防止重复访问

        q.push(rootZDD);
        visited.insert(rootZDD->id);

        while (!q.empty()) { 
            auto curNode = q.front();
            q.pop();

            if (curNode->isTerminal()) {
                std::cout << curNode->id << std::endl;
            } else {
                printf("Internal Node: {id = %s, variable = %d, low -> %s (var: %d), high -> %s (var: %d)}\n", 
                       curNode->id.c_str(), 
                       curNode->variable + 1, 
                       curNode->low ? curNode->low->id.c_str() : "NULL",
                       curNode->low ? curNode->low->variable + 1 : -1,
                       curNode->high ? curNode->high->id.c_str() : "NULL",
                       curNode->high ? curNode->high->variable + 1 : -1);

                if (curNode->low && visited.find(curNode->low->id) == visited.end()) {
                    q.push(curNode->low);
                    visited.insert(curNode->low->id);
                }

                if (curNode->high && visited.find(curNode->high->id) == visited.end()) {
                    q.push(curNode->high);
                    visited.insert(curNode->high->id);
                }  
            }
        }
    }

    void traverseZDD(std::shared_ptr<ZDDNode> node, std::unordered_set<std::string>& visited) { 
        if (!node || node->isTerminal()){
            return;
        }

        if (visited.find(node->id) == visited.end()) {
            visited.insert(node->id);
            orderZddTable[node->variable].push_back(node);
        }

        traverseZDD(node->low, visited);
        traverseZDD(node->high, visited);

        return;
    }

    void outputZDD(const std::string& output_file) {
        std::unordered_set<std::string> visited;
        traverseZDD(rootZDD, visited);

        if(orderZddTable.size() == 0){
            std::cout << "ZDD is empty" << std::endl;
            return;
        }

        if (fs::exists(output_file)) {
            return; // 如果输出文件已存在，跳过
        }

        // 从大到小写入文件
        std::ofstream output(output_file);
        if (!output.is_open()) {
            std::cout << "Cannot open output file" << std::endl;
            return;
        }

        for (auto it = orderZddTable.rbegin(); it != orderZddTable.rend(); ++it) {
            for (const auto& node : it->second) {
                output << node->id << " " << node->variable << " " << node->low->id << " " << node->high->id << std::endl;
            }
        }
        output.close();
    }

    // 枚举所有路径
    void enumeratePaths(std::shared_ptr<ZDDNode> node, std::vector<int>& currentPath) {
        if (!node) return;
        
        if (node->id == "T") {
            std::cout << "Path: {";
            bool first = true;
            for (int var : currentPath) {
                if (!first) std::cout << ", ";
                std::cout << (char)('a' + var);
                first = false;
            }
            std::cout << "}\n";
            return;
        }
        
        if (node->id == "F") {
            return;
        }
        
        // Low edge (不选择当前变量)
        if (node->low) {
            enumeratePaths(node->low, currentPath);
        }
        
        // High edge (选择当前变量)
        if (node->high) {
            currentPath.push_back(node->variable);
            enumeratePaths(node->high, currentPath);
            currentPath.pop_back();
        }
    }

    void enumInterface() {
        std::vector<int> path;
        enumeratePaths(rootZDD, path);
    }


    void batchConvert(const std::string& input_folder, const std::string& output_folder, int read_mode) {
        
        std::ofstream compile_time_file("zdd_compile_time.txt", std::ios::app);
        // 遍历输入文件夹
        int count = 0;
        for (const auto& entry : fs::directory_iterator(input_folder)) { 
            if (entry.is_regular_file()) {
                std::string input_file = entry.path().string();
                fs::path p = entry.path().filename();
                p = p.stem(); // 去掉扩展名
                if (read_mode == 3) {
                    p = p.stem(); // 再去掉一次扩展名
                }
                std::string output_file_name = p.string();
                if (output_file_name == ".DS_Store") continue; // 跳过系统文件
                std::string output_file = output_folder + "/" + output_file_name + ".zdd";
                         
                auto start = std::chrono::high_resolution_clock::now();
                convertMatrixToZDD(input_file, read_mode);
                outputZDD(output_file);
                auto end = std::chrono::high_resolution_clock::now();
                double elapsedSeconds = std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count();
                compile_time_file << output_file_name << ": " << elapsedSeconds << std::endl;
            }
            count++;
        }
        fileCounter += count;
        // std::cout << "Conversion completed." << std::endl;
    }
};

int test() {
    std::vector<std::vector<int>> matrix = {
        {1, 1, 0, 0, 0, 0},  // 行1: a, b
        {1, 1, 1, 0, 1, 0},  // 行2: a, b, c, e  
        {0, 0, 0, 1, 0, 1},  // 行3: d, f
        {0, 0, 1, 1, 0, 1},  // 行4: c, d, f
        {0, 0, 1, 0, 1, 0}   // 行5: c, e
    };
    
    std::cout << "输入矩阵:\n";
    std::cout << "   a b c d e f\n";
    for (size_t i = 0; i < matrix.size(); i++) {
        std::cout << i+1 << " (";
        for (size_t j = 0; j < matrix[i].size(); j++) {
            std::cout << matrix[i][j];
            if (j < matrix[i].size() - 1) std::cout << " ";
        }
        std::cout << ")\n";
    }
    std::cout << "\n";

    ZDDConverter converter;

    std::cout << "ZDD节点数: " << converter.getZddNodes() << "\n";

    std::cout << "\nZDD structure:\n";
    converter.printZDDByBFS();

    std::cout << "\nZDD paths:\n"; 
    converter.enumInterface();

    return 0;
}

// 本项目作为矩阵转换ZDD工具，为D3X提供数据集
int main(int argc, char *argv[]) {

    // cmd format ./matrix_to_zdd <option> <input> <output> <read_mode>
    // if(argc < 5) {
    //     std::cout << "Usage: ./matrix_to_zdd <option> <input> <output> <read_mode>\n";
    //     return 1;
    // }

    ZDDConverter converter;

    std::string inputFolder1 = "./exact_cover_benchmark";
    std::string inputFolder2 = "./set_partitioning_benchmarks";
    std::string inputFolder3 = "./graph_dataset";
    std::string inputFolder4 = "./extra_matrix";
    std::string outputFolder = "../../D3X/data";

    // converter.batchConvert(inputFolder1, outputFolder, 1);
    // converter.batchConvert(inputFolder2, outputFolder, 2);
    // converter.batchConvert(inputFolder3, outputFolder, 3);
    converter.batchConvert(inputFolder4, outputFolder, 1);

    std::cout << "\n=== ZDD输出完成 ===\n"; 

    std::cout << "Total files converted: " << converter.fileCounter << std::endl;
    // if (std::string(argv[1]) == "-b") {

    //     std::string input_folder = argv[2];
    //     std::string output_folder = argv[3];
    //     // 检查输入文件夹是否存在
    //     if (!fs::exists(input_folder) || !fs::is_directory(input_folder)) {
    //         std::cout << "输入文件夹不存在或不是目录: " << input_folder << std::endl;
    //         return 1;
    //     }

    //     converter.batchConvert(input_folder, output_folder, std::stoi(argv[4]));
    //     std::cout << "\n=== 批量转换ZDD完成 ===\n";
    // } else if(std::string(argv[1]) == "-s") {
    //     converter.convertMatrixToZDD(argv[2], std::stoi(argv[4]));
    //     std::cout << "\n=== ZDD构建完成 ===\n";
    //     converter.outputZDD(argv[3]);
    //     std::cout << "\n=== ZDD输出完成 ===\n";
    // }
    
    return 0;
}