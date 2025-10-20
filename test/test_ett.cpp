#include "../include/DancingMatrix.h"
#include "../include/DXD.h"

static Logger logger("../test/test_log.txt");  // 全局日志

int main(){

    try
    {
        const string input_file = "../input_matrix.txt";

        // DancingMatrix matrix(input_file, 3, false, true); 
        // auto block = matrix.InitBlock;

        // matrix.coverInBlock(9, block);
        // matrix.coverInBlock(10, block);

        // auto components = matrix.getComponentsByETT(block.rows);
        // int i = 1;
        // for(auto& component : components){
        //     component.printBlock(i++);
        // }
        // cout << endl;

        DanceDNNF dxd(input_file, 3, logger, false, true);
        // dxd.startDXD();
        dxd.startMultiThreadDXD();

    }
    catch (const std::exception& e) {
        std::cerr << "An error occurred: " << e.what() << std::endl;
    }

    return 0;
}