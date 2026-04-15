#include "../include/main.h"

static Logger logger("../run_results.txt");  // 全局日志
// const string muti_thread_dxd_log_file = "../muti_thread_dxd_log.csv";
static const int DEFAULT_THREADS = 8;  // 线程数

static int inferReadMode(const std::string& input_file) {
    std::filesystem::path p(input_file);
    std::string parent = p.parent_path().filename().string();
    if (parent == "exact_cover_benchmark") return 1;
    if (parent == "run_set")               return 3;
    return 3;   // default
}

enum class AlgType { dlx, dxz, dxd, mdxd, mdlx };
 
static AlgType toAlgType(const std::string& s) {
    if (s == "dlx")  return AlgType::dlx;
    if (s == "dxz")  return AlgType::dxz;
    if (s == "dxd")  return AlgType::dxd;
    if (s == "mdxd") return AlgType::mdxd;
    if (s == "mdlx") return AlgType::mdlx;
    throw std::invalid_argument("unknown algorithm: " + s);
}

// ./main -a mdxd -i ../data/exact_cover_benchmark/instance1.txt -t 4 --debug
int main(int argc, char *argv[]){
    
    ArgParser args;
    if (!args.parse(argc, argv)) return 1;
    
    const int read_mode = inferReadMode(args.input);
    const std::string filename  = fs::path(args.input).stem().string();
    const AlgType alg = toAlgType(args.alg);

    try
    {
        switch (alg) {
            case AlgType::dlx:
                {
                    logger.logLine("启用DLX算法求解: " + filename);
                    DanceZDD danceZDD(args.input, read_mode, logger);
                    danceZDD.startDLX();
                    logger.logLine("DLX算法求解结束: " + filename);
                    break;
                }
            case AlgType::dxz:
                {
                    logger.logLine("启用DXZ算法求解: " + filename);
                    // DanceZDD danceZDD(input_file, read_mode, logger);
                    // danceZDD.startDXZ();
                    DanceDNNF danceDNNF(args.input, read_mode, logger);
                    danceDNNF.runDXZ();
                    logger.logLine("DXZ算法求解结束: " + filename);
                    break;
                }
            case AlgType::dxd: 
                {
                    logger.logLine("启用DXD算法求解: " + filename);
                    DanceDNNF danceDNNF(args.input, read_mode, logger, true, false, 1, args.debug);
                    danceDNNF.startDXD();
                    logger.logLine("DXD算法求解结束: " + filename);
                    break;
                }
            case AlgType::mdxd:
                {
                    logger.logLine("启用多线程DXD算法求解: " + filename);
                    // DanceDNNF danceDNNF(input_file, read_mode, logger, true, false, num_threads, debug);
                    DanceDNNF danceDNNF(args.input, read_mode, logger, false, true, args.threads, args.debug); // 默认ett
                    danceDNNF.startMultiThreadDXD();
                    logger.logLine("多线程DXD算法求解结束: " + filename);
                    break;
                }
            case AlgType::mdlx:
                {
                    logger.logLine("启用多线程DLX算法求解: " + filename);
                    DanceDNNF danceDNNF(args.input, read_mode, logger, false, true, args.threads, args.debug);
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