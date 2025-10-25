#include "../include/DXZ.h"

void DanceZDD::DLX(std::vector<label_t>& solution)
{

    if (timer.timeBoundBroken()) {
        throw std::runtime_error("Time OUT");
    }

    if( isSolved() ) {
        count++;
        // sols.push_back(solution);  
        return;
    } 

    ColumnHeader* choose = selectCol();
    if( choose->size <= 0 ){
        return;  
    }


    cover( choose->col );
    Node* curC = choose->down;  
    while( curC != choose )  // curC遍历选中列非零行
    {  
        
        // Node* noteR = curC;  
        Node* curR = curC->right;  
        while( curR != curC )  
        {  
            cover( curR->col );  
            curR = curR->right;  
        }  

        solution.push_back(curC->row + 1);  // 将当前行加入解集中
        // 递归搜索
        DLX(solution);
       
        solution.pop_back();  // 回溯，移除当前行
        curR = curC->left;  
        while( curR != curC )  
        {  
            uncover( curR->col );  
            curR = curR->left;  
        }  
        curC = curC->down;  
    }  
    uncover( choose->col );  
    return; 
}

void DanceZDD::X(uint64_t& count)  
{  
    if( isSolved() ) {
        count++;
        return;
    } 
    ColumnHeader* choose = selectCol();

    if( choose->size <= 0 ){
        return;  
    } 
          
    cover( choose->col );  
    Node* curC = choose->down;  
    while( curC != choose )  // curC遍历选中列非零行
    {  

        Node* noteR = curC;  
        Node* curR = curC->right;  
        while( curR != noteR )  
        {  
            cover( curR->col );  
            curR = curR->right;  
        }  

        X(count);  // 递归搜索
        
        curR = noteR->left;  
        while( curR != noteR )  
        {  
            uncover( curR->col );  
            curR = curR->left;  
        }  
        curC = curC->down;  
    }  
    uncover( choose->col );  
    return;  
}

size_t DanceZDD::hashFunction(int r, ZDDNode* x, ZDDNode* y)
{
    return std::hash<int>()(r) ^ (std::hash<int>()(x->label) << 1) ^ (std::hash<int>()(y->label) << 2);
}

shared_ptr<ZDDNode> DanceZDD::unique(int r, shared_ptr<ZDDNode> x, shared_ptr<ZDDNode> y)
{

    if (x == y || y == F_ZDD) {
        return x;
    }

    // NodeKey key{r, x, y};
    // auto it = node_table.find(key);
    // if (it != node_table.end()) {
    //     return it->second;
    // }

    // auto node = std::make_shared<ZDDNode>(r, x, y);
    // node_table[key] = node;
    // return node;

    std::size_t key = hashFunction(r, x.get(), y.get());
    if (Z.find(key) != Z.end()) {
        return Z[key];
    }

    auto zdd_node = make_shared<ZDDNode>(r, x, y);
    Z[key] = zdd_node;
    return zdd_node;
}

int DanceZDD::makeZDDNode(int r, int lo, int hi) {
        
    // 如果hi分支指向false，则整个节点等价于lo
    if (hi == 0) return lo;
    
    // 查找是否已存在相同的节点（节点共享）
    for (size_t i = 2; i < zddNodes.size(); i++) {
        if (zddNodes[i].option == r && 
            zddNodes[i].lo == lo && 
            zddNodes[i].hi == hi) {
            return i;
        }
    }
    
    // 创建新节点
    Simple_ZDDNode node;
    node.option = r;
    node.lo = lo;
    node.hi = hi;
    zddNodes.push_back(node);
    
    return zddNodes.size() - 1;
}

shared_ptr<ZDDNode> DanceZDD::DXZ()
{
    if(isSolved()){
        return T_ZDD;
    }

    // if (timer.timeBoundBroken()) {
    //     throw std::runtime_error("Time bound broken");
    // }

    size_t columnState = getColumnState();
    if (memo_cache.find(columnState) != memo_cache.end()) {
        return memo_cache[columnState]; 
    }


    ColumnHeader* choose = selectCol();

    if( choose->size <= 0 ){
        memo_cache[columnState] = F_ZDD;
        return F_ZDD;  
    } 

    shared_ptr<ZDDNode> x = F_ZDD;

    cover(choose->col);  //将选中列移出列表
    Node* curC = choose->down;  //curC用来遍历选中列的所有非零行(从下往上)
    while(curC != choose)  //相当于for r such that A[r, c]=1 do
    {
        // Node* noteR = curC;  
        Node* curR = curC->right;  
        while( curR != curC )  
        {  
            cover( curR->col );  
            curR = curR->right;  
        }

        shared_ptr<ZDDNode> y = DXZ();
        x = unique(curC->row, x, y);

        curR = curC->left;  
        while( curR != curC )  
        {  
            uncover( curR->col );  
            curR = curR->left;  
        }  


        curC = curC->down;
    }
    uncover(choose->col);  //回溯
    // 存入缓存
    memo_cache[columnState] = x; 

    return x;
}

