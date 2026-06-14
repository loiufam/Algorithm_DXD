#include "DXD.h"
#include "DXZ.h"

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
    int         threads = 8;
    bool        debug   = false;
 
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
 
            // ── value flags: consume the *next* token as the value ────────────
            if (tok == "-a" || tok == "--alg"     ||
                tok == "-i" || tok == "--input"   ||
                tok == "-t" || tok == "--threads" ||
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
 
        return true;
    }
};