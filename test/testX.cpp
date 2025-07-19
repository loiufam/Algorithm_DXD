#include "../include/DancingMatrix.h"

int** TranFileToMatrix(const std::string& filename, int& r, int& c) { 
    std::ifstream file(filename);
    if(!file.is_open()) {
        throw std::runtime_error("无法打开文件");
    }

    std::string line;
    std::getline(file, line);  // 读取第一行
    std::istringstream iss(line);
    int n, m;
    iss >> n >> m;  // 假设第一行格式为 "n m"
    c = n;
    r = m;

    // 动态分配二维数组
    int** matrix = new int*[r];
    for(int i = 0; i < r; ++i) {
        matrix[i] = new int[c];
        for(int j = 0; j < c; ++j) {
            matrix[i][j] = 0;
        }
    }

    int row = 0;
    while (std::getline(file, line))
    {
        std::istringstream rowStream(line);
        int col, count;

        // 读取该行“1”的个数
        rowStream >> count;

        for (int i = 0; i < count; ++i) {
            rowStream >> col;  // 读取列索引
            if (col > 0 && col <= n) {  // 确保列索引在有效范围内
                matrix[row][col - 1] = 1;  // 由于数组索引从0开始，所以减1
            }
        }
        row++;
    }
    
    file.close();
    return matrix;
}

int main(){
    try
    {
        // file path
        const std::string filePath = "../data/Missouri.txt";
        const std::string folderPath2 = "../data/set_partitioning_benchmarks";
        const std::string filePath_d3x = "../data/dataset_d3x";
        // int r, c;
        // int** matrix = TranFileToMatrix(filePath, r, c);
        // std::cout << "rows: " << r << ", cols: " << c << std::endl;
        
        // DancingMatrix dm(r, c, matrix);

        // int count = 0;
        // std::cout << "开始搜索..." << std::endl;
        // dm.X(count);
       
        // std::cout << "搜索到的解个数: " << count << std::endl;
        for(const auto& entry : fs::directory_iterator(filePath_d3x))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".txt")
            {
                // 文件路径
                std::cout << "文件名: " << entry.path().filename().string() << std::endl;

                // 从文件中提取 n 和 m 的值
                int r, c; 
                // int** matrix = PreProccess::processFileToMatrix2(entry.path(), r, c);
                int** matrix = TranFileToMatrix(entry.path().string(), r, c);

                // 创建 DancingMatrix 对象
                DancingMatrix dmx(r, c, matrix);

                std::cout << "开始搜索..." << std::endl;
                dmx.startSearch();

                // 释放内存
                PreProccess::freeMatrix(matrix, r);
            }
        }
        std::cout << "set_partitioning_benchmarks 文件夹处理完毕" << std::endl;
    }
    catch(const std::exception& e)
    {
        std::cerr << e.what() << '\n';
    }
    
}