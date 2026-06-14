#include "../include/main.h"

static Logger logger("");  // 全局日志
// const string muti_thread_dxd_log_file = "../muti_thread_dxd_log.csv";
static const int DEFAULT_THREADS = 8;  // 线程数

enum class AlgType { dlx, dxz, dxd, ddxd, mdlx };

static AlgType toAlgType(const std::string& s) {
    if (s == "dlx")  return AlgType::dlx;
    if (s == "dxz")  return AlgType::dxz;
    if (s == "dxd")  return AlgType::dxd;
    if (s == "ddxd") return AlgType::ddxd;
    if (s == "mdlx") return AlgType::mdlx;
    throw std::invalid_argument("unknown algorithm: " + s);
}

static DecomMode toMDLXMode(const std::string& s) {
    if (s == "d") return DecomMode::Dynamic;
    if (s == "s") return DecomMode::Static;
    throw std::invalid_argument("unknown mdlx-mode: '" + s +
        "'. Valid: d | s");
}

// ./main -a ddxd -i ../data/exact_cover_benchmark/instance1.txt -t 4 --debug
int main(int argc, char *argv[]){

    ArgParser args;
    if (!args.parse(argc, argv)) return 1;

    const std::string filename  = fs::path(args.input).stem().string();
    const AlgType alg = toAlgType(args.alg);

    try
    {
        switch (alg) {
            case AlgType::dlx:
                {
                    logger.logLine("启用DLX算法求解: " + filename);
                    DanceZDD danceZDD(args.input, logger);
                    danceZDD.startDLX();
                    logger.logLine("DLX算法求解结束: " + filename);
                    break;
                }
            case AlgType::dxz:
                {
                    logger.logLine("启用DXZ算法求解: " + filename);
                    DanceDNNF danceDNNF(args.input, logger);
                    danceDNNF.runDXZ();
                    logger.logLine("DXZ算法求解结束: " + filename);
                    break;
                }
            case AlgType::dxd:
                {
                    int threads = args.threads > 0 ? args.threads : 1;
                    logger.logLine("启用DXD算法求解: " + filename);
                    DanceDNNF danceDNNF(args.input, logger, true, false, threads, args.debug);
                    danceDNNF.startDXD();
                    logger.logLine("DXD算法求解结束: " + filename);
                    break;
                }
            case AlgType::ddxd:
                {
                    logger.logLine("启用DynDXD算法求解: " + filename);
                    // DanceDNNF danceDNNF(input_file, logger, true, false, num_threads, debug);
                    DanceDNNF danceDNNF(args.input, logger, false, true, args.threads, args.debug); // 默认ett
                    danceDNNF.startMultiThreadDXD();
                    logger.logLine("DynDXD算法求解结束: " + filename);
                    break;
                }
            case AlgType::mdlx:
                {
                    DecomMode mode = toMDLXMode(args.dep_mode);

                    bool needETT = (mode == DecomMode::Dynamic);
                    logger.logLine("启用MDLX算法求解 [mode=" + args.dep_mode + "]: " + filename);
                    DanceDNNF danceDNNF(args.input, logger,
                                    /*useIG=*/!needETT,
                                    /*useETT=*/needETT,
                                    args.threads, args.debug);
                    danceDNNF.start_MDLX(mode);
                    logger.logLine("多线程DLX算法求解结束: " + filename);

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