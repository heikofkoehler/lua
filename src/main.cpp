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
RunStatus runInternal(const std::string& source, VM& vm, const std::string& name, std::string& outError, const std::vector<Value>& args = {}) {
    try {
        Lexer lexer(source);
        lexer.setSourceName(name);

        Parser parser(lexer);
        auto program = parser.parse();

        if (!program) return RunStatus::COMPILE_ERROR;

        CodeGenerator codegen;
        auto function = codegen.generate(program.get(), name);

        if (!function) return RunStatus::COMPILE_ERROR;

        FunctionObject* funcPtr = function.get();
        vm.registerFunction(function.release());

        if (vm.run(*funcPtr, args)) {
            return RunStatus::OK;
        }
        outError = vm.lastErrorMessage();
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
bool run(const std::string& source, VM& vm, const std::string& name = "chunk", bool silent = false, const char* progname = "lua", const std::vector<Value>& args = {}) {
    std::string error;
    RunStatus status = runInternal(source, vm, name, error, args);
    if (status != RunStatus::OK && !silent) {
        std::cerr << progname << ": " << error << std::endl;
    }
    return status == RunStatus::OK;
}

// Run initialization code from LUA_INIT or LUA_INIT_5_5
bool runInit(VM& vm, bool ignoreEnv, const char* progname = "lua") {
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
                return run(buffer.str(), vm, "@" + path, false, progname);
            }
        } catch (...) {
            return false;
        }
    } else {
        // Run source
        return run(init, vm, "=LUA_INIT", false, progname);
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

        FunctionObject* funcPtr = function.get();
        vm.registerFunction(function.release());

        if (!vm.run(*funcPtr)) {
            return 1;
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error during execution: " << e.what() << std::endl;
        return 1;
    }
}

// Run file
int runFile(const std::string& path, VM& vm, const char* progname = "lua", const std::vector<Value>& args = {}) {
    try {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << progname << ": cannot open " << path << ": No such file or directory" << std::endl;
            return 1;
        }
        
        // Peek at signature, skipping BOM and any first-line comment starting with '#'
        std::streampos startPos = 0;
        char bom[3];
        file.read(bom, 3);
        if (file.gcount() == 3 &&
            static_cast<unsigned char>(bom[0]) == 0xEF &&
            static_cast<unsigned char>(bom[1]) == 0xBB &&
            static_cast<unsigned char>(bom[2]) == 0xBF) {
            startPos = 3;
        } else {
            file.clear();
            file.seekg(0);
            startPos = 0;
        }
        int firstChar = file.peek();
        if (firstChar == '#') {
            std::string commentLine;
            std::getline(file, commentLine);
            startPos = file.tellg();
        }
        char sig[4];
        file.read(sig, 4);
        bool isBytecode = (file.gcount() == 4 && std::memcmp(sig, "\x1bLua", 4) == 0);
        
        file.clear();
        if (isBytecode) {
            file.seekg(startPos);
        } else {
            file.seekg(0);
        }

        vm.setSourceName("@" + path);

        if (isBytecode) {
            auto function = FunctionObject::deserialize(file);
            if (!function) {
                std::cerr << progname << ": error: could not deserialize bytecode" << std::endl;
                return 1;
            }
            FunctionObject* funcPtr = function.get();
            vm.registerFunction(function.release());
            return vm.run(*funcPtr, args) ? 0 : 1;
        } else {
            std::stringstream buffer;
            buffer << file.rdbuf();
            return run(buffer.str(), vm, "@" + path, false, progname, args) ? 0 : 1;
        }
    } catch (const std::exception& e) {
        std::cerr << progname << ": " << e.what() << std::endl;
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

#define LUA_COPYRIGHT "Lua 5.5.0  Copyright (C) 1994-2024 Lua.org, PUC-Rio"

// Helper to get REPL prompt with __tostring support
std::string getPrompt(VM& vm, bool firstline) {
    const char* var = firstline ? "_PROMPT" : "_PROMPT2";
    Value val = vm.getGlobal(var);
    if (val.isNil()) {
        return firstline ? "> " : ">> ";
    }
    if (val.isString()) {
        return vm.getStringValue(val);
    }
    Value tostringFunc = vm.getGlobal("tostring");
    if (tostringFunc.isFunction() || tostringFunc.isNativeFunction() || tostringFunc.isCFunction()) {
        vm.push(tostringFunc);
        vm.push(val);
        if (vm.pcall(2)) {
            Value res = Value::nil();
            if (vm.currentCoroutine()->lastResultCount >= 2) {
                res = vm.peek(0);
            }
            while (vm.currentCoroutine()->lastResultCount > 0) {
                vm.pop();
                vm.currentCoroutine()->lastResultCount--;
            }
            if (res.isString()) {
                return vm.getStringValue(res);
            }
        } else {
            while (vm.currentCoroutine()->lastResultCount > 0) {
                vm.pop();
                vm.currentCoroutine()->lastResultCount--;
            }
        }
    }
    return val.toString();
}

static void checklocal(const std::string& line) {
    size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
        i++;
    }
    if (line.compare(i, 5, "local") == 0 &&
        (i + 5 < line.size()) && (line[i + 5] == ' ' || line[i + 5] == '\t')) {
        std::cerr << "warning: locals do not survive across lines in interactive mode" << std::endl;
    }
}

