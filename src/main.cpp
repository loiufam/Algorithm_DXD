#include "../include/DXZ.h"
#include "../include/DXD.h"

static Logger logger("../run_results.txt");  // 全局日志
// const string muti_thread_dxd_log_file = "../muti_thread_dxd_log.csv";
static const int DEFAULT_THREADS = 24;  // 线程数

// 算法类型枚举
enum class algorithm_type {
    dlx,
    dxz,
    dxd,
    mdxd
};

// 将字符串转换为枚举
algorithm_type parseAlgorithmType(const std::string& name) {
    if (name == "dlx") return algorithm_type::dlx;
    if (name == "dxz") return algorithm_type::dxz;
    if (name == "dxd") return algorithm_type::dxd;
    if (name == "mdxd") return algorithm_type::mdxd;
    throw std::invalid_argument("Unknown algorithm type: " + name);
}

// ./main <algorithm> <input> <read_mode> [pool_size]
int main(int argc, char *argv[]){
    
    if (argc < 4) {
            std::cout << "Usage: " << argv[0] << "<algorithm> <input> <read_mode> [pool_size]" << std::endl;
            return 1;
    }
    
    try
    {
        std::string algType = argv[1];
        std::string input_file = argv[2];
        int read_mode = std::stoi(argv[3]);
        int poolsize = argc > 4 ? std::stoi(argv[4]) : DEFAULT_THREADS;

        string filename = fs::path(input_file).stem().string();
        algorithm_type type = parseAlgorithmType(algType);
        switch (type) {
            case algorithm_type::dlx:
                {
                    logger.logLine("启用DLX算法求解: " + filename);
                    DanceZDD danceZDD(input_file, read_mode, logger);
                    auto res = danceZDD.startDLX();
                    logger.logLine("DLX算法求解结束: " + filename);
                    break;
                }
            case algorithm_type::dxz:
                {
                    logger.logLine("启用DXZ算法求解: " + filename);
                    DanceZDD danceZDD(input_file, read_mode, logger);
                    auto res = danceZDD.startDXZ();
                    logger.logLine("DXZ算法求解结束: " + filename);
                    break;
                }
            case algorithm_type::dxd: 
                {
                    logger.logLine("启用DXD算法求解: " + filename);
                    DanceDNNF danceDNNF(input_file, read_mode, logger, false, true);
                    auto res = danceDNNF.startDXD();
                    logger.logLine("DXD算法求解结束: " + filename);
                    break;
                }
            case algorithm_type::mdxd:
                {
                    logger.logLine("启用多线程DXD算法求解: " + filename);
                    // logger.logLine("线程池大小: " + std::to_string(poolsize));
                    DanceDNNF danceDNNF(input_file, read_mode, logger, false, true, poolsize);
                    auto res = danceDNNF.startMultiThreadDXD();
                    logger.logLine("多线程DXD算法求解结束: " + filename);
                    break;
                }
            default:
                std::cout << "Unknowed algorithm type" << std::endl;
                return 1;
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "错误：" << e.what() << '\n';
    }
    
    return 0;
}