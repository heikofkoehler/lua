#include "common/common.hpp"
#include "compiler/lexer.hpp"
#include "compiler/parser.hpp"
#include "compiler/codegen.hpp"
#include "vm/vm.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <cstring>
#include <vector>

// ANSI Color Codes
#define COLOR_RESET   "\033[0m"
#define COLOR_BOLD    "\033[1m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_MAGENTA "\033[35m"

// Read file into string
std::string readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file: " + path);
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// Result status for REPL
enum class RunStatus {
    OK,
    COMPILE_ERROR,
    RUNTIME_ERROR,
    INCOMPLETE
};

// Internal run implementation that returns status and error
RunStatus runInternal(const std::string& source, VM& vm, const std::string& name, std::string& outError) {
    try {
        Lexer lexer(source);
        lexer.setSourceName(name);

        Parser parser(lexer);
        auto program = parser.parse();

        if (!program) return RunStatus::COMPILE_ERROR;

        CodeGenerator codegen;
        auto function = codegen.generate(program.get(), name);

        if (!function) return RunStatus::COMPILE_ERROR;

        if (vm.run(*function)) {
            return RunStatus::OK;
        }
        return RunStatus::RUNTIME_ERROR;
    } catch (const CompileError& e) {
        outError = e.what();
        // Detect incomplete input
        if (outError.find("near <eof>") != std::string::npos || 
            outError.find("unfinished string") != std::string::npos ||
            outError.find("unfinished long string") != std::string::npos ||
            outError.find("unfinished long comment") != std::string::npos) {
            return RunStatus::INCOMPLETE;
        }
        return RunStatus::COMPILE_ERROR;
    } catch (const RuntimeError& e) {
        outError = e.what();
        return RunStatus::RUNTIME_ERROR;
    } catch (const std::exception& e) {
        outError = e.what();
        return RunStatus::RUNTIME_ERROR;
    }
}

// Run Lua source code
bool run(const std::string& source, VM& vm, const std::string& name = "chunk", bool silent = false) {
    std::string error;
    RunStatus status = runInternal(source, vm, name, error);
    if (status != RunStatus::OK && !silent) {
        std::string displayPath = name;
        if (!displayPath.empty() && displayPath[0] == '=') {
            displayPath = displayPath.substr(1);
        }
        std::cerr << error << std::endl;
    }
    return status == RunStatus::OK;
}


// Run initialization code from LUA_INIT or LUA_INIT_5_5
bool runInit(VM& vm, bool ignoreEnv) {
    if (ignoreEnv) return true;
    const char* init = getenv("LUA_INIT_5_5");
    if (!init) init = getenv("LUA_INIT");
    if (!init) return true;

    if (init[0] == '@') {
        // Run file
        std::string path = init + 1;
        try {
            std::ifstream file(path);
            if (file.is_open()) {
                std::stringstream buffer;
                buffer << file.rdbuf();
                return run(buffer.str(), vm, "LUA_INIT", false);
            }
        } catch (...) {
            return false;
        }
    } else {
        // Run source
        return run(init, vm, "LUA_INIT", false);
    }
    return true;
}

// Compile file to bytecode
int compileFile(const std::string& inputPath, const std::string& outputPath) {
    try {
        std::string source = readFile(inputPath);
        Lexer lexer(source);
        lexer.setSourceName("@" + inputPath);
        Parser parser(lexer);
        auto program = parser.parse();
        if (!program) return 1;

        CodeGenerator codegen;
        auto function = codegen.generate(program.get(), "@" + inputPath);
        if (!function) return 1;

        std::ofstream os(outputPath, std::ios::binary);
        if (!os.is_open()) {
            std::cerr << "Could not open output file: " << outputPath << std::endl;
            return 1;
        }
        function->serialize(os);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error during compilation: " << e.what() << std::endl;
        return 1;
    }
}

