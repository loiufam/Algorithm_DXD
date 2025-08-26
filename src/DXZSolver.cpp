#include "../include/DXZ.h"

void DanceZDD::DLX(std::vector<int>& solution)
{
    if( root->right == root ) {
        count++;
        solutions.push_back(solution);  // 找到解，存入解集中
        return;
    } 

    ColunmHeader* choose = selectCol();
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

        solution.push_back(curC->row + 1);  // 将当前行加入解集中
        // 递归搜索
        DLX(solution);
       
        solution.pop_back();  // 回溯，移除当前行
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

    std::size_t key = hashFunction(r, x.get(), y.get());
    if (Z.find(key) != Z.end()) {
        return Z[key];
    }

    Z[key] = make_shared<ZDDNode>(r, x, y); 
    // if (Z.find(key) == Z.end()) {  //如果没有找到解
    //     shared_ptr<ZDDNode> lo = x;
    //     shared_ptr<ZDDNode> hi = y;

    //     //先检查下x和y是否都为终端节点
    //     if(x->isTerminal && y->isTerminal){
    //        Z[key] = make_shared<ZDDNode>(r, lo, hi); 
    //        return Z[key];
    //     }

    //     //如果x，y都为分支节点,x指向的是已找到的解, 并且x，y都存在Z中
    //     if(!x->isTerminal && !y->isTerminal){
    //         //如果y存在缓存, 说明也存在Z中（DXZ）
    //         if (Z_a.find(getColumnState()) != Z_a.end()) {
    //             hi = Z_a[getColumnState()];    
    //         }

            
    //        Z[key] = make_shared<ZDDNode>(r, lo, hi); 
    //        return Z[key]; 
    //     }
        
    //     // 如果x或y是终端节点，尝试在另一个节点中找到匹配的终端节点
    //     if (x->isTerminal) {
    //         lo = F_ZDD;
    //     } else if (y->isTerminal) { // 此时说明r覆盖了当前矩阵的所有列
    //         hi = T_ZDD;
    //     }

    //     // 创建新的ZDDNode
    //     Z[key] = make_shared<ZDDNode>(r, lo, hi);
    // }
    return Z[key];
}

shared_ptr<ZDDNode> DanceZDD::DXZ()
{
    if(root->right == root){
        return T_ZDD;
    }

    std::string columnState = getColumnState();
    // 查找缓存
    if (Z_a.find(columnState) != Z_a.end()) {
        return Z_a[columnState]; 
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
        Node* noteR = curC;  
        Node* curR = curC->right;  
        while( curR != noteR )  
        {  
            cover( curR->col );  
            curR = curR->right;  
        }
        shared_ptr<ZDDNode> y = DXZ();
        if(y->label != -2){
            x = unique(curC->row, x, y);
        }
       
        //printColumnHeaders();
        curR = noteR->left;  
        while( curR != noteR )  
        {  
            uncover( curR->col );  
            curR = curR->left;  
        }  

        curC = curC->up;
    }
    uncover(choose->col);  //回溯
    Z_a[columnState] = x; // 结果存入缓存，实际上在Z中
    return x;
}

void DanceZDD::startDLX(){

    std::cout << "DLX开始搜索..." << std::endl;
    uint64_t sol = 0;
    auto start = std::chrono::high_resolution_clock::now();
    X(sol);
    auto end = std::chrono::high_resolution_clock::now();
    searchTimeSeconds = std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count();
    std::cout << "搜索完成，找到 " << sol << " 个解。" << std::endl;
    count = sol; // 更新计数
    std::cout << "DLX搜索耗时: " << searchTimeSeconds << " 秒。" << std::endl;
    std::cout<<std::endl;

}

void DanceZDD::startDXZ(){

    std::cout << "DXZ开始搜索..." << std::endl;
    auto startDXZ = std::chrono::high_resolution_clock::now();
    shared_ptr<ZDDNode> result = DXZ();
    auto endDXZ = std::chrono::high_resolution_clock::now();
    searchTimeSeconds = std::chrono::duration_cast<std::chrono::duration<double>>(endDXZ - startDXZ).count();
    std::cout << "搜索完成，找到 " << count << " 个解。" << std::endl;
    std::cout << "DXZ搜索完成, 耗时: " << searchTimeSeconds << " 秒。" << std::endl;
    std::cout << std::endl;

}

