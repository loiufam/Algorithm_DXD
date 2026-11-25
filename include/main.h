#include "DXD.h"
#include "DXZ.h"
#include <filesystem>

// ─────────────────────────────────────────────────────────────────────────────
// ArgParser
//
// Zero-dependency command-line parser.  Supports:
//   • long flags   --alg dxd   --input file.txt   --threads 8   --debug
//   • short flags   -a  dxd    -i      file.txt   -t       8    -d
//   • boolean flags (present/absent, no value needed)
//   • --help / -h
//   • unknown-flag detection
//   • missing-required-argument detection
//
// Order on the command line is completely free.
// ─────────────────────────────────────────────────────────────────────────────
class ArgParser {
public:
    // Parsed values (populated by parse())
    std::string alg;
    std::string input;
    std::string dep_mode = "d"; // default to dynamic decomposition
    // std::string ALGORITHM_DXD_SOURCE_DIR = "/home/lyh/projects/lyh/Alg_DXD/Algorithm_DXD";
    int         threads = 8;
    bool        debug   = false;
    bool        enable_cc_stats = false;
    bool        enable_cc_time = false;
    bool        skip_dnnf_size = false;
    bool        pure_dyndxd = false;
    int         quick_stats_updates = 0;
    int         time_limit = 1500;
    // Default to 3 ETT queries; explicitly passing 0 keeps the budget unlimited.
    int         cc_ett_max_calls = 3;
    int         cc_bfs_max_calls = 10;
    int         bfs_area_threshold = 0;
    int         hybrid_ett_area_divisor = 10;
    bool        hybrid_ett_always = false;
    bool        hybrid_adaptive = false;
    bool        hybrid_runtime_policy = false;
    bool        hybrid_max_seed = false;
    std::string hybrid_engine = "runtime";
    std::string hybrid_decision_log;
    double      hybrid_stop_ratio = 0.8;
    double      hybrid_tree_op_ratio = 1.0;
    bool        hybrid_tree_op_ratio_set = false;
    bool        dxd_threshold = false;
    double      dxd_threshold_ratio = 0.5;
    double      threshold_ratio = 0.5;
 
