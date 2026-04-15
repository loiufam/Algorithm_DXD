#include "../include/DXZ.h"
#include "../include/DXD.h"

static Logger logger("../run_results.txt");  // 全局日志
// const string muti_thread_dxd_log_file = "../muti_thread_dxd_log.csv";
static const int DEFAULT_THREADS = 8;  // 线程数

// 算法类型枚举
enum class algorithm_type {
    dlx,
    dxz,
    dxd,
    mdxd,
    mdlx
};

// 将字符串转换为枚举
algorithm_type parseAlgorithmType(const std::string& name) {
    if (name == "dlx") return algorithm_type::dlx;
    if (name == "dxz") return algorithm_type::dxz;
    if (name == "dxd") return algorithm_type::dxd;
    if (name == "mdxd") return algorithm_type::mdxd;
    if (name == "mdlx") return algorithm_type::mdlx;
    throw std::invalid_argument("Unknown algorithm type: " + name);
}

// ./main <algorithm> <input> <read_mode> [pool_size]
int main(int argc, char *argv[]){
    
    if (argc < 3) {
            std::cout << "Usage: " << argv[0] << "<algorithm> <input>" << std::endl;
            return 1;
    }
    
    try
    {
        std::string algType = argv[1];
        std::string input_file = argv[2];

        int read_mode = 3;
        std::filesystem::path p(input_file);
        std::string parent = p.parent_path().filename().string();
        if (parent == "exact_cover_benchmark") {
            read_mode = 1;
        } else if (parent == "run_set") {
            read_mode = 3;
        }

        int num_threads = (argc > 3) ? std::stoi(argv[3]) : DEFAULT_THREADS; // 默认线程数
        if (num_threads > 8) {
            std::cout << "线程数过大, 建议不超过8" << std::endl;
            num_threads = DEFAULT_THREADS;
        }

        bool debug = false;

        string filename = fs::path(input_file).stem().string();
        algorithm_type type = parseAlgorithmType(algType);
        switch (type) {
            case algorithm_type::dlx:
                {
                    logger.logLine("启用DLX算法求解: " + filename);
                    DanceZDD danceZDD(input_file, read_mode, logger);
                    danceZDD.startDLX();
                    logger.logLine("DLX算法求解结束: " + filename);
                    break;
                }
            case algorithm_type::dxz:
                {
                    logger.logLine("启用DXZ算法求解: " + filename);
                    // DanceZDD danceZDD(input_file, read_mode, logger);
                    // danceZDD.startDXZ();
                    DanceDNNF danceDNNF(input_file, read_mode, logger);
                    danceDNNF.runDXZ();
                    logger.logLine("DXZ算法求解结束: " + filename);
                    break;
                }
            case algorithm_type::dxd: 
                {
                    logger.logLine("启用DXD算法求解: " + filename);
                    DanceDNNF danceDNNF(input_file, read_mode, logger, true, false, 1, debug);
                    danceDNNF.startDXD();
                    logger.logLine("DXD算法求解结束: " + filename);
                    break;
                }
            case algorithm_type::mdxd:
                {
                    logger.logLine("启用多线程DXD算法求解: " + filename);
                    // DanceDNNF danceDNNF(input_file, read_mode, logger, true, false, num_threads, debug);
                    DanceDNNF danceDNNF(input_file, read_mode, logger, false, true, num_threads, debug); // 默认ett
                    danceDNNF.startMultiThreadDXD();
                    logger.logLine("多线程DXD算法求解结束: " + filename);
                    break;
                }
            case algorithm_type::mdlx:
                {
                    logger.logLine("启用多线程DLX算法求解: " + filename);
                    DanceDNNF danceDNNF(input_file, read_mode, logger, false, true, num_threads, debug);
                    danceDNNF.start_MDLX_Search();
                    logger.logLine("多线程DLX算法求解结束: " + filename);
                    // else {
                    //     logger.logLine("启用多线程DLX算法求解: " + filename);
                    //     DanceDNNF danceDNNF(input_file, read_mode, logger, true, false, DEFAULT_THREADS, debug);
                    //     danceDNNF.start_MDLX_Search();
                    //     logger.logLine("多线程DLX算法求解结束: " + filename);
                    // }
                    break;
                }
            default:
                std::cout << "Unknowed algorithm type" << std::endl;
                return 1;
        }
    }
    catch (const std::exception& e) {
        std::cerr << "错误：" << e.what() << '\n';
    }
    
    return 0;
}