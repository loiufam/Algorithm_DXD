#ifndef DXD_H
#define DXD_H

#include "../include/DancingMatrix.h"
#include "../include/DXDTime.h"

const int MIN_BLOCK_ROWS = 20;
const int MAX_BLOCK_ROWS = 200;
const int TIME_LIMIT_SECONDS = 1500; 
const int TIME_LIMIT_BUILDING_SECONDS = 1200;
const int MAX_DECOMPOSE_TIMES = 5;
using namespace std;

enum class NodeType { OR, Decision, Decomposed, Variable, Terminal };  // 节点类型 AND node 分为Decision和Decomposed两种
// 模式：ETT动态连通 vs BFS静态扫描
enum class DecomMode { Dynamic, Static };

struct DNNFNode {
    size_t id;
    inline static std::atomic<size_t> id_counter{0};

    NodeType type;
    int label{-3};   // -1 = T terminal, -2 = F terminal, -3 = Decomposed/OR
    uint64_t count{0};
    vector<std::shared_ptr<DNNFNode>> children;
    std::shared_ptr<DNNFNode> left;
    std::shared_ptr<DNNFNode> right;

    // Every constructor path assigns a fresh id.
    DNNFNode()
        : id(id_counter.fetch_add(1, std::memory_order_relaxed)) {}
 
    DNNFNode(NodeType t, int l)
        : id(id_counter.fetch_add(1, std::memory_order_relaxed)),
          type(t), label(l) {}
 
    DNNFNode(NodeType t, int l, uint64_t c)
        : id(id_counter.fetch_add(1, std::memory_order_relaxed)),
          type(t), label(l), count(c) {}
 
    // Decision node: Decision(var, lo, hi)
    DNNFNode(NodeType t, shared_ptr<DNNFNode> l, shared_ptr<DNNFNode> r)
        : id(id_counter.fetch_add(1, std::memory_order_relaxed)),
          type(t), left(l), right(r) {}
 
    // Simplified decision node (label + hi + count)
    DNNFNode(int l, shared_ptr<DNNFNode> r, uint64_t c)
        : id(id_counter.fetch_add(1, std::memory_order_relaxed)),
          type(NodeType::Decision), label(l), right(r), count(c) {}
 
    // Convenience: is this a terminal node?
    bool isTerminal() const { return type == NodeType::Terminal; }
};

class DanceDNNF : DancingMatrix { 

    public:
        DanceDNNF(int rows, int cols, int** matrix, Logger& l, bool verbose = false, int pool_size = 8) 
            : DancingMatrix(rows, cols, matrix, verbose), logger(l) {
            
 
            timer.setTimeBound(TIME_LIMIT_SECONDS);

            std::cout<< "初始化DanceDNNF完成." << endl;
        }

        DanceDNNF(const string& file_path, Logger& l, 
                       const bool useIG = false, const bool useETT = false, int pool_size = 1, bool debug = false)
            : DancingMatrix(file_path, useIG, useETT), 
            logger(l), 
            max_threads(pool_size), 
            debug(debug), 
            num_of_DNNFNodes(0),
            max_depth(1) {

            omp_set_num_threads(pool_size); // 设置并行线程数
            std::cout << "设置并行线程数为: " << pool_size << std::endl;

            if (pool_size > 2) isParallelSearch = true;
            timer.setTimeBound(TIME_LIMIT_SECONDS);
        }

        ~DanceDNNF() = default;

        CStopWatch timer;   // 计时器

        const int MAX_P_COUNT = 1; // 最大并行搜索次数   
        atomic<int> p_count{0}; // 记录当前并行的子进程数
        int detect_record = 0; // 记录第几次检测
        size_t MAX_B_COUNT;

        double searchTime = 0.0;
        string solutionCount; // 记录解的数量
        bool timeout = false; // 是否超时
        bool isParallelSearch = false; // 是否并行搜索
        double decomposeTime = 0.0;
        bool debug = false;

        // 构建Decision-ZDNNF
        shared_ptr<DNNFNode> buildDecisionNode(int r, shared_ptr<DNNFNode> lo, shared_ptr<DNNFNode> hi);

        shared_ptr<DNNFNode> buildDecomposableNode(vector<shared_ptr<DNNFNode>>& subDNNFs);

        std::pair<DNNFResult, std::shared_ptr<DNNFNode>> DXD(Block& block, int depth);
        std::pair<DNNFResult, std::shared_ptr<DNNFNode>> serialSearch(vector<Block>& blocks, int depth);
        std::pair<DNNFResult, std::shared_ptr<DNNFNode>> parallelSearchUseOmp(vector<Block>& blocks, int depth);