    // ── parse ────────────────────────────────────────────────────────────────
    // Returns true on success, false if --help was requested or an error
    // occurred (error message already printed).
    bool parse(int argc, char* argv[]) {
        for (int i = 1; i < argc; ++i) {
            std::string tok = argv[i];
 
            // ── help ─────────────────────────────────────────────────────────
            if (tok == "-h" || tok == "--help") {
                printHelp(argv[0]);
                return false;
            }
 
            // ── boolean flags (no value) ──────────────────────────────────────
            if (tok == "-d" || tok == "--debug") {
                debug = true;
                continue;
            }
            if (tok == "--enable-cc-stats") {
                enable_cc_stats = true;
                continue;
            }
            if (tok == "--enable-cc-time") {
                enable_cc_time = true;
                continue;
            }
            if (tok == "--skip-dnnf-size") {
                skip_dnnf_size = true;
                continue;
            }
            if (tok == "--pure-dyndxd") {
                pure_dyndxd = true;
                continue;
            }
            if (tok == "--hybrid-ett-always") {
                hybrid_ett_always = true;
                continue;
            }
            if (tok == "--hybrid-adaptive") {
                hybrid_adaptive = true;
                continue;
            }
            if (tok == "--hybrid-runtime-policy") {
                hybrid_runtime_policy = true;
                continue;
            }
            if (tok == "--hybrid-max-seed") {
                hybrid_max_seed = true;
                continue;
            }
            if (tok == "--dxd-threshold") {
                dxd_threshold = true;
                continue;
            }
 
            // ── value flags: consume the *next* token as the value ────────────
            if (tok == "-a" || tok == "--alg"     ||
                tok == "-i" || tok == "--input"   ||
                tok == "-t" || tok == "--threads" ||
                tok == "--time-limit" ||
                tok == "--cc-ett-max-calls" ||
                tok == "--cc-bfs-max-calls" ||
                tok == "--quick-stats" ||
                tok == "--bfs-area-threshold" ||
                tok == "--hybrid-ett-area-divisor" ||
                tok == "--hybrid-stop-ratio" ||
                tok == "--hybrid-tree-op-ratio" ||
                tok == "--hybrid-decision-log" ||
                tok == "--dxd-threshold-ratio" ||
                tok == "--threshold-ratio" ||
                tok == "--hybrid-engine" ||
                tok == "-m" || tok == "--mode") {
 
                if (i + 1 >= argc) {
                    std::cerr << "[error] flag '" << tok
                              << "' requires a value.\n";
                    printHelp(argv[0]);
                    return false;
                }
                std::string val = argv[++i];
 
                if (tok == "-a" || tok == "--alg")         alg     = val;
                else if (tok == "-i" || tok == "--input")  input   = val;
                else if (tok == "-m" || tok == "--mode") {
                    if (val != "d" && val != "s") {
                        std::cerr << "[error] --mode value must be 'd' or 's'.\n";
                        return false;
                    }
                    dep_mode = val;
                }
                else if (tok == "-t" || tok == "--threads") {
                    try {
                        threads = std::stoi(val);
                    } catch (...) {
                        std::cerr << "[error] --threads requires an integer, got '"
                                  << val << "'.\n";
                        return false;
                    }
                }
                else if (tok == "--time-limit") {
                    try { time_limit = std::stoi(val); } catch (...) {
                        std::cerr << "[error] --time-limit requires an integer.\n";
                        return false;
                    }
                }
                else if (tok == "--cc-ett-max-calls") {
                    try { cc_ett_max_calls = std::stoi(val); } catch (...) {
                        std::cerr << "[error] --cc-ett-max-calls requires an integer.\n";
                        return false;
                    }
                }
                else if (tok == "--cc-bfs-max-calls") {
                    try { cc_bfs_max_calls = std::stoi(val); } catch (...) {
                        std::cerr << "[error] --cc-bfs-max-calls requires an integer.\n";
                        return false;
                    }
                }
                else if (tok == "--quick-stats") {
                    try { quick_stats_updates = std::stoi(val); } catch (...) {
                        std::cerr << "[error] --quick-stats requires an integer.\n";
                        return false;
                    }
                }
                else if (tok == "--bfs-area-threshold") {
                    try { bfs_area_threshold = std::stoi(val); } catch (...) {
                        std::cerr << "[error] --bfs-area-threshold requires an integer.\n";
                        return false;
                    }
                }
                else if (tok == "--hybrid-ett-area-divisor") {
                    try { hybrid_ett_area_divisor = std::stoi(val); }
                    catch (...) {
                        std::cerr << "[error] --hybrid-ett-area-divisor "
                                     "requires an integer.\n";
                        return false;
                    }
                }
                else if (tok == "--hybrid-stop-ratio") {
                    try { hybrid_stop_ratio = std::stod(val); }
                    catch (...) {
                        std::cerr << "[error] --hybrid-stop-ratio "
                                     "requires a number.\n";
                        return false;
                    }
                }
                else if (tok == "--hybrid-tree-op-ratio") {
                    try { hybrid_tree_op_ratio = std::stod(val); }
                    catch (...) {
                        std::cerr << "[error] --hybrid-tree-op-ratio "
                                     "requires a number.\n";
                        return false;
                    }
                    hybrid_tree_op_ratio_set = true;
                }
                else if (tok == "--hybrid-decision-log") {
                    hybrid_decision_log = val;
                }
                else if (tok == "--hybrid-engine") {
                    hybrid_engine = val;
                }
                else if (tok == "--dxd-threshold-ratio") {
                    try { dxd_threshold_ratio = std::stod(val); }
                    catch (...) {
                        std::cerr << "[error] --dxd-threshold-ratio "
                                     "requires a number.\n";
                        return false;
                    }
                    threshold_ratio = dxd_threshold_ratio;
                }
                else if (tok == "--threshold-ratio") {
                    try { threshold_ratio = std::stod(val); }
                    catch (...) {
                        std::cerr << "[error] --threshold-ratio "
                                     "requires a number.\n";
                        return false;
                    }
                    dxd_threshold_ratio = threshold_ratio;
                }
                continue;
            }
 
            // ── inline form  --flag=value  (optional convenience) ────────────
            if (tok.size() > 2 && tok[0] == '-' && tok[1] == '-') {
                auto eq = tok.find('=');
                if (eq != std::string::npos) {
                    std::string key = tok.substr(0, eq);
                    std::string val = tok.substr(eq + 1);
                    if (key == "--alg")     { alg     = val; continue; }
                    if (key == "--input")   { input   = val; continue; }
                    if (key == "--mode") {
                        if (val != "d" && val != "s") {
                            std::cerr << "[error] --mode value must be 'd' or 's'.\n";
                            return false;
                        }
                        dep_mode = val;
                        continue;
                    }
                    if (key == "--threads") {
                        try { threads = std::stoi(val); } catch (...) {
                            std::cerr << "[error] --threads value must be an integer.\n";
                            return false;
                        }
                        continue;
                    }
                    if (key == "--cc-ett-max-calls") {
                        try { cc_ett_max_calls = std::stoi(val); } catch (...) {
                            std::cerr << "[error] --cc-ett-max-calls value must be an integer.\n";
                            return false;
                        }
                        continue;
                    }
                    if (key == "--cc-bfs-max-calls") {
                        try { cc_bfs_max_calls = std::stoi(val); } catch (...) {
                            std::cerr << "[error] --cc-bfs-max-calls value must be an integer.\n";
                            return false;
                        }
                        continue;
                    }
                    if (key == "--quick-stats") {
                        try { quick_stats_updates = std::stoi(val); } catch (...) {
                            std::cerr << "[error] --quick-stats value must be an integer.\n";
                            return false;
                        }
                        continue;
                    }
                    if (key == "--bfs-area-threshold") {
                        try { bfs_area_threshold = std::stoi(val); } catch (...) {
                            std::cerr << "[error] --bfs-area-threshold value must be an integer.\n";
                            return false;
                        }
                        continue;
                    }
                    if (key == "--hybrid-ett-area-divisor") {
                        try {
                            hybrid_ett_area_divisor = std::stoi(val);
                        } catch (...) {
                            std::cerr << "[error] "
                                         "--hybrid-ett-area-divisor value "
                                         "must be an integer.\n";
                            return false;
                        }
                        continue;
                    }
                    if (key == "--hybrid-stop-ratio") {
                        try {
                            hybrid_stop_ratio = std::stod(val);
                        } catch (...) {
                            std::cerr << "[error] --hybrid-stop-ratio "
                                         "requires a number.\n";
                            return false;
                        }
                        continue;
                    }
                    if (key == "--hybrid-tree-op-ratio") {
                        try {
                            hybrid_tree_op_ratio = std::stod(val);
                        } catch (...) {
                            std::cerr << "[error] --hybrid-tree-op-ratio "
                                         "requires a number.\n";
                            return false;
                        }
                        hybrid_tree_op_ratio_set = true;
                        continue;
                    }
                    if (key == "--hybrid-decision-log") {
                        hybrid_decision_log = val;
                        continue;
                    }
                    if (key == "--hybrid-engine") {
                        hybrid_engine = val;
                        continue;
                    }
                    if (key == "--dxd-threshold-ratio") {
                        try {
                            dxd_threshold_ratio = std::stod(val);
                        } catch (...) {
                            std::cerr << "[error] --dxd-threshold-ratio "
                                         "requires a number.\n";
                            return false;
                        }
                        threshold_ratio = dxd_threshold_ratio;
                        continue;
                    }
                    if (key == "--threshold-ratio") {
                        try {
                            threshold_ratio = std::stod(val);
                        } catch (...) {
                            std::cerr << "[error] --threshold-ratio "
                                         "requires a number.\n";
                            return false;
                        }
                        dxd_threshold_ratio = threshold_ratio;
                        continue;
                    }
                }
            }
 
            // ── unknown flag ──────────────────────────────────────────────────
            std::cerr << "[error] unknown argument: '" << tok << "'.\n";
            printHelp(argv[0]);
            return false;
        }
 
        return validate(argv[0]);
    }
 
    // ── printHelp ────────────────────────────────────────────────────────────
    static void printHelp(const char* prog) {
        std::cout
            << "\nUsage:\n"
            << "  " << prog << " -a <alg> -i <input> [-t <threads>] [-d]\n\n"
            << "Options:\n"
            << "  -a, --alg     <name>   Algorithm to run (required)\n"
            << "                           dlx   – Dancing Links (count)\n"
            << "                           dxz   – DXZ (ZDD compilation)\n"
            << "                           dxd   – DXD\n"
            << "                           ddxd  – DynDXD (deferred ETT)\n"
            << "                           hybrid – HybridDXD (single BFS + min-block ETT)\n"
            << "                           mdlx  – Multi-thread DLX\n"
            << "  -i, --input   <path>   Path to the input test-case file (required)\n"
            << "  -t, --threads <n>      Number of threads (default: 8).\n"
            << "      --enable-cc-stats    Collect CC graph/decomposition statistics\n"
            << "                           (dxd, ddxd, or hybrid).\n"
            << "      --enable-cc-time     Measure CC operations only (single-thread DXD/DynDXD).\n"
            << "      --cc-ett-max-calls <n>      Limit ETT queries, then fall back to BFS.\n"
            << "                                  Default 3; 0 is unlimited.\n"
            << "      --cc-bfs-max-calls <n>      Stop DynDXD BFS after n queries.\n"
            << "                                  Default 10; 0 is unlimited.\n"
            << "      --pure-dyndxd              Keep DynDXD in ETT after root BFS;\n"
            << "                                  no BFS fallback and no ETT call limit.\n"
            << "      --skip-dnnf-size           Do not print DNNF Size.\n"
            << "      --hybrid-runtime-policy     Use runtime V/E/ET/EN decisions.\n"
            << "      --hybrid-engine <name>      Hybrid implementation: runtime | legacy.\n"
            << "      --hybrid-max-seed           Deprecated; runtime Hybrid always uses the\n"
            << "                                  smallest-area initial block as the Dyn seed.\n"
            << "      --hybrid-decision-log <p>   Write per-decision Hybrid CSV.\n"
            << "      --dxd-threshold             Enable relative BFS stop thresholds.\n"
            << "                                  First multi-split fixes each child\n"
            << "                                  threshold = child area * ratio.\n"
            << "      --threshold-ratio <r>       Shared min-Dyn/max-BFS and\n"
            << "                                  DXD-threshold area ratio.\n"
            << "                                  Legacy max-block BFS stays at 0.5.\n"
            << "                                  (default 0.5).\n"
            << "      --time-limit <s>    Solver time limit in seconds (default: 1500).\n"
            << "                         Effective for dxd / ddxd / mdlx.\n"
            << "  -d, --debug            Enable debug output\n"
            << "  -h, --help             Print this message and exit\n\n"
            << "Examples:\n"
            << "  " << prog << " -a dxd  -i ../data/run_set/Aarnet.txt\n"
            << "  " << prog << " -a ddxd -i ../data/run_set/Aarnet.txt -t 8\n"
            << "  " << prog << " -a ddxd -i ../data/run_set/Aarnet.txt -t 1   # single-thread benchmark\n"
            << "  " << prog << " -a dxd  -i ../data/exact_cover_benchmark/hard.txt --debug\n\n";
    }
 
private:
    // ── validate: check required flags and apply business rules ──────────────
    bool validate(const char* prog) {
        bool ok = true;
        if (alg.empty()) {
            std::cerr << "[error] --alg is required.\n";
            ok = false;
        }
        if (input.empty()) {
            std::cerr << "[error] --input is required.\n";
            ok = false;
        }
        if (!ok) { printHelp(prog); return false; }

        // Commands are often launched from build/ while using a path relative
        // to the repository root (for example data/batch_2/...). Resolve that
        // form without forcing callers to spell ../data/... .
        std::filesystem::path inputPath(input);
        if (!inputPath.is_absolute() && !std::filesystem::exists(inputPath)) {
            const auto sourceRelative =
                std::filesystem::path(ALGORITHM_DXD_SOURCE_DIR) / inputPath;
            if (std::filesystem::exists(sourceRelative)) {
                input = sourceRelative.string();
            }
        }
        if (!std::filesystem::exists(input)) {
            std::cerr << "[error] input file does not exist: " << input << "\n";
            return false;
        }
 
        // Thread cap
        if (threads > 8) {
            std::cerr << "[warn]  --threads " << threads
                      << " exceeds recommended maximum 8; clamping to 8.\n";
            threads = 8;
        }
        if (threads < 1) {
            std::cerr << "[warn]  --threads must be >= 1; resetting to 1.\n";
            threads = 1;
        }
 
        // Algorithm name validation
        static const std::unordered_set<std::string> valid_algs =
            {"dlx", "dxz", "dxd", "ddxd", "hybrid",
             "mdlx"};
        if (!valid_algs.count(alg)) {
            std::cerr << "[error] unknown algorithm '" << alg << "'.\n"
                      << "        Valid choices: dlx  dxz  dxd  ddxd  "
                         "hybrid  mdlx\n";
            return false;
        }
        if (enable_cc_stats &&
            alg != "ddxd" && alg != "dxd" && alg != "hybrid") {
            std::cerr << "[error] --enable-cc-stats requires "
                         "--alg dxd/ddxd/hybrid.\n";
            return false;
        }
        if (enable_cc_time && ((alg != "ddxd" && alg != "dxd") || threads != 1)) {
            std::cerr << "[error] --enable-cc-time requires --alg dxd/ddxd --threads 1.\n";
            return false;
        }
        if (enable_cc_time && enable_cc_stats) {
            std::cerr << "[error] --enable-cc-time and --enable-cc-stats are separate experiment modes.\n";
            return false;
        }
        if (pure_dyndxd && alg != "ddxd") {
            std::cerr << "[error] --pure-dyndxd requires --alg ddxd.\n";
            return false;
        }
        if (time_limit < 1) {
            std::cerr << "[error] --time-limit must be >= 1.\n";
            return false;
        }
        if (cc_ett_max_calls < 0) {
            std::cerr << "[error] --cc-ett-max-calls must be >= 0 (0 means unlimited).\n";
            return false;
        }
        if (cc_bfs_max_calls < 0) {
            std::cerr << "[error] --cc-bfs-max-calls must be >= 0 (0 means unlimited).\n";
            return false;
        }
        if (quick_stats_updates < 0) {
            std::cerr << "[error] --quick-stats must be >= 1 when specified.\n";
            return false;
        }
        if (quick_stats_updates > 0 && (alg != "ddxd" || threads != 1)) {
            std::cerr << "[error] --quick-stats requires --alg ddxd --threads 1.\n";
            return false;
        }
        if (quick_stats_updates > 0 &&
            (enable_cc_stats || enable_cc_time)) {
            std::cerr << "[error] --quick-stats cannot be combined with other CC experiment modes.\n";
            return false;
        }
        if (bfs_area_threshold < 0) {
            std::cerr << "[error] --bfs-area-threshold must be >= 0 (0 means auto).\n";
            return false;
        }
        if ((hybrid_ett_always || hybrid_adaptive ||
             hybrid_stop_ratio != 0.8 ||
             hybrid_tree_op_ratio != 1.0) &&
            alg != "hybrid") {
            std::cerr << "[error] Hybrid policy options require --alg hybrid.\n";
            return false;
        }
        if (hybrid_stop_ratio < 0.0 || hybrid_stop_ratio > 1.0) {
            std::cerr << "[error] --hybrid-stop-ratio must be in [0,1].\n";
            return false;
        }
        if (hybrid_tree_op_ratio < 0.0 ||
            hybrid_tree_op_ratio > 1.0) {
            std::cerr << "[error] --hybrid-tree-op-ratio must be in [0,1].\n";
            return false;
        }
        if (hybrid_runtime_policy && alg != "hybrid") {
            std::cerr << "[error] --hybrid-runtime-policy requires --alg hybrid.\n";
            return false;
        }
        if (hybrid_max_seed && alg != "hybrid") {
            std::cerr << "[error] --hybrid-max-seed requires --alg hybrid.\n";
            return false;
        }
        if (hybrid_engine != "runtime" && hybrid_engine != "legacy") {
            std::cerr << "[error] --hybrid-engine must be runtime or legacy.\n";
            return false;
        }
        if (hybrid_engine != "runtime" && alg != "hybrid") {
            std::cerr << "[error] --hybrid-engine requires --alg hybrid.\n";
            return false;
        }
        if (dxd_threshold_ratio < 0.0 ||
            dxd_threshold_ratio > 1.0) {
            std::cerr << "[error] --dxd-threshold-ratio must be in [0,1].\n";
            return false;
        }
        if (threshold_ratio < 0.0 || threshold_ratio > 1.0) {
            std::cerr << "[error] --threshold-ratio must be in [0,1].\n";
            return false;
        }
        if (dxd_threshold && alg != "dxd") {
            std::cerr << "[error] --dxd-threshold requires --alg dxd.\n";
            return false;
        }
        if (!hybrid_decision_log.empty() && alg != "hybrid") {
            std::cerr << "[error] --hybrid-decision-log requires --alg hybrid.\n";
            return false;
        }
        if (hybrid_ett_always && hybrid_adaptive) {
            std::cerr << "[error] --hybrid-ett-always and "
                         "--hybrid-adaptive are separate experiment modes.\n";
            return false;
        }
        if (hybrid_adaptive && !hybrid_tree_op_ratio_set) {
            std::cerr << "[error] --hybrid-adaptive requires "
                         "--hybrid-tree-op-ratio from the first experiment.\n";
            return false;
        }

        return true;
    }
};
