#include "../include/DancingMatrix.h"

using namespace std;

int main() {

    string file_path = "../blocks_matrix.txt";

    try {
        DancingMatrix matrix(file_path, 3, false, true);

        auto block = matrix.InitBlock;

        cout << "start....." << endl;
        auto components = matrix.detector->GetBlocks(block.rows);
        cout << "检测初始分块的连通分量: " << endl;
        matrix.printBlocks(components);

        matrix.coverInBlock(6, block);
        matrix.coverInBlock(9, block);
        components = matrix.detector->GetBlocks(block.rows);
        cout << "检测覆盖第5行后的连通分量: " << endl;
        matrix.printBlocks(components);

        matrix.uncoverInBlock(9,block);
        matrix.uncoverInBlock(6, block);
        components = matrix.detector->GetBlocks(block.rows);
        cout << "检测取消覆盖第5行后的连通分量: " << endl;
        matrix.printBlocks(components);

    }catch (const exception &e) {
        cout << e.what() << endl;
    }
}