        static constexpr int SMALL_INSTANCE_ROWS = 100;
        static constexpr int SMALL_INSTANCE_COLS = 30;
        static constexpr int MAIN_NO_SPLIT_LIMIT = 5;
        static constexpr int TLS_NO_SPLIT_LIMIT = 3;
        static constexpr int SMALL_BLOCK_ROWS = 64;
        static constexpr int SMALL_BLOCK_COLS = 12;
        static constexpr int NESTED_DYNAMIC_DEPTH_LIMIT = 2;

        bool dynamic_ett_disabled = false;
        bool decomposition_disabled = false;
        int main_no_split_count = 0;

        bool isSmallInstanceForDynamicEtt() const {
            return ROWS <= SMALL_INSTANCE_ROWS || COLS <= SMALL_INSTANCE_COLS;
        }

        bool isLargeEnoughToStopAfterNoSplit(const Block& block) const {
            return block.rows.size() > SMALL_BLOCK_ROWS && block.cols.size() > SMALL_BLOCK_COLS;
        }

        bool isCurrentDynamicEttDisabled() const {
            return dynamic_ett_disabled ||
                   (isThreadLocal() && tlsState->dynamic_ett_disabled);
        }

        bool isCurrentDecompositionDisabled() const {
            return decomposition_disabled ||
                   (isThreadLocal() && tlsState->decomposition_disabled);
        }

        void disableDynamicEttForCurrentState() {
            if (isThreadLocal()) {
                tlsState->dynamic_ett_disabled = true;
            } else {
                dynamic_ett_disabled = true;
            }
        }

        bool shouldTryDecompose(const Block& block, int depth) {
            (void)depth;
            if (block.rows.size() <= 2 || block.cols.empty()) return false;

            if (dxd_mode) return true; // DXD 模式保持静态 BFS 分解尝试。

            if (isCurrentDecompositionDisabled()) return false;

            if (!isGraphSyncEnabled() && useETT && !isCurrentDynamicEttDisabled()) {
                return false;
            }

            return true;
        }

        bool shouldUseDynamicEtt(Block& block, int depth) {
            if (!useETT || dxd_mode) return false;
            if (isCurrentDynamicEttDisabled()) return false;

            if (isSmallInstanceForDynamicEtt()) {
                dynamic_ett_disabled = true;
                return false;
            }

            if (isThreadLocal()) {
                const bool smallBlock = block.rows.size() <= SMALL_BLOCK_ROWS ||
                                        block.cols.size() <= SMALL_BLOCK_COLS;
                const bool deepNested = tlsState->decompose_depth >= NESTED_DYNAMIC_DEPTH_LIMIT ||
                                        depth >= NESTED_DYNAMIC_DEPTH_LIMIT + 2;
                if (smallBlock || deepNested || tlsState->no_split_count >= TLS_NO_SPLIT_LIMIT) {
                    tlsState->dynamic_ett_disabled = true;
                    return false;
                }
            }

            return true;
        }

        bool shouldMaintainDynamicEtt() const {
            return useETT && !dxd_mode && !isCurrentDynamicEttDisabled();
        }

        void resetAdaptiveDecompositionState() {
            dynamic_ett_disabled = false;
            decomposition_disabled = false;
            main_no_split_count = 0;
        }

        void updateAdaptiveDecompositionState(const Block& block, size_t numBlocks, bool usedDynamicEtt) {
            if (dxd_mode) return;

            if (numBlocks > 1) {
                if (isThreadLocal()) {
                    tlsState->no_split_count = 0;
                } else {
                    main_no_split_count = 0;
                }
                return;
            }

            if (isThreadLocal()) {
                if (usedDynamicEtt && ++tlsState->no_split_count >= TLS_NO_SPLIT_LIMIT) {
                    tlsState->dynamic_ett_disabled = true;
                }
                return;
            }

            if (usedDynamicEtt && isLargeEnoughToStopAfterNoSplit(block) &&
                ++main_no_split_count >= MAIN_NO_SPLIT_LIMIT) {
                dynamic_ett_disabled = true;
                decomposition_disabled = true;
                turnOffGraphSync();
            }
        }

        // 启动搜索函数
        void startDXD();
        void startMultiThreadDXD();

        // ── MDLX（统一接口，mode决定分块策略）
        DNNFResult parallelSearchMDLX(vector<Block>& blocks, DecomMode mode);
 
