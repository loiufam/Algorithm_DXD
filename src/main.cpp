#include "../include/main.h"
#include "../include/HybridDXD.h"

static Logger logger("");  // 全局日志
// const string muti_thread_dxd_log_file = "../muti_thread_dxd_log.csv";
static const int DEFAULT_THREADS = 8;  // 线程数

static DecomMode toMDLXMode(const std::string& s) {
    if (s == "d") return DecomMode::Dynamic;
    if (s == "s") return DecomMode::Static;
    throw std::invalid_argument("unknown mdlx-mode: '" + s +
        "'. Valid: d | s");
}

static AlgorithmMode toAlgorithmMode(const std::string& algorithm,
                                     const std::string& mdlx_mode) {
    if (algorithm == "dlx") return AlgorithmMode::DLX;
    if (algorithm == "dxz") return AlgorithmMode::DXZ;
    if (algorithm == "dxd") return AlgorithmMode::DXD;
    if (algorithm == "ddxd") return AlgorithmMode::DYNDXD;
    if (algorithm == "hybrid") {
        return AlgorithmMode::HYBRID_DXD;
    }
    if (algorithm == "mdlx") {
        return toMDLXMode(mdlx_mode) == DecomMode::Dynamic
                   ? AlgorithmMode::MDLX_DYNAMIC
                   : AlgorithmMode::MDLX_STATIC;
    }
    throw std::invalid_argument("unknown algorithm: " + algorithm);
}

// ./main -a ddxd -i ../data/exact_cover_benchmark/instance1.txt -t 4 --debug
int main(int argc, char *argv[]){

    ArgParser args;
    if (!args.parse(argc, argv)) return 1;

    const std::string filename  = fs::path(args.input).stem().string();
    const AlgorithmMode algorithmMode =
        toAlgorithmMode(args.alg, args.dep_mode);

    try
    {
        switch (algorithmMode) {
            case AlgorithmMode::DLX:
                {
                    logger.logLine("启用DLX算法求解: " + filename);
                    DanceZDD danceZDD(args.input, logger);
                    danceZDD.startDLX();
                    logger.logLine("DLX算法求解结束: " + filename);
                    break;
                }
            case AlgorithmMode::DXZ:
                {
                    logger.logLine("启用DXZ算法求解: " + filename);
                    DanceDNNF danceDNNF(
                        args.input, logger, AlgorithmMode::DXZ, 1,
                        args.debug);
                    danceDNNF.runDXZ();
                    logger.logLine("DXZ算法求解结束: " + filename);
                    break;
                }
            case AlgorithmMode::DXD:
                {
                    int threads = args.threads > 0 ? args.threads : 1;
                    logger.logLine("Instance: " + filename);
                    logger.logLine("启用DXD算法求解: " + filename);
                    DanceDNNF danceDNNF(
                        args.input, logger, AlgorithmMode::DXD, threads,
                        args.debug);
                    if (args.enable_cc_stats) danceDNNF.enableCCStatistics();
                    if (args.enable_cc_time) danceDNNF.enableCCTiming();
                    danceDNNF.setSkipDNNFSize(args.skip_dnnf_size);
                    danceDNNF.setTimeLimit(args.time_limit);
                    danceDNNF.startDXD();
                    logger.logLine("DXD算法求解结束: " + filename);
                    break;
                }
            case AlgorithmMode::DYNDXD:
                {
                    logger.logLine("Instance: " + filename);
                    logger.logLine("启用DynDXD算法求解: " + filename);
                    DanceDNNF danceDNNF(
                        args.input, logger, AlgorithmMode::DYNDXD,
                        args.threads, args.debug);
                    if (args.quick_stats_updates > 0) {
                        danceDNNF.enableQuickStatistics(args.quick_stats_updates);
                    } else if (args.enable_cc_stats) {
                        danceDNNF.enableCCStatistics();
                    }
                    if (args.enable_cc_time) danceDNNF.enableCCTiming();
                    danceDNNF.setCCETTMaxCalls(args.cc_ett_max_calls);
                    danceDNNF.setCCBFSMaxCalls(args.cc_bfs_max_calls);
                    danceDNNF.setBFSAreaThreshold(args.bfs_area_threshold);
                    danceDNNF.setPureDynDXD(args.pure_dyndxd);
                    danceDNNF.setSkipDNNFSize(args.skip_dnnf_size);
                    danceDNNF.setTimeLimit(args.time_limit);
                    danceDNNF.startMultiThreadDXD();
                    if (args.quick_stats_updates > 0) {
                        logger.logLine("DynDXD快捷统计结束: " + filename);
                    } else {
                        logger.logLine("DynDXD算法求解结束: " + filename);
                    }
                    break;
                }
            case AlgorithmMode::HYBRID_DXD:
                {
                    logger.logLine("Instance: " + filename);
                    logger.logLine(
                        "启用HybridDXD算法求解: " + filename);
                    HybridDXD hybrid(
                        args.input, logger, args.threads, args.debug);
                    hybrid.setEttAreaDivisor(
                        args.hybrid_ett_area_divisor);
                    hybrid.setSkipDNNFSize(args.skip_dnnf_size);
                    hybrid.setTimeLimit(args.time_limit);
                    hybrid.startHybridDXD();
                    logger.logLine(
                        "HybridDXD算法求解结束: " + filename);
                    break;
                }
            case AlgorithmMode::MDLX_DYNAMIC:
            case AlgorithmMode::MDLX_STATIC:
                {
                    DecomMode mode = toMDLXMode(args.dep_mode);

                    logger.logLine("启用MDLX算法求解 [mode=" + args.dep_mode + "]: " + filename);
                    DanceDNNF danceDNNF(
                        args.input, logger, algorithmMode, args.threads,
                        args.debug);
                    danceDNNF.setSkipDNNFSize(args.skip_dnnf_size);
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
        return 1;
    }

    return 0;
}
