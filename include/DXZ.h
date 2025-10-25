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

// ZDD节点结构
struct Simple_ZDDNode {
    int option;  // 选项编号
    int lo, hi;  // low和high分支
};

class DanceZDD : DancingMatrix{
    public:
        DanceZDD(int rows, int cols, int** matrix, Logger& l) 
            : DancingMatrix(rows, cols, matrix), logger(l) {

            timer.setTimeBound(1200);
           
            T_ZDD = make_shared<ZDDNode>(-1, nullptr, nullptr, true); // ZDD的T节点
            F_ZDD = make_shared<ZDDNode>(-2, nullptr, nullptr, true); // ZDD的F节点
            zddNodes.push_back({-1, 1, 1});
            zddNodes.push_back({-1, 0, 0});
            zddRoot = 0;
        }

        DanceZDD(const string& file_path, int from, Logger& l) 
        : DancingMatrix(file_path, from), logger(l) {

            timer.setTimeBound(1200);  
           
            T_ZDD = make_shared<ZDDNode>(-1, nullptr, nullptr, true); // ZDD的T节点
            F_ZDD = make_shared<ZDDNode>(-2, nullptr, nullptr, true); // ZDD的F节点
            zddNodes.push_back({-1, 1, 1});
            zddNodes.push_back({-1, 0, 0});
            zddRoot = 0;
        }
        
        ~DanceZDD() = default;

        shared_ptr<ZDDNode> T_ZDD; // ZDD的T节点
        shared_ptr<ZDDNode> F_ZDD; // ZDD的F节点
        vector<vector<label_t>> sols;
        CStopWatch timer;

        string cur_instance = ""; // 当前处理的实例名
        double searchTime = 0.0;
        uint64_t solutionCount = 0;
        bool timeout = false;

        // ZDD 相关方法
        size_t hashFunction(int r, ZDDNode* x, ZDDNode* y);
        shared_ptr<ZDDNode> unique(int r, shared_ptr<ZDDNode> x, shared_ptr<ZDDNode> y);
        shared_ptr<ZDDNode> DXZ();
        int makeZDDNode(int r, int lo, int hi);
        int search();

        void DLX(std::vector<label_t>& solution);
        void X(uint64_t& count);  // 搜索但不记录解
        
        shared_ptr<ExperimentResult> startDLX();
        shared_ptr<ExperimentResult> startDXZ();

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

        // 从ZDD计算解的数量
        uint64_t countSolutions(int node, unordered_map<int, uint64_t >& memo);

    private:
        Logger& logger;

        shared_ptr<ExperimentResult> cur_result = make_shared<ExperimentResult>();

        // ZDD
        std::unordered_map<size_t, shared_ptr<ZDDNode>> Z; // ZDD缓存
        std::unordered_map<std::string, shared_ptr<ZDDNode>> Z_; // ZDD缓存（线程安全）

        // 备忘录缓存：活跃列集合 -> ZDD节点
        unordered_map<size_t, shared_ptr<ZDDNode>> memo_cache;
        unordered_map<size_t, int> Cache;

        // ZDD相关
        vector<Simple_ZDDNode> zddNodes;
        int zddRoot;
        
        // Memo Cache
        unordered_map<Signature, int, SignatureHash> memoCache;


        struct Key {
            int r;
            int lo;
            int hi;
            bool operator==(Key const& o) const noexcept {
                return r==o.r && lo==o.lo && hi==o.hi;
            }
        };
    
        // splitmix64 for good mixing
        static uint64_t splitmix64(uint64_t x) {
            x += 0x9e3779b97f4a7c15ULL;
            x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
            x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
            x = x ^ (x >> 31);
            return x;
        }

        struct KeyHash {
            size_t operator()(Key const& k) const noexcept {
                // mix three ints into one 64-bit hash
                uint64_t a = static_cast<uint32_t>(k.r);
                uint64_t b = static_cast<uint32_t>(k.lo);
                uint64_t c = static_cast<uint32_t>(k.hi);

                uint64_t h = a;
                h = h * 0x9e3779b97f4a7c15ULL + (b << 1);
                h = splitmix64(h ^ (c + 0x9e3779b97f4a7c15ULL));
                return static_cast<size_t>(h);
            }
        };
    
        std::unordered_map<Key, int, KeyHash> node_table;

};

#endif