#ifndef DXZ_H
#define DXZ_H

#include "DancingMatrix.h"
#include "DXDTime.h"

using label_t = uint32_t;

// ZDD相关
struct ZDDNode {
    int label;
    shared_ptr<ZDDNode> lo;
    shared_ptr<ZDDNode> hi;
    bool isTerminal;

    ZDDNode(int label, shared_ptr<ZDDNode> lo, shared_ptr<ZDDNode> hi, bool isTerminal = false)
        : label(label), lo(lo), hi(hi), isTerminal(isTerminal) {}
    
    
    bool operator==(const ZDDNode& other) const {
        return label == other.label && lo == other.lo && hi == other.hi;
    }
    
    // 拷贝构造函数
    ZDDNode(const ZDDNode& other) : label(other.label), isTerminal(other.isTerminal), lo(other.lo), hi(other.hi) {}

};

class DanceZDD : DancingMatrix{
    public:
        DanceZDD(int rows, int cols, int** matrix, Logger& l) 
            : DancingMatrix(rows, cols, matrix), logger(l) {
            root = getRoot();
            timer.setTimeBound(2400);
           
            T_ZDD = make_shared<ZDDNode>(-1, nullptr, nullptr, true); // ZDD的T节点
            F_ZDD = make_shared<ZDDNode>(-2, nullptr, nullptr, true); // ZDD的F节点
        }

        DanceZDD(const string& file_path, int from, Logger& l) 
        : DancingMatrix(file_path, from), logger(l) {
            root = getRoot();
            timer.setTimeBound(2400);  // 20 minutes
           
            T_ZDD = make_shared<ZDDNode>(-1, nullptr, nullptr, true); // ZDD的T节点
            F_ZDD = make_shared<ZDDNode>(-2, nullptr, nullptr, true); // ZDD的F节点
        }
        
        ~DanceZDD() {
            Z.clear();
            Z_.clear();
            node_table.clear();
            memo_cache.clear();
        }

        shared_ptr<ZDDNode> T_ZDD; // ZDD的T节点
        shared_ptr<ZDDNode> F_ZDD; // ZDD的F节点
        vector<vector<label_t>> sols;
        CStopWatch timer;

        // ZDD 相关方法
        size_t hashFunction(int r, ZDDNode* x, ZDDNode* y);
        shared_ptr<ZDDNode> unique(int r, shared_ptr<ZDDNode> x, shared_ptr<ZDDNode> y);
        shared_ptr<ZDDNode> DXZ();

        void DLX(std::vector<label_t>& solution);
        void X(uint64_t& count);  // 搜索但不记录解
        
        void startDLX();
        void startDXZ();

        // 枚举所有解（深度优先遍历ZDD）
        void enumerate_solutions(std::shared_ptr<ZDDNode> node, 
                            std::vector<int>& current_solution,
                            std::vector<std::vector<int>>& all_solutions) const {
            if (!node || node->label == -2) {
                return;
            }
            
            if (node->label == -1) {
                all_solutions.push_back(current_solution);
                return;
            }
            
            // 不选择当前行（沿lo边）
            if (node->lo) {
                enumerate_solutions(node->lo, current_solution, all_solutions);
            }
            
            // 选择当前行（沿hi边）
            if (node->hi) {
                current_solution.push_back(node->label);
                enumerate_solutions(node->hi, current_solution, all_solutions);
                current_solution.pop_back();
            }
        }

        // 获取所有解
        std::vector<std::vector<int>> get_all_solutions(std::shared_ptr<ZDDNode> root) const {
            std::vector<std::vector<int>> solutions;
            std::vector<int> current_solution;
            enumerate_solutions(root, current_solution, solutions);
            return solutions;
        }
    
        // 统计ZDD节点数量
        size_t count_zdd_nodes(std::shared_ptr<ZDDNode> node, 
                            std::unordered_set<ZDDNode*>& visited) const {
            if (!node || visited.count(node.get())) {
                return 0;
            }
            
            visited.insert(node.get());
            size_t count = 1;
            
            if (node->lo) {
                count += count_zdd_nodes(node->lo, visited);
            }
            if (node->hi) {
                count += count_zdd_nodes(node->hi, visited);
            }
            
            return count;
        }
    
        size_t get_zdd_size(std::shared_ptr<ZDDNode> root) const {
            std::unordered_set<ZDDNode*> visited;
            return count_zdd_nodes(root, visited);
        }


    private:
        Logger& logger;
        ColunmHeader* root;


        // ZDD
        std::unordered_map<size_t, shared_ptr<ZDDNode>> Z; // ZDD缓存
        std::unordered_map<std::string, shared_ptr<ZDDNode>> Z_; // ZDD缓存（线程安全）


        // 备忘录缓存：活跃列集合 -> ZDD节点
        std::unordered_map<size_t, std::shared_ptr<ZDDNode>> memo_cache;


        struct NodeKey {
            int label;
            std::shared_ptr<ZDDNode> lo;
            std::shared_ptr<ZDDNode> hi;
            
            bool operator==(const NodeKey& other) const {
                return label == other.label && lo == other.lo && hi == other.hi;
            }
        };
    
        struct NodeKeyHash {
            std::size_t operator()(const NodeKey& k) const {
                return std::hash<int>{}(k.label) ^
                    (std::hash<void*>{}(k.lo.get()) << 1) ^
                    (std::hash<void*>{}(k.hi.get()) << 2);
            }
        };
    
        std::unordered_map<NodeKey, std::shared_ptr<ZDDNode>, NodeKeyHash> node_table;

};

#endif