// Run pre-compiled bytecode
int runBytecode(const std::string& path, VM& vm) {
    try {
        std::ifstream is(path, std::ios::binary);
        if (!is.is_open()) {
            std::cerr << "Could not open bytecode file: " << path << std::endl;
            return 1;
        }

        auto function = FunctionObject::deserialize(is);

        vm.setSourceName("@" + path);

        if (!vm.run(*function)) {
            return 1;
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error during execution: " << e.what() << std::endl;
        return 1;
    }
}

// Run file
int runFile(const std::string& path, VM& vm) {
    try {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Could not open file: " + path);
        }
        
        // Peek at signature
        char sig[4];
        file.read(sig, 4);
        bool isBytecode = (file.gcount() == 4 && std::memcmp(sig, "\x1bLua", 4) == 0);
        
        vm.setSourceName("@" + path);

        if (isBytecode) {
            auto function = FunctionObject::deserialize(file);
            if (!function) {
                std::cerr << "Error: Could not deserialize bytecode" << std::endl;
                return 1;
            }
            return vm.run(*function) ? 0 : 1;
        } else {
            // Seek back to start if not bytecode
            file.clear();
            file.seekg(0);
            std::stringstream buffer;
            buffer << file.rdbuf();
            std::string source = buffer.str();
            return run(source, vm, "@" + path, false) ? 0 : 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}

// Interactive REPL
void repl(VM& vm) {
    std::string buffer;
    bool multiLine = false;

    std::cout << COLOR_BOLD << "Lua 5.5.0 REPL" << COLOR_RESET << " - Type 'help' for commands" << std::endl;

    while (true) {
        std::string prompt;
        if (!multiLine) {
            Value promptVal = vm.getGlobal("_PROMPT");
            prompt = promptVal.isString() ? promptVal.toString() : COLOR_CYAN "> " COLOR_RESET;
        } else {
            Value promptVal = vm.getGlobal("_PROMPT2");
            prompt = promptVal.isString() ? promptVal.toString() : COLOR_CYAN ">> " COLOR_RESET;
        }
        
        std::cout << prompt;
        std::cout.flush();

        std::string line;
        if (!std::getline(std::cin, line)) {
            std::cout << std::endl;
            break;
        }

        // Meta commands
        if (!multiLine) {
            if (line == "exit" || line == "quit") return;
            if (line == "help") {
                std::cout << COLOR_YELLOW << "REPL Meta Commands:" << COLOR_RESET << std::endl;
                std::cout << "  exit, quit     Exit the REPL" << std::endl;
                std::cout << "  help           Show this help" << std::endl;
                std::cout << "  globals        List all global variables" << std::endl;
                std::cout << "  =expr          Evaluate and print expression" << std::endl;
                continue;
            }
            if (line == "globals") {
                auto& globals = vm.globals();
                std::cout << COLOR_YELLOW << "Global Variables:" << COLOR_RESET << std::endl;
                for (auto const& [key, val] : globals) {
                    std::cout << "  " << key << "\t = " << val.toString() << std::endl;
                }
                continue;
            }
            if (!line.empty() && line[0] == '=') {
                line = "return " + line.substr(1);
            }
        }

        if (buffer.empty()) {
            buffer = line;
        } else {
            buffer += "\n" + line;
        }

        if (buffer.empty()) continue;

        // Try to evaluate as expression first if not in multi-line mode
        if (!multiLine) {
            // Only prepend 'return ' if it's not already there
            std::string expr = buffer;
            if (expr.length() < 7 || expr.substr(0, 7) != "return ") {
                expr = "return " + buffer;
            }
            
            std::string err;
            RunStatus status = runInternal(expr, vm, "=stdin", err);
            
            if (status == RunStatus::OK) {
                // Print results
                for (size_t i = 0; i < vm.currentCoroutine()->lastResultCount; i++) {
                    std::cout << COLOR_GREEN << vm.peek(vm.currentCoroutine()->lastResultCount - 1 - i).toString() << COLOR_RESET;
                    if (i < vm.currentCoroutine()->lastResultCount - 1) std::cout << "\t";
                }
                if (vm.currentCoroutine()->lastResultCount > 0) std::cout << std::endl;
                
                // Pop results
                for (size_t i = 0; i < vm.currentCoroutine()->lastResultCount; i++) {
                    vm.pop();
                }
                buffer.clear();
                multiLine = false;
                continue;
            }
        }

        // Run the buffer normally
        std::string err;
        RunStatus status = runInternal(buffer, vm, "=stdin", err);

        if (status == RunStatus::INCOMPLETE) {
            multiLine = true;
            continue;
        } else if (status == RunStatus::OK) {
            buffer.clear();
            multiLine = false;
        } else {
            std::cerr << COLOR_RED << err << COLOR_RESET << std::endl;
            buffer.clear();
            multiLine = false;
        }
    }
}

// Print usage
void printUsage(const char* program) {
    std::cerr << "Usage: " << program << " [options] [script]" << std::endl;
    std::cerr << "  Options:" << std::endl;
    std::cerr << "    -e stat      execute string 'stat'" << std::endl;
    std::cerr << "    -i           enter interactive mode after executing 'script'" << std::endl;
    std::cerr << "    -l name      require library 'name'" << std::endl;
    std::cerr << "    -v           show version information" << std::endl;
    std::cerr << "    -E           ignore environment variables" << std::endl;
    std::cerr << "    -c, --compile    Compile source to bytecode" << std::endl;
    std::cerr << "    -o, --output     Output file for bytecode (default: out.luac)" << std::endl;
    std::cerr << "    -b, --bytecode   Execute input as pre-compiled bytecode" << std::endl;
    std::cerr << "    -h, --help       Print this help message" << std::endl;
    std::cerr << "  Run without arguments to start REPL" << std::endl;
}

int main(int argc, char* argv[]) {
    bool verbose = false;
    bool compileOnly = false;
    bool isBytecode = false;
    bool interactive = false;
    bool ignoreEnv = false;
    std::vector<std::string> executeStrings;
    std::vector<std::string> loadLibs;
    std::string scriptPath = "";
    int scriptIndex = 0;
    std::string outputPath = "";
    bool stopFlags = false;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (!stopFlags && (arg == "-v" || arg == "--verbose")) {
            verbose = true;
            if (argc == 2) {
                std::cout << "Lua 5.5.0 (MVP)" << std::endl;
                return 0;
            }
        } else if (!stopFlags && arg == "-i") {
            interactive = true;
        } else if (!stopFlags && arg == "-E") {
            ignoreEnv = true;
        } else if (!stopFlags && arg.length() >= 2 && arg[0] == '-' && arg[1] == 'l') {
            std::string lib;
            if (arg.length() > 2) {
                lib = arg.substr(2);
            } else if (i + 1 < argc) {
                lib = argv[++i];
            } else {
                std::cerr << "Error: -l option requires an argument" << std::endl;
                return 1;
            }
            loadLibs.push_back(lib);
        } else if (!stopFlags && arg.length() >= 2 && arg[0] == '-' && arg[1] == 'e') {
            std::string code;
            if (arg.length() > 2) {
                code = arg.substr(2);
            } else if (i + 1 < argc) {
                code = argv[++i];
            } else {
                std::cerr << "Error: -e option requires an argument" << std::endl;
                return 1;
            }
            executeStrings.push_back(code);
        } else if (!stopFlags && (arg == "-c" || arg == "--compile")) {
            compileOnly = true;
        } else if (!stopFlags && (arg == "-b" || arg == "--bytecode")) {
            isBytecode = true;
        } else if (!stopFlags && (arg == "-o" || arg == "--output")) {
            if (i + 1 < argc) {
                outputPath = argv[++i];
            } else {
                std::cerr << "Error: -o option requires an argument" << std::endl;
                return 1;
            }
        } else if (!stopFlags && (arg == "-h" || arg == "--help")) {
            printUsage(argv[0]);
            return 0;
        } else if (!stopFlags && arg == "--") {
            stopFlags = true;
        } else if (!stopFlags && arg == "-") {
            if (scriptPath.empty()) {
                scriptPath = "-";
                scriptIndex = i;
                stopFlags = true;
            } else {
                std::cerr << "Too many arguments" << std::endl;
                return 1;
            }
        } else if (!stopFlags && arg[0] == '-' && arg.length() > 1) {
            // Handle combined short flags like -vi
            for (size_t j = 1; j < arg.length(); j++) {
                char c = arg[j];
                if (c == 'v') verbose = true;
                else if (c == 'i') interactive = true;
                else if (c == 'E') { /* ignore for now */ }
                else if (c == 'W') { /* ignore for now */ }
                else {
                    std::cerr << "Unknown option: -" << c << std::endl;
                    return 1;
                }
            }
        } else {
            if (scriptPath.empty()) {
                scriptPath = arg;
                scriptIndex = i;
                // Once we find the script, the remaining arguments are for the script
                stopFlags = true;
            }
        }
    }

    VM vm;
    vm.setTraceExecution(verbose);
    if (scriptPath == "-") {
        vm.setSourceName("=[string \"stdin\"]");
    } else if (!scriptPath.empty()) {
        vm.setSourceName("@" + scriptPath);
    }
    if (ignoreEnv) {
        vm.setGlobal("__IGNORE_ENV__", Value::boolean(true));
    }
    vm.initStandardLibrary();

    // Set up the 'arg' table early so -e scripts can access it
    TableObject* argTable = vm.createTable();
    for (int i = 0; i < argc; i++) {
        argTable->set(Value::integer(static_cast<int64_t>(i - (scriptIndex == 0 ? argc : scriptIndex))), 
                      Value::runtimeString(vm.internString(argv[i])));
    }
    vm.setGlobal("arg", Value::table(argTable));

    if (!runInit(vm, ignoreEnv)) {
        return 1;
    }

    for (const auto& lib : loadLibs) {
        std::string name = lib;
        std::string global = "";
        size_t eq = lib.find('=');
        if (eq != std::string::npos) {
            global = lib.substr(0, eq);
            name = lib.substr(eq + 1);
        } else {
            global = lib;
        }
        
        std::string cmd = "_G['" + global + "'] = require('" + name + "')";
        
        if (!run(cmd, vm, "=(command line)", false)) {
            return 1;
        }
    }

    for (const auto& code : executeStrings) {
        if (!run(code, vm, "=[string \"(command line)\"]", false)) {
            return 1;
        }
    }

    int result = 0;
    if (scriptPath == "-") {
        // Read from stdin
        std::stringstream buffer;
        buffer << std::cin.rdbuf();
        std::string source = buffer.str();
        
        if (source.length() >= 4 && std::memcmp(source.data(), "\x1bLua", 4) == 0) {
            std::istringstream is(source.substr(4), std::ios::binary);
            auto function = FunctionObject::deserialize(is);
            if (!function) {
                std::cerr << "Error: Could not deserialize bytecode from stdin" << std::endl;
                result = 1;
            } else {
                result = vm.run(*function) ? 0 : 1;
            }
        } else {
            result = run(source, vm, "=[string \"stdin\"]", false) ? 0 : 1;
        }
    } else if (!scriptPath.empty()) {
        if (compileOnly) {
            if (outputPath.empty()) outputPath = "out.luac";
            result = compileFile(scriptPath, outputPath);
        } else if (isBytecode) {
            result = runBytecode(scriptPath, vm);
        } else {
            // Normal file execution
            result = runFile(scriptPath, vm);
        }
    }

    if (interactive || (scriptPath.empty() && executeStrings.empty() && isatty(STDIN_FILENO))) {
        repl(vm);
    } else if (scriptPath.empty() && executeStrings.empty()) {
        std::stringstream buffer;
        buffer << std::cin.rdbuf();
        std::string source = buffer.str();
        if (!source.empty()) {
            if (source.length() >= 4 && std::memcmp(source.data(), "\x1bLua", 4) == 0) {
                std::istringstream is(source.substr(4), std::ios::binary);
                auto function = FunctionObject::deserialize(is);
                if (function) vm.run(*function);
            } else {
                vm.setSourceName("=[string \"stdin\"]");
                run(source, vm, "=[string \"stdin\"]", false);
            }
        }
    }

    return result;
}
