#include "../include/DancingMatrix.h"
#include "../include/DXD.h"

static Logger logger("../test/test_log.txt");  // 全局日志

int main(int argc, char* argv[]) {

    string input_file = "../input_matrix.txt";
    if (argc > 1) {
        input_file = std::string(argv[1]);
    }

    bool useEtt = true; // 是否使用ETT
    if (argc > 2) {
        useEtt = std::string(argv[2]) == "ett";
    }

    bool debug = false;
    if (argc > 3) {
        debug = std::string(argv[3]) == "debug";
    }

    try
    {

        // DancingMatrix matrix(input_file, 3, false, true); 
        // auto block = matrix.InitBlock;

        // cout << "cover cols 4, 5 in initial block:" << endl;
        // matrix.coverInBlock(4, block);
        // matrix.coverInBlock(5, block);

        // auto components = matrix.getComponentsByETT(block.rows);
        // int i = 1;
        // for(auto& component : components){
        //     component.printBlock(i++);
        // }
        // cout << endl;

        // matrix.uncoverInBlock(5, block);
        // matrix.uncoverInBlock(4, block);
        // cout << "uncover cols 4, 5 :" << endl;

        // components = matrix.getComponentsByETT(block.rows);
        // i = 1;
        // for(auto& component : components){
        //     component.printBlock(i++);
        // }
        // cout << endl;
        if (useEtt) {
            cout << "使用ETT方法" << endl;
            DanceDNNF dxd(input_file, 3, logger, false, true, 12, debug);
            dxd.startMultiThreadDXD();
        } else {
            cout << "使用原方法" << endl;
            DanceDNNF dxd(input_file, 3, logger, true, false, 12, debug);
            dxd.startMultiThreadDXD();
        }

    }
    catch (const std::exception& e) {
        std::cerr << "An error occurred: " << e.what() << std::endl;
    }

    return 0;
}