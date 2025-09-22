#include <iostream>
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>

// ZDD节点结构
struct ZDDNode {
    int variable;           // 变量ID (对应列)
    std::shared_ptr<ZDDNode> low;   // 0-边 (不选择该变量)
    std::shared_ptr<ZDDNode> high;  // 1-边 (选择该变量)
    std::string id;                // 节点ID
    
    ZDDNode(int var, std::string id) : variable(var), low(nullptr), high(nullptr), id(id) {}
    
    ZDDNode(int var, std::shared_ptr<ZDDNode> l, std::shared_ptr<ZDDNode> h, std::string id) : variable(var), low(l), high(h), id(id) {}    
    
    bool isTerminal() const {
        return variable < 0;
    }

    bool operator==(const ZDDNode& other) const {
        return variable == other.variable && low == other.low && high == other.high && id == other.id;
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
    int nodeCounter;

    // 唯一表 (unique table)
    std::unordered_set<std::shared_ptr<ZDDNode>> unique_table;
    std::unordered_map<std::string, std::shared_ptr<ZDDNode>> nodeCache;
    
    std::string nodeKey(int var, std::shared_ptr<ZDDNode> low, std::shared_ptr<ZDDNode> high) {
        return std::to_string(var) + "_" + (low ? low->id : "Lo") + "_" + (high ? high->id : "Hi");
    }
    
public:
    ZDDConverter() : nodeCounter(2) {
        // 创建终端节点
        F = std::make_shared<ZDDNode>(-1, "B");
        T = std::make_shared<ZDDNode>(-1, "T");

        unique_table.insert(T);
        unique_table.insert(F);
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
        auto temp_node = std::make_shared<ZDDNode>(top, low, high);
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
        auto new_node = std::make_shared<ZDDNode>(top, low, high);
        unique_table.insert(new_node);
        return new_node;
    }
    
    std::shared_ptr<ZDDNode> convertMatrixToZDD(const std::vector<std::vector<int>>& matrix) {
        if (matrix.empty() || matrix[0].empty()) {
            return F;
        }
        
        int cols = matrix[0].size();
        
        // 将每行转换为列的集合
        std::vector<std::set<int>> rowSets;
        for (size_t i = 0; i < matrix.size(); i++) {
            std::set<int> rowSet;
            for (size_t j = 0; j < matrix[i].size(); j++) {
                if (matrix[i][j] == 1) {
                    rowSet.insert(j);
                }
            }
            if (!rowSet.empty()) {
                rowSets.push_back(rowSet);
            }
        }
        
        // std::cout << "转换后的行集合:\n";
        // for (size_t i = 0; i < rowSets.size(); i++) {
        //     std::cout << "集合" << (i+1) << ": {";
        //     bool first = true;
        //     for (int col : rowSets[i]) {
        //         if (!first) std::cout << ", ";
        //         std::cout << (char)('a' + col);
        //         first = false;
        //     }
        //     std::cout << "}\n";
        // }
        // std::cout << "\n";
        
        return buildZDD(rowSets, 0, cols - 1);
    }
    
private:
    std::shared_ptr<ZDDNode> buildZDD(const std::vector<std::set<int>>& sets, int currentVar, int maxVar) {
 
        // 移除空集合
        std::vector<std::set<int>> validSets;
        for (const auto& s : sets) {
            if (!s.empty()) {
                validSets.push_back(s);
            }
        }
        
        // 如果没有有效集合，返回True（表示一个有效的解）
        if (validSets.empty()) {
            std::cout << "  -> 返回True（无剩余集合）\n";
            return T;
        }
        
        // 如果当前变量超过最大变量，但还有未处理的集合，返回False
        if (currentVar > maxVar) {
            std::cout << "  -> 返回False（变量超界但还有集合）\n";
            return F;
        }
        
        // 检查是否所有集合都不包含当前变量
        bool hasCurrentVar = false;
        for (const auto& s : validSets) {
            if (s.count(currentVar)) {
                hasCurrentVar = true;
                break;
            }
        }
        
        if (!hasCurrentVar) {
            // 当前变量不在任何集合中，跳过到下一个变量
            std::cout << "  -> 跳过变量" << (char)('a' + currentVar) << "（不在任何集合中）\n";
            return buildZDD(validSets, currentVar + 1, maxVar);
        }
        
        // 分割集合：不包含当前变量的（low分支）和包含当前变量的（high分支）
        std::vector<std::set<int>> lowSets, highSets;
        
        for (const auto& s : validSets) {
            if (s.count(currentVar)) {
                // 包含当前变量，移除该变量后加入high分支
                std::set<int> newSet = s;
                newSet.erase(currentVar);
                highSets.push_back(newSet);
            } else {
                // 不包含当前变量，直接加入low分支
                lowSets.push_back(s);
            }
        }
        
        std::cout << "  -> Low分支: " << lowSets.size() << " 个集合\n";
        std::cout << "  -> High分支: " << highSets.size() << " 个集合\n";
        
        // 递归构建子ZDD
        auto lowChild = buildZDD(lowSets, currentVar + 1, maxVar);
        auto highChild = buildZDD(highSets, currentVar + 1, maxVar);
        
        // ZDD规约规则
        if (highChild == F) {
            // 如果选择当前变量导致False，则跳过当前变量
            std::cout << "  -> High分支为False，返回Low分支\n";
            return lowChild;
        }
        
        if (lowChild == highChild) {
            // 如果两个分支相同，跳过当前变量
            std::cout << "  -> 两个分支相同，跳过变量\n";
            return lowChild;
        }
        
        // 检查节点缓存，避免重复节点
        std::string key = nodeKey(currentVar, lowChild, highChild);
        if (nodeCache.find(key) != nodeCache.end()) {
            std::cout << "  -> 使用缓存节点\n";
            return nodeCache[key];
        }
        
        // 创建新节点
        auto node = std::make_shared<ZDDNode>(currentVar);
        node->low = lowChild;
        node->high = highChild;
        node->id = nodeCounter++;
        
        nodeCache[key] = node;
        
        std::cout << "  -> 创建节点ID=" << node->id << ", 变量=" << (char)('a' + currentVar) << "\n";
        
        return node;
    }
    
private:
    void printZDDDotHelper(std::shared_ptr<ZDDNode> node, std::ostream& os, std::set<std::shared_ptr<ZDDNode>>& visited) {
        if (!node || visited.count(node)) return;
        visited.insert(node);
        
        if (!node->isTerminal()) {
            char varName = 'a' + node->variable;
            os << "  " << node->id << " [label=\"" << varName << "\"];\n";
            
            if (node->low) {
                os << "  " << node->id << " -> " << node->low->id << " [style=dashed];\n";
                printZDDDotHelper(node->low, os, visited);
            }
            
            if (node->high) {
                os << "  " << node->id << " -> " << node->high->id << " [style=solid];\n";
                printZDDDotHelper(node->high, os, visited);
            }
        }
    }
    
public:
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
    
};

// 测试函数
int main() {
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
    auto zddRoot = converter.convertMatrixToZDD(matrix);
    
    std::cout << "\n=== ZDD构建完成 ===\n";

    std::cout << "\nZDD表示的所有路径:\n";
    std::vector<int> path;
    converter.enumeratePaths(zddRoot, path);
    

    return 0;
}