int DanceZDD::search() {
    
    if (timer.timeBoundBroken()) {
        throw std::runtime_error("Time OUT");
    }

    if(isSolved()){
        return 1;
    } 


    size_t columnState = getColumnState();
    if (Cache.find(columnState) != Cache.end()) {
        return Cache[columnState]; 
    }

    // // 生成当前状态的签名
    // Signature sig = getColumnSignature();
    
    // // 检查Memo Cache
    // auto it = memoCache.find(sig);
    // if (it != memoCache.end()) {
    //     return it->second;
    // }

    ColumnHeader* choose = selectCol();

    if( choose->size <= 0 ){
        // memoCache[sig] = 0;
        Cache[columnState] = 0;
        return 0;  
    } 

    int resZDD = 0;

    cover(choose->col);  
    Node* curC = choose->down; 
    while(curC != choose)  
    {
        // Node* noteR = curC;  
        Node* curR = curC->right;  
        while( curR != curC )  
        {  
            cover( curR->col );  
            curR = curR->right;  
        }

        int highZDD = search();

        curR = curC->left;  
        while( curR != curC )  
        {  
            uncover( curR->col );  
            curR = curR->left;  
        }  

        resZDD = makeZDDNode(curC->row, highZDD, resZDD);

        curC = curC->down;
    }
    uncover(choose->col);  //回溯
    
    // 存入缓存
    // memoCache[sig] = resZDD;
    Cache[columnState] = resZDD;

    return resZDD;

}

// 从ZDD计算解的数量
uint64_t DanceZDD::countSolutions(int node, unordered_map<int, uint64_t >& memo) {
    if (node == 0) return 0;
    if (node == 1) return 1;
    
    auto it = memo.find(node);
    if (it != memo.end()) return it->second;

    uint64_t  lo = countSolutions(zddNodes[node].lo, memo);
    uint64_t  hi = countSolutions(zddNodes[node].hi, memo);
    
    uint64_t  result = lo + hi;
    memo[node] = result;
    return result;
}

shared_ptr<ExperimentResult> DanceZDD::startDLX(){

    logger.logLine("DLX开始搜索...");
    cur_result->instance_name = cur_instance;
    try{
        vector<label_t> solution;
        timer.markStartTime();
        auto start = std::chrono::high_resolution_clock::now();
        // X(sol);
        DLX(solution);
        auto end = std::chrono::high_resolution_clock::now();
        timer.reset();
        searchTimeSeconds = std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count();
        logger.logLine("Time: " + std::to_string(searchTimeSeconds) + " s");
        cur_result->runtime = std::to_string(searchTimeSeconds);
        searchTime = searchTimeSeconds;

        logger.logLine("Solutions: " + std::to_string(count));
        cur_result->solution_count = count;
        solutionCount = count;

        return cur_result;
    } catch (std::runtime_error& e) {
        timeout = true;
        cur_result->runtime = "timeout";
        logger.logLine("DLX搜索超时: " + std::string(e.what()));
        return cur_result;
    }
}


shared_ptr<ExperimentResult> DanceZDD::startDXZ(){

    logger.logLine("DXZ开始搜索...");
    cur_result->instance_name = cur_instance;
    try{
        timer.markStartTime();
        auto startDXZ = std::chrono::high_resolution_clock::now();
        // auto root = DXZ();
        auto root = search();
        auto endDXZ = std::chrono::high_resolution_clock::now();
        timer.reset();
        searchTimeSeconds = std::chrono::duration_cast<std::chrono::duration<double>>(endDXZ - startDXZ).count();
        cur_result->runtime = std::to_string(searchTimeSeconds);
        searchTime = searchTimeSeconds;
        logger.logLine("Time: " + std::to_string(searchTimeSeconds) + " s");

        unordered_map<int, uint64_t> countMemo;
        count = countSolutions(root, countMemo);
        cur_result->solution_count = count;
        solutionCount = count;
        logger.logLine("Solutions: " + std::to_string(count));

        return cur_result;
    } catch (std::runtime_error &e) {
        timeout = true;
        cur_result->runtime = "timeout";
        logger.logLine("DXZ搜索超时: " + std::string(e.what()));
        return cur_result;
    }

}

