#include "../include/DXZ.h"

void DanceZDD::DLX(std::vector<label_t>& solution)
{
    if( root->right == root ) {
        count++;
        // sols.push_back(solution);  
        return;
    } 

    ColunmHeader* choose = selectCol();
    if( choose->size <= 0 ){
        return;  
    }

    if( timer.timeBoundBroken() ){
        throw std::runtime_error("Time bound broken");
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
    if( root->right == root ) {
        count++;
        return;
    } 
    ColunmHeader* choose = (ColunmHeader*)root->right, *cur=choose;  
    while( cur != root )  
    {   //选择元素最少的列
        if( choose->size > cur->size )  
            choose = cur;  
        cur = (ColunmHeader*)cur->right;  
    }  
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

    if (x == y) {
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

shared_ptr<ZDDNode> DanceZDD::DXZ()
{
    if(root->right == root){
        return T_ZDD;
    }

    if (timer.timeBoundBroken()) {
        throw std::runtime_error("Time bound broken");
    }

    size_t columnState = getColumnState();
    // 查找缓存
    if (memo_cache.find(columnState) != memo_cache.end()) {
        return memo_cache[columnState]; 
    }

    ColunmHeader* choose = (ColunmHeader*)root->right;  
    ColunmHeader* cur = choose;
    while( cur != root )  
    {   //选择元素最少的列
        if( choose->size > cur->size )  
            choose = cur;  
        cur = (ColunmHeader*)cur->right;  
    }  

    if( choose->size <= 0 ){
        return F_ZDD;  
    } 

    shared_ptr<ZDDNode> x = F_ZDD;


    cover(choose->col);  //将选中列移出列表
    Node* curC = choose->up;  //curC用来遍历选中列的所有非零行(从下往上)
    while(curC != choose)  //相当于for r such that A[r, c]=1 do
    {
        //printColumnHeaders();
        // Node* noteR = curC;  
        Node* curR = curC->right;  
        while( curR != curC )  
        {  
            cover( curR->col );  
            curR = curR->right;  
        }

        shared_ptr<ZDDNode> y = DXZ();
        if(y->label != -2){
            x = unique(curC->row, x, y);
        }
       
        //printColumnHeaders();
        curR = curC->left;  
        while( curR != curC )  
        {  
            uncover( curR->col );  
            curR = curR->left;  
        }  

        curC = curC->up;
    }
    uncover(choose->col);  //回溯
    memo_cache[columnState] = x; 
    return x;
}

void DanceZDD::startDLX(){

    logger.logLine("DLX开始搜索...");
    // uint64_t sol = 0;
    try{
        vector<label_t> solution;
        auto start = std::chrono::high_resolution_clock::now();
        // X(sol);
        timer.markStartTime();
        DLX(solution);
        auto end = std::chrono::high_resolution_clock::now();
        searchTimeSeconds = std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count();
        logger.logLine("DLX搜索完成, 找到的解个数: " + std::to_string(count));

        // count = sol; // 更新计数
        logger.logLine("DLX搜索耗时(s): " + std::to_string(searchTimeSeconds));
        std::cout << std::endl;
    } catch (std::runtime_error& e) {
        logger.logLine("DLX搜索超时: " + std::string(e.what()));
    }
}


void DanceZDD::startDXZ(){

    logger.logLine("DXZ开始搜索...");
    try{
        auto startDXZ = std::chrono::high_resolution_clock::now();
        // shared_ptr<ZDDNode> root = DXZ()
        timer.markStartTime();
        auto root = DXZ();
        auto endDXZ = std::chrono::high_resolution_clock::now();
        searchTimeSeconds = std::chrono::duration_cast<std::chrono::duration<double>>(endDXZ - startDXZ).count();
        // std::cout << "搜索完成，找到 " << count << " 个解。" << std::endl;
        // std::cout << "DXZ搜索完成, 耗时: " << searchTimeSeconds << " 秒。" << std::endl;
        logger.logLine("DXZ搜索完成, 耗时: " + std::to_string(searchTimeSeconds) + " 秒。");
        // auto solutions = get_all_solutions(root);
        auto zdd_size = get_zdd_size(root);
            
        // std::cout << "解的数量: " << solutions.size() << std::endl;
        logger.logLine("解的数量: " + std::to_string(count));
        // std::cout << "ZDD节点数: " << zdd_size << std::endl;
        logger.logLine("ZDD节点数: " + std::to_string(zdd_size));
        std::cout << std::endl;
    } catch (std::runtime_error &e) {
        logger.logLine("DXZ搜索超时: " + std::string(e.what()));
    }

}

