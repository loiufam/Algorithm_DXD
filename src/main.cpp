#include "../include/DancingMatrix.h"
#include "../include/DXZ.h"
#include "../include/DXD.h"

static Logger logger("../alg_log.txt");  // 全局日志

// 算法类型枚举
enum class algorithm_type {
    dlx,
    dxz,
    d3x,
    dxd,
    dxd_p
};

// 将字符串转换为枚举
algorithm_type parseAlgorithmType(const std::string& name) {
    if (name == "dlx") return algorithm_type::dlx;
    if (name == "dxz") return algorithm_type::dxz;
    if (name == "d3x") return algorithm_type::d3x;
    if (name == "dxd") return algorithm_type::dxd;
    if (name == "dxd_p") return algorithm_type::dxd_p;
    throw std::invalid_argument("Unknown algorithm type: " + name);
}

// ./main <input> <algorithm> <read_mode> [timeout_seconds]
int main(int argc, char *argv[]){
    
    if (argc < 4) {
            std::cout << "Usage: " << argv[0] << " <input> <algorithm> <read_mode> [timeout_seconds]" << std::endl;
            return 1;
    }
    
    try
    {
        std::string input_file = argv[1];
        std::string algType = argv[2];
        int read_mode = std::stoi(argv[3]);
        int timeout_seconds = argc >= 5 ? std::stoi(argv[4]) : 1200; // 默认超时时间为 1200 秒
        if (timeout_seconds <= 0) {
            cerr << "Invalid timeout seconds: " << timeout_seconds << std::endl;
            return 1;
        }

        string filename = fs::path(input_file).stem().string();
        algorithm_type type = parseAlgorithmType(algType);
        switch (type) {
            case algorithm_type::dlx:
                {
                    logger.logLine("启用DLX算法求解: " + filename);
                    DanceZDD danceZDD(input_file, read_mode, logger, timeout_seconds);
                    auto res = danceZDD.startDLX();
                    logger.logLine("DLX算法求解结束: " + filename);
                    break;
                }
            case algorithm_type::dxz:
                {
                    logger.logLine("启用DXZ算法求解: " + filename);
                    DanceZDD danceZDD(input_file, read_mode, logger, timeout_seconds);
                    auto res = danceZDD.startDXZ();
                    logger.logLine("DXZ算法求解结束: " + filename);
                    break;
                }
            case algorithm_type::dxd: 
                {
                    logger.logLine("启用DXD算法求解: " + filename);
                    DanceDNNF danceDNNF(input_file, read_mode, logger, true, timeout_seconds);
                    auto res = danceDNNF.startDXD();
                    logger.logLine("DXD算法求解结束: " + filename);
                    break;
                }
            case algorithm_type::dxd_p:
                {
                    logger.logLine("启用多线程DXD算法求解: " + filename);
                    ExactCoverSolver ecSolver(input_file, read_mode, logger, 16);
                    auto res = ecSolver.searchEC();
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