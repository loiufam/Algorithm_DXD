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

        DanceDNNF(const string& file_path, int from, Logger& l, 
                       const bool useIG = false, const bool useETT = false, int pool_size = 1, bool debug = false)
            : DancingMatrix(file_path, from, useIG, useETT), 
            logger(l), 
            max_threads(pool_size), 
            debug(debug), 
            num_of_DNNFNodes(0),
            max_depth(1) {

            omp_set_num_threads(pool_size); // 设置并行线程数
            std::cout << "设置并行线程数为: " << pool_size << std::endl;

            timer.setTimeBound(TIME_LIMIT_SECONDS + 30);
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

        // 多线程DLX
        DNNFResult parallelSearchMDLX(vector<Block>& blocks);
        DNNFResult MDLX(vector<int>& sols, Block& block);

        std::pair<DNNFResult, std::shared_ptr<DNNFNode>> DXD(Block& block, int depth);
        std::pair<DNNFResult, std::shared_ptr<DNNFNode>> serialSearch(vector<Block>& blocks, int depth);
        std::pair<DNNFResult, std::shared_ptr<DNNFNode>> parallelSearchUseOmp(vector<Block>& blocks, int depth);

        bool shouldDecompose() {
            // 单线程模式下原有的 MAX_TRIES 限制
            if (single_thread_mode) {
                std::lock_guard<std::mutex> lock(tried_numbers_mutex);
                if (++tried_numbers > MAX_TRIES) {
                    turnOffGraphSync();
                    return false;
                }
            }

            int cur = 0;
            {
                std::lock_guard<std::mutex> lock(thread_count_mutex);
                cur = current_concurrent_threads;
            }

            // 更新峰值
            int prev_peak = peak_concurrent_threads.load();
            while (cur > prev_peak && !peak_concurrent_threads.compare_exchange_weak(prev_peak, cur)) {}

            const int cap = getMaxThreads();
            const int threshold = static_cast<int>(cap * sync_off_ratio);
        
            if (cap > 0 && cur > threshold) {
                // 只有当前还开着时才计数 + 关闭
                if (isGraphSyncEnabled()) {
                    turnOffGraphSync();
                    sync_auto_off_count.fetch_add(1, std::memory_order_relaxed);
                }
                return false;
            }
                    
            return isGraphSyncEnabled();;
        }

        // 启动搜索函数
        void startDXD();
        void startMultiThreadDXD();
        void start_MDLX_Search();

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

        // 统计指标访问器
        int    getPeakConcurrentThreads()  const { return peak_concurrent_threads.load();  }
        size_t getMaxBlocksDetected()      const { return max_blocks_detected.load();      }
        size_t getSyncAutoOffCount()       const { return sync_auto_off_count.load();      }
        int  getMaxThreads() const {
            return max_threads > 0 ? max_threads
                                : static_cast<int>(std::thread::hardware_concurrency());
        }

        void addConcurrentThread(int count) {
            std::lock_guard<std::mutex> lock(thread_count_mutex);
            current_concurrent_threads += count;
        }

        void recordBlocksDetected(size_t n) {
            size_t prev = max_blocks_detected.load();
            while (n > prev &&
                !max_blocks_detected.compare_exchange_weak(prev, n)) {}
        }

    private:
        // ThreadPool& pool;
        Logger& logger;
        bool controlOUTPUT = false;

        // 并行阈值控制
        int max_threads = 0;               // 最大线程数（0 = 使用 hardware_concurrency）
        double sync_off_ratio = 0.75;      // 超过此比例即关闭图同步

        int current_concurrent_threads = 0;
        std::mutex thread_count_mutex;

        // 统计指标
        std::atomic<int>    peak_concurrent_threads{0};
        std::atomic<size_t> max_blocks_detected{0};
        std::atomic<size_t> sync_auto_off_count{0};  // 由阈值触发关闭同步的次数

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
        unordered_map<size_t, shared_ptr<DNNFNode>> node_table;

         // count cache (state → count)
        unordered_map<size_t, DNNFResult> countCache;

        ThreadPool& getThreadPool(int poolSize) {
            return ThreadPoolManager::get_instance(poolSize);
        }
};

class ConcurrentGuard {
public:
    ConcurrentGuard(
        DanceDNNF& dm, int delta) : dm_(dm), delta_(delta) {
        if (delta_ != 0) dm_.addConcurrentThread(delta_);
    }
    ~ConcurrentGuard() {
        if (delta_ != 0) dm_.addConcurrentThread(-delta_);
    }
    ConcurrentGuard(const ConcurrentGuard&) = delete;
    ConcurrentGuard& operator=(const ConcurrentGuard&) = delete;
private:
    DanceDNNF& dm_;
    int delta_;
};

#endif