// Helper to print REPL results via Lua's 'print' function
static void printReplResults(VM& vm, const char* progname) {
    size_t resultCount = vm.currentCoroutine()->lastResultCount;
    if (resultCount > 0) {
        std::vector<Value> results;
        for (size_t i = 0; i < resultCount; i++) {
            results.push_back(vm.peek(resultCount - 1 - i));
        }
        for (size_t i = 0; i < resultCount; i++) {
            vm.pop();
        }
        Value printFunc = vm.getGlobal("print");
        if (!printFunc.isFunction() && !printFunc.isNativeFunction() && !printFunc.isCFunction()) {
            std::cerr << progname << ": error calling 'print' ('print' is not a function)" << std::endl;
        } else {
            vm.push(printFunc);
            for (const auto& val : results) {
                vm.push(val);
            }
            if (!vm.pcall(static_cast<int>(results.size() + 1))) {
                Value errVal = vm.pop();
                vm.pop(); // pop false
                std::string errStr = errVal.isString() ? vm.getStringValue(errVal) : errVal.toString();
                std::cerr << progname << ": error calling 'print' (" << errStr << ")" << std::endl;
            } else {
                while (vm.currentCoroutine()->lastResultCount > 0) {
                    vm.pop();
                    vm.currentCoroutine()->lastResultCount--;
                }
            }
        }
    }
}

// Interactive REPL
void repl(VM& vm, bool versionPrinted = false, const char* progname = "lua") {
    replVM = &vm;
    linenoiseSetCompletionCallback(completion);
    linenoiseHistoryLoad("lua_history.txt");

    if (!versionPrinted) {
        std::cout << LUA_COPYRIGHT << std::endl;
    }

    std::string buffer;
    bool multiLine = false;

    while (true) {
        std::string prompt = getPrompt(vm, !multiLine);
        
        if (!isatty(STDIN_FILENO)) {
            std::cout << prompt;
            std::cout.flush();
        }

        char* input = linenoise(prompt.c_str());
        if (input == nullptr) {
            if (multiLine && !buffer.empty()) {
                std::string err;
                runInternal(buffer, vm, "=stdin", err);
                std::cerr << err << std::endl;
            }
            std::cout << std::endl;
            break;
        }

        std::string line(input);
        linenoiseFree(input);

        // Meta commands
        if (!multiLine) {
            checklocal(line);
            if (line == "exit" || line == "quit") return;
            if (line == "help") {
                std::cout << "REPL Meta Commands:" << std::endl;
                std::cout << "  exit, quit     Exit the REPL" << std::endl;
                std::cout << "  help           Show this help" << std::endl;
                std::cout << "  globals        List all global variables" << std::endl;
                std::cout << "  =expr          Evaluate and print expression" << std::endl;
                continue;
            }
            if (line == "globals") {
                auto& globals = vm.globals();
                std::cout << "Global Variables:" << std::endl;
                for (auto const& [key, val] : globals) {
                    std::cout << "  " << key << "\t = " << val.toString() << std::endl;
                }
                continue;
            }
            if (!line.empty() && line[0] == '=') {
                line = "return " + line.substr(1);
            }
        }

        if (!line.empty() && isatty(STDIN_FILENO)) {
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
            std::string expr = buffer;
            if (expr.length() < 7 || expr.substr(0, 7) != "return ") {
                expr = "return " + buffer;
            }
            
            std::string err;
            RunStatus status = runInternal(expr, vm, "=stdin", err);
            
            if (status == RunStatus::OK) {
                printReplResults(vm, progname);
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
            printReplResults(vm, progname);
            buffer.clear();
            multiLine = false;
        } else {
            std::cerr << err << std::endl;
            buffer.clear();
            multiLine = false;
        }
    }
}

// Print usage
void printUsage(const char* program) {
    std::cerr << "usage: " << program << " [options] [script [args]]\n"
              << "Available options are:\n"
              << "  -e stat   execute string 'stat'\n"
              << "  -i        enter interactive mode after executing 'script'\n"
              << "  -l mod    require library 'mod' into global 'mod'\n"
              << "  -l g=mod  require library 'mod' into global 'g'\n"
              << "  -v        show version information\n"
              << "  -E        ignore environment variables\n"
              << "  -W        turn warnings on\n"
              << "  --        stop handling options\n"
              << "  -         stop handling options and execute stdin" << std::endl;
}

int main(int argc, char* argv[]) {
    const char* progname = argv[0];
    bool has_v = false;
    bool has_i = false;
    bool ignoreEnv = false;
    bool warnings = false;
    bool jitEnabled = true;
    bool compileOnly = false;
    bool listBytecode = false;
    bool isBytecode = false;
    std::string outputPath = "";

    struct OptionAction {
        enum Type { EXEC, REQUIRE } type;
        std::string arg;
    };
    std::vector<OptionAction> actions;

    std::string scriptPath = "";
    int scriptIndex = -1;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg[0] != '-') {
            scriptPath = arg;
            scriptIndex = i;
            break;
        }
        if (arg == "-") {
            scriptPath = "-";
            scriptIndex = i;
            break;
        }
        if (arg == "--") {
            if (i + 1 < argc) {
                scriptPath = argv[i + 1];
                scriptIndex = i + 1;
            }
            break;
        }

        if (arg == "--nojit") {
            jitEnabled = false;
            continue;
        } else if (arg == "-c" || arg == "--compile") {
            compileOnly = true;
            continue;
        } else if (arg == "-b" || arg == "--bytecode") {
            isBytecode = true;
            continue;
        } else if (arg == "-L" || arg == "--list") {
            listBytecode = true;
            continue;
        } else if (arg == "-o" || arg == "--output") {
            if (i + 1 < argc) {
                outputPath = argv[++i];
            } else {
                std::cerr << progname << ": '" << arg << "' needs argument" << std::endl;
                return 1;
            }
            continue;
        } else if (arg.rfind("-o", 0) == 0) {
            outputPath = arg.substr(2);
            continue;
        }

        if (arg == "-v") {
            has_v = true;
        } else if (arg == "-i") {
            has_i = true;
        } else if (arg == "-E") {
            ignoreEnv = true;
        } else if (arg == "-W") {
            warnings = true;
        } else if (arg == "-e") {
            if (i + 1 < argc) {
                actions.push_back({OptionAction::EXEC, argv[++i]});
            } else {
                std::cerr << progname << ": '-e' needs argument" << std::endl;
                printUsage(progname);
                return 1;
            }
        } else if (arg.rfind("-e", 0) == 0) {
            actions.push_back({OptionAction::EXEC, arg.substr(2)});
        } else if (arg == "-l") {
            if (i + 1 < argc) {
                actions.push_back({OptionAction::REQUIRE, argv[++i]});
            } else {
                std::cerr << progname << ": '-l' needs argument" << std::endl;
                printUsage(progname);
                return 1;
            }
        } else if (arg.rfind("-l", 0) == 0) {
            actions.push_back({OptionAction::REQUIRE, arg.substr(2)});
        } else {
            std::cerr << progname << ": unrecognized option '" << arg << "'" << std::endl;
            printUsage(progname);
            return 1;
        }
    }

    if (has_v) {
        std::cout << LUA_COPYRIGHT << std::endl;
        if (!has_i && actions.empty() && scriptPath.empty()) {
            return 0;
        }
    }

    VM vm;
    vm.setupSigintHandler();
    vm.setJitEnabled(jitEnabled);
    vm.setWarnEnabled(warnings);

    if (ignoreEnv) {
        vm.setGlobal("__IGNORE_ENV__", Value::boolean(true));
    }
    vm.initStandardLibrary();

    // Set up the 'arg' table
    TableObject* argTable = vm.createTable();
    if (scriptIndex >= 0) {
        argTable->set(Value::integer(0), Value::runtimeString(vm.internString(argv[scriptIndex])));
        for (int i = scriptIndex + 1; i < argc; i++) {
            argTable->set(Value::integer(i - scriptIndex), Value::runtimeString(vm.internString(argv[i])));
        }
        for (int i = scriptIndex - 1; i >= 0; i--) {
            argTable->set(Value::integer(i - scriptIndex), Value::runtimeString(vm.internString(argv[i])));
        }
    } else {
        argTable->set(Value::integer(0), Value::runtimeString(vm.internString(argv[0])));
        for (int i = 1; i < argc; i++) {
            argTable->set(Value::integer(i), Value::runtimeString(vm.internString(argv[i])));
        }
    }
    vm.setGlobal("arg", Value::table(argTable));

    if (!runInit(vm, ignoreEnv, progname)) {
        return 1;
    }

    for (const auto& act : actions) {
        if (act.type == OptionAction::REQUIRE) {
            std::string lib = act.arg;
            size_t eq = lib.find('=');
            std::string global, name;
            if (eq != std::string::npos) {
                global = lib.substr(0, eq);
                name = lib.substr(eq + 1);
            } else {
                size_t hyphen = lib.find('-');
                global = (hyphen != std::string::npos) ? lib.substr(0, hyphen) : lib;
                name = lib;
            }
            std::string cmd = "_G['" + global + "'] = require('" + name + "')";
            if (!run(cmd, vm, "=(command line)", false, progname)) {
                return 1;
            }
        } else if (act.type == OptionAction::EXEC) {
            if (!run(act.arg, vm, "=[string \"(command line)\"]", false, progname)) {
                return 1;
            }
        }
    }

    int result = 0;
    if (scriptPath == "-") {
        // Read from stdin
        std::stringstream buffer;
        buffer << std::cin.rdbuf();
        std::string source = buffer.str();

        Value argVal = vm.getGlobal("arg");
        if (!argVal.isTable()) {
            std::cerr << progname << ": 'arg' is not a table" << std::endl;
            return 1;
        }
        TableObject* argTbl = argVal.asTableObj();
        size_t n = argTbl->length();
        std::vector<Value> scriptArgs;
        for (size_t i = 1; i <= n; i++) {
            scriptArgs.push_back(argTbl->get(Value::integer(static_cast<int64_t>(i))));
        }

        size_t offset = 0;
        if (!source.empty() && source[0] == '#') {
            size_t nl = source.find('\n');
            if (nl != std::string::npos) offset = nl + 1;
            else offset = source.length();
        }

        if (source.length() >= offset + 4 && std::memcmp(source.data() + offset, "\x1bLua", 4) == 0) {
            std::istringstream is(source.substr(offset), std::ios::binary);
            auto function = FunctionObject::deserialize(is);
            if (!function) {
                std::cerr << progname << ": error: could not deserialize bytecode from stdin" << std::endl;
                result = 1;
            } else {
                FunctionObject* funcPtr = function.get();
                vm.registerFunction(function.release());
                result = vm.run(*funcPtr, scriptArgs) ? 0 : 1;
            }
        } else {
            result = run(source, vm, "=[string \"stdin\"]", false, progname, scriptArgs) ? 0 : 1;
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
            Value argVal = vm.getGlobal("arg");
            if (!argVal.isTable()) {
                std::cerr << progname << ": 'arg' is not a table" << std::endl;
                return 1;
            }
            TableObject* argTbl = argVal.asTableObj();
            size_t n = argTbl->length();
            std::vector<Value> scriptArgs;
            for (size_t i = 1; i <= n; i++) {
                scriptArgs.push_back(argTbl->get(Value::integer(static_cast<int64_t>(i))));
            }
            result = runFile(scriptPath, vm, progname, scriptArgs);
        }
    }

    if (has_i || (scriptPath.empty() && actions.empty() && isatty(STDIN_FILENO))) {
        repl(vm, has_v, progname);
    } else if (scriptPath.empty() && actions.empty()) {
        std::stringstream buffer;
        buffer << std::cin.rdbuf();
        std::string source = buffer.str();
        if (!source.empty()) {
            size_t offset = 0;
            if (!source.empty() && source[0] == '#') {
                size_t nl = source.find('\n');
                if (nl != std::string::npos) offset = nl + 1;
                else offset = source.length();
            }
            if (source.length() >= offset + 4 && std::memcmp(source.data() + offset, "\x1bLua", 4) == 0) {
                std::istringstream is(source.substr(offset), std::ios::binary);
                auto function = FunctionObject::deserialize(is);
                if (function) {
                    FunctionObject* funcPtr = function.get();
                    vm.registerFunction(function.release());
                    vm.run(*funcPtr);
                }
            } else {
                run(source, vm, "=[string \"stdin\"]", false, progname);
            }
        }
    }

    vm.close();
    return result;
}
