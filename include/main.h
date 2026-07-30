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
    int         time_limit = 1500;
    // 0 selects the instance-size-based automatic threshold.
    int         cc_ett_threshold = 0;
    // 0 keeps the normal per-component 3 ETT + 1 BFS policy unlimited.
    int         cc_ett_max_calls = 0;
    int         bfs_area_threshold = 0;
 
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
 
            // ── value flags: consume the *next* token as the value ────────────
            if (tok == "-a" || tok == "--alg"     ||
                tok == "-i" || tok == "--input"   ||
                tok == "-t" || tok == "--threads" ||
                tok == "--time-limit" || tok == "--cc-ett-threshold" ||
                tok == "--cc-ett-max-calls" ||
                tok == "--bfs-area-threshold" ||
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
                else if (tok == "--cc-ett-threshold") {
                    try { cc_ett_threshold = std::stoi(val); } catch (...) {
                        std::cerr << "[error] --cc-ett-threshold requires an integer.\n";
                        return false;
                    }
                }
                else if (tok == "--cc-ett-max-calls") {
                    try { cc_ett_max_calls = std::stoi(val); } catch (...) {
                        std::cerr << "[error] --cc-ett-max-calls requires an integer.\n";
                        return false;
                    }
                }
                else if (tok == "--bfs-area-threshold") {
                    try { bfs_area_threshold = std::stoi(val); } catch (...) {
                        std::cerr << "[error] --bfs-area-threshold requires an integer.\n";
                        return false;
                    }
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
                    if (key == "--cc-ett-threshold") {
                        try { cc_ett_threshold = std::stoi(val); } catch (...) {
                            std::cerr << "[error] --cc-ett-threshold value must be an integer.\n";
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
                    if (key == "--bfs-area-threshold") {
                        try { bfs_area_threshold = std::stoi(val); } catch (...) {
                            std::cerr << "[error] --bfs-area-threshold value must be an integer.\n";
                            return false;
                        }
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
            << "                           dxd   – DXD single-thread\n"
            << "                           mdxd  – DXD multi-thread (ETT)\n"
            << "                           mdlx  – Multi-thread DLX\n"
            << "  -i, --input   <path>   Path to the input test-case file (required)\n"
            << "  -t, --threads <n>      Number of threads (default: 8).\n"
            << "      --enable-cc-stats    Keep DynDXD's adaptive behavior\n"
            << "      --cc-ett-threshold <rows>  ETT statistics boundary (default: auto).\n"
            << "                                  auto: >2000=>200, >1000=>100, >100=>50, else 30.\n"
            << "      --cc-ett-max-calls <n>      Stop all CC work after n ETT queries.\n"
            << "                                  Statistics-only; default 0 is unlimited.\n"
            << "      --bfs-area-threshold <n>   Switch ETT to BFS only when rows*cols <= n.\n"
            << "                                  Default: 100000; 0 selects the default.\n"
            << "      --time-limit <s>    Solver time limit in seconds (default: 1500).\n"
            << "                         Effective only for mdxd / mdlx.\n"
            << "                         Values > 8 are clamped to 8.\n"
            << "  -d, --debug            Enable debug output\n"
            << "  -h, --help             Print this message and exit\n\n"
            << "Examples:\n"
            << "  " << prog << " -a dxd  -i ../data/run_set/Aarnet.txt\n"
            << "  " << prog << " -a mdxd -i ../data/run_set/Aarnet.txt -t 8\n"
            << "  " << prog << " -a mdxd -i ../data/run_set/Aarnet.txt -t 1   # single-thread benchmark\n"
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
            {"dlx", "dxz", "dxd", "ddxd", "mdlx"};
        if (!valid_algs.count(alg)) {
            std::cerr << "[error] unknown algorithm '" << alg << "'.\n"
                      << "        Valid choices: dlx  dxz  dxd  ddxd  mdlx\n";
            return false;
        }
        if (enable_cc_stats && (alg != "ddxd" || threads != 1)) {
            std::cerr << "[error] --enable-cc-stats requires --alg ddxd --threads 1.\n";
            return false;
        }
        if (time_limit < 1) {
            std::cerr << "[error] --time-limit must be >= 1.\n";
            return false;
        }
        if (cc_ett_threshold < 0) {
            std::cerr << "[error] --cc-ett-threshold must be >= 0 (0 means auto).\n";
            return false;
        }
        if (cc_ett_max_calls < 0) {
            std::cerr << "[error] --cc-ett-max-calls must be >= 0 (0 means unlimited).\n";
            return false;
        }
        if (cc_ett_max_calls > 0 && !enable_cc_stats) {
            std::cerr << "[error] --cc-ett-max-calls requires --enable-cc-stats.\n";
            return false;
        }
        if (bfs_area_threshold < 0) {
            std::cerr << "[error] --bfs-area-threshold must be >= 0 (0 means auto).\n";
            return false;
        }
 
        return true;
    }
};