        // MDLX_impl: 统一递归搜索，根据mode选择分块方式和图更新策略
        DNNFResult MDLX(vector<int>& sols, Block& block, DecomMode mode);
 
        // start_MDLX: 统一入口，通过mode参数切换ETT/BFS
        void start_MDLX(DecomMode mode);

        /**
         * Counts unique nodes in the Decision-DNNF rooted at rootDNNF.
         * Terminal nodes (T, F) are excluded from the count.
         */
        size_t countDNNFNodes() const;

        size_t countZDDSize() const;

        void printAllSolutions(std::ostream& out = std::cout, size_t max_sols = 0) const;

        bool queryRecord (size_t key) {
            std::shared_lock<std::shared_mutex> readLock(recordMutex);
            return records.find(key) != records.end();
        }

        void insertRecord (size_t key) {
            std::unique_lock<std::shared_mutex> writeLock(recordMutex);
            records.insert(key);
        }

        Block getBlock() {
            Block fullBlock(rowsSet, colsSet);
            return fullBlock;
        };

        std::shared_ptr<DNNFNode> getCache(const size_t& key){
            std::shared_lock<std::shared_mutex> readLock(cacheMutex);
            if(C.find(key) != C.end()){
                return C[key];
            }
            return nullptr;
        }

        void setCache(const size_t& key, std::shared_ptr<DNNFNode> node){
            std::unique_lock<std::shared_mutex> writeLock(cacheMutex);
            if(C.find(key) == C.end()){
                C[key] = node;
            }
        }

        void setCacheCount(const size_t& key, DNNFResult count){
            std::unique_lock<std::shared_mutex> writeLock(cacheMutex);
            countCache[key] = count;
        }

        int getRecordCount(){
            std::shared_lock<std::shared_mutex> readLock(recordMutex);
            return detect_record;
        }

        void addRecordCount(){
            std::unique_lock<std::shared_mutex> writeLock(recordMutex);
            detect_record++;
        }

        void runDXZ() {
            turnOffGraphSync();
            controlOUTPUT = true;
            dxz_mode = true;
            startDXD();
        }

        // Hash (row-variable, lo-id, hi-id).  Using node ids instead of labels
        // avoids false collisions: labels are reused row indices, while ids are
        // globally unique per node.
        inline size_t gen_key(int r, const DNNFNode* lo, const DNNFNode* hi) const {
            auto mix = [](size_t x) -> size_t {
                x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
                x ^= x >> 27; x *= 0x94d049bb133111ebULL;
                return x ^ (x >> 31);
            };
            size_t h = mix(static_cast<size_t>(r));
            h ^= mix(lo->id + 1ULL);
            h ^= mix(hi->id + 0x9e3779b9ULL);
            return h;
        }

        void addTriedNumbers(int count) {
            std::lock_guard<std::mutex> lock(tried_numbers_mutex);
            tried_numbers += count;
        }


    private:
        // ThreadPool& pool;
        Logger& logger;
        bool controlOUTPUT = false;

        // 并行阈值控制
        int max_threads = 0;               // 最大线程数（0 = 使用 hardware_concurrency）
        double sync_off_ratio = 0.75;      // 超过此比例即关闭图同步


        int tried_numbers = 0; // 已尝试的次数
        std::mutex tried_numbers_mutex;
        
        int num_of_zddNodes = 0; // 记录生成的ZDD节点数量
        // DNNF相关
        int num_of_DNNFNodes;
        int max_depth;
        vector<string> cache_input_order; // 记录缓存的输入顺序，便于输出
        std::shared_ptr<DNNFNode> rootDNNF;
        std::shared_ptr<DNNFNode> T = std::make_shared<DNNFNode>(NodeType::Terminal, -1, 1);
        std::shared_ptr<DNNFNode> F = std::make_shared<DNNFNode>(NodeType::Terminal, -2, 0);
        
        // DNNF缓存
        std::unordered_set<size_t> records; // 用于记录无法分解的矩阵状态

        mutable std::shared_mutex cacheMutex;
        mutable std::shared_mutex recordMutex; // 记录互斥锁
        unordered_map<size_t, shared_ptr<DNNFNode>> C;

        // DNNF Nodes table
        mutable std::shared_mutex tableMutex;
        unordered_map<size_t, std::weak_ptr<DNNFNode>> node_table;

         // count cache (state → count)
        unordered_map<size_t, DNNFResult> countCache;

        ThreadPool& getThreadPool(int poolSize) {
            return ThreadPoolManager::get_instance(poolSize);
        }
};


#endif