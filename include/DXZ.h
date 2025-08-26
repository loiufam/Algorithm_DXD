#ifndef DXZ_H
#define DXZ_H

#include "DancingMatrix.h"

// ZDD相关
struct ZDDNode {
    int label;
    shared_ptr<ZDDNode> lo;
    shared_ptr<ZDDNode> hi;
    bool isTerminal;

    ZDDNode(int label, shared_ptr<ZDDNode> lo, shared_ptr<ZDDNode> hi, bool isTerminal = false)
        : label(label), lo(lo), hi(hi), isTerminal(isTerminal) {}
    
    // 拷贝构造函数
    ZDDNode(const ZDDNode& other) : label(other.label), isTerminal(other.isTerminal), lo(other.lo), hi(other.hi) {}

};

class DanceZDD : DancingMatrix{
    public:
        DanceZDD(int rows, int cols, int** matrix) : DancingMatrix(rows, cols, matrix) {
            root = getRoot();
            
            // 初始化ZDD的T和F节点
            T_ZDD = make_shared<ZDDNode>(-1, nullptr, nullptr, true); // ZDD的T节点
            F_ZDD = make_shared<ZDDNode>(-2, nullptr, nullptr, true); // ZDD的F节点
        }
        ~DanceZDD() {}

        // ZDD 相关方法
        size_t hashFunction(int r, ZDDNode* x, ZDDNode* y);
        shared_ptr<ZDDNode> unique(int r, shared_ptr<ZDDNode> x, shared_ptr<ZDDNode> y);
        shared_ptr<ZDDNode> DXZ();

        void DLX(std::vector<int>& solution);
        void X(uint64_t& count);  // 搜索但不记录解
        
        void startDLX();
        void startDXZ();


    private:
        ColunmHeader* root;


        // ZDD
        shared_ptr<ZDDNode> T_ZDD = make_shared<ZDDNode>(-1, nullptr, nullptr, true); // ZDD的T节点
        shared_ptr<ZDDNode> F_ZDD = make_shared<ZDDNode>(-2, nullptr, nullptr, true); // ZDD的F节点
        std::unordered_map<size_t, shared_ptr<ZDDNode>> Z; // ZDD缓存
        std::unordered_map<std::string, shared_ptr<ZDDNode>> Z_a; // ZDD缓存（线程安全）

};

#endif