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
#include <algorithm>
#include "linenoise.h"

// ANSI Color Codes
#define COLOR_RESET   "\033[0m"
#define COLOR_BOLD    "\033[1m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_MAGENTA "\033[35m"

// Global VM pointer for linenoise completion
static VM* replVM = nullptr;

// Completion callback for linenoise
void completion(const char* buf, linenoiseCompletions* lc) {
    if (!replVM) return;

    std::string line(buf);
    if (line.empty()) return;

    // Find the start of the identifier being completed
    size_t start = line.find_last_of(" \t+-*/%^#=<>()[]{};:,.");
    std::string prefix;
    std::string search;
    
    if (start == std::string::npos) {
        search = line;
    } else {
        prefix = line.substr(0, start + 1);
        search = line.substr(start + 1);
    }

    // Handle table member completion (e.g., math.s or table.sub.f)
    size_t dot = search.find_last_of('.');
    TableObject* targetTable = nullptr;
    std::string tablePrefix;
    std::string memberSearch;

    if (dot != std::string::npos) {
        std::string fullTableName = search.substr(0, dot);
        memberSearch = search.substr(dot + 1);
        
        // Traverse tables (e.g., math.sin -> lookup "math")
        std::vector<std::string> parts;
        size_t last = 0;
        size_t next = 0;
        while ((next = fullTableName.find('.', last)) != std::string::npos) {
            parts.push_back(fullTableName.substr(last, next - last));
            last = next + 1;
        }
        parts.push_back(fullTableName.substr(last));

        Value current = Value::nil();
        bool first = true;
        for (const auto& part : parts) {
            if (first) {
                current = replVM->getGlobal(part);
                first = false;
            } else if (current.isTable()) {
                current = current.asTableObj()->get(part);
            } else {
                current = Value::nil();
                break;
            }
        }

        if (current.isTable()) {
            targetTable = current.asTableObj();
            tablePrefix = fullTableName + ".";
        }
    } else {
        memberSearch = search;
    }

    if (targetTable) {
        // Complete members of the table
        targetTable->data(); // Need to iterate members
        for (auto const& [key, val] : targetTable->data()) {
            if (key.isString()) {
                std::string name = replVM->getStringValue(key);
                if (name.substr(0, memberSearch.length()) == memberSearch) {
                    linenoiseAddCompletion(lc, (prefix + tablePrefix + name).c_str());
                }
            }
        }
    } else {
        // Complete globals
        auto& globals = replVM->globals();
        for (auto const& [key, val] : globals) {
            if (key.substr(0, search.length()) == search) {
                linenoiseAddCompletion(lc, (prefix + key).c_str());
            }
        }
        
        // Also complete keywords
        static const std::vector<std::string> keywords = {
            "and", "break", "do", "else", "elseif", "end",
            "false", "for", "function", "goto", "if", "in",
            "local", "nil", "not", "or", "repeat", "return",
            "then", "true", "until", "while"
        };
        for (const auto& kw : keywords) {
            if (kw.substr(0, search.length()) == search) {
                linenoiseAddCompletion(lc, (prefix + kw).c_str());
            }
        }
    }
}

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
        
        // Seek back to start
        file.clear();
        file.seekg(0);

        vm.setSourceName("@" + path);

        if (isBytecode) {
            auto function = FunctionObject::deserialize(file);
            if (!function) {
                std::cerr << "Error: Could not deserialize bytecode" << std::endl;
                return 1;
            }
            return vm.run(*function) ? 0 : 1;
        } else {
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

// Disassemble file (source or bytecode)
int disassembleFile(const std::string& path, const std::string& outputPath) {
    try {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Could not open file: " + path);
        }
        
        // Peek at signature
        char sig[4];
        file.read(sig, 4);
        bool isBytecode = (file.gcount() == 4 && std::memcmp(sig, "\x1bLua", 4) == 0);
        
        // Seek back to start
        file.clear();
        file.seekg(0);

        // Set up output stream
        std::ofstream outFile;
        if (!outputPath.empty()) {
            outFile.open(outputPath);
            if (!outFile.is_open()) {
                std::cerr << "Could not open output file: " << outputPath << std::endl;
                return 1;
            }
        }

        // We need to temporarily redirect std::cout for Chunk::disassemble
        // because it uses std::cout directly.
        // A better fix would be to change Chunk::disassemble to take an ostream.
        std::streambuf* oldCoutStreamBuf = std::cout.rdbuf();
        if (!outputPath.empty()) {
            std::cout.rdbuf(outFile.rdbuf());
        }

        if (isBytecode) {
            auto function = FunctionObject::deserialize(file);
            if (!function) {
                if (!outputPath.empty()) std::cout.rdbuf(oldCoutStreamBuf);
                std::cerr << "Error: Could not deserialize bytecode" << std::endl;
                return 1;
            }
            function->disassemble();
        } else {
            std::stringstream buffer;
            buffer << file.rdbuf();
            std::string source = buffer.str();
            
            Lexer lexer(source);
            lexer.setSourceName("@" + path);
            Parser parser(lexer);
            auto program = parser.parse();
            if (!program) {
                if (!outputPath.empty()) std::cout.rdbuf(oldCoutStreamBuf);
                return 1;
            }

            CodeGenerator codegen;
            auto function = codegen.generate(program.get(), "@" + path);
            if (!function) {
                if (!outputPath.empty()) std::cout.rdbuf(oldCoutStreamBuf);
                return 1;
            }
            
            function->disassemble();
        }

        if (!outputPath.empty()) {
            std::cout.rdbuf(oldCoutStreamBuf);
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}

// Interactive REPL
void repl(VM& vm) {
    std::string buffer;
    bool multiLine = false;
    
    replVM = &vm;
    linenoiseSetCompletionCallback(completion);
    linenoiseHistoryLoad("lua_history.txt");

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
        
        char* input = linenoise(prompt.c_str());
        if (input == nullptr) {
            std::cout << std::endl;
            break;
        }

        std::string line(input);
        linenoiseFree(input);

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

        if (!line.empty()) {
            linenoiseHistoryAdd(line.c_str());
            linenoiseHistorySave("lua_history.txt");
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
    std::cerr << "    -L, --list       List (disassemble) bytecode" << std::endl;
    std::cerr << "    -h, --help       Print this help message" << std::endl;
    std::cerr << "  Run without arguments to start REPL" << std::endl;
}

int main(int argc, char* argv[]) {
    bool verbose = false;
    bool compileOnly = false;
    bool listBytecode = false;
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
        
        if (stopFlags) {
            if (scriptPath.empty()) {
                scriptPath = arg;
                scriptIndex = i;
            }
            continue;
        }

        if (arg == "--") {
            stopFlags = true;
            continue;
        }

        if (arg[0] == '-' && arg.length() > 1) {
            if (arg == "-v" || arg == "--verbose") {
                verbose = true;
            } else if (arg == "-i") {
                interactive = true;
            } else if (arg == "-E") {
                ignoreEnv = true;
            } else if (arg == "-L" || arg == "--list") {
                listBytecode = true;
            } else if (arg == "-c" || arg == "--compile") {
                compileOnly = true;
            } else if (arg == "-b" || arg == "--bytecode") {
                isBytecode = true;
            } else if (arg.substr(0, 2) == "-o" || arg == "--output") {
                if (arg == "-o") {
                    if (i + 1 < argc) outputPath = argv[++i];
                    else { std::cerr << "Error: -o requires an argument" << std::endl; return 1; }
                } else if (arg == "--output") {
                    if (i + 1 < argc) outputPath = argv[++i];
                    else { std::cerr << "Error: --output requires an argument" << std::endl; return 1; }
                } else {
                    outputPath = arg.substr(2);
                }
            } else if (arg.substr(0, 2) == "-l") {
                std::string lib;
                if (arg == "-l") {
                    if (i + 1 < argc) lib = argv[++i];
                    else { std::cerr << "Error: -l requires an argument" << std::endl; return 1; }
                } else {
                    lib = arg.substr(2);
                }
                loadLibs.push_back(lib);
            } else if (arg.substr(0, 2) == "-e") {
                std::string code;
                if (arg == "-e") {
                    if (i + 1 < argc) code = argv[++i];
                    else { std::cerr << "Error: -e requires an argument" << std::endl; return 1; }
                } else {
                    code = arg.substr(2);
                }
                executeStrings.push_back(code);
            } else if (arg == "-h" || arg == "--help") {
                printUsage(argv[0]);
                return 0;
            } else if (scriptPath.empty()) {
                // Combined short flags (only if no script yet, to avoid consuming script args)
                for (size_t j = 1; j < arg.length(); j++) {
                    char c = arg[j];
                    if (c == 'v') verbose = true;
                    else if (c == 'i') interactive = true;
                    else if (c == 'E') ignoreEnv = true;
                    else {
                        std::cerr << "Unknown option: -" << c << std::endl;
                        return 1;
                    }
                }
            }
            // If scriptPath is set, we just let unknown flags pass through to 'arg' table
        } else {
            if (scriptPath.empty()) {
                scriptPath = arg;
                scriptIndex = i;
                // Standard Lua stops parsing flags here, but we allow it for better UX.
                // We only stop if explicitly requested with --.
            }
        }
    }

    VM vm;
    vm.setTraceExecution(verbose);

    // If -o is specified without -c or -L or execution flags, imply -c
    if (!outputPath.empty() && !compileOnly && !listBytecode && !isBytecode && executeStrings.empty() && !interactive) {
        compileOnly = true;
    }

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
        } else if (listBytecode) {
            result = disassembleFile(scriptPath, outputPath);
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
