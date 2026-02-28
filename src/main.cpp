#include "common/common.hpp"
#include "compiler/lexer.hpp"
#include "compiler/parser.hpp"
#include "compiler/codegen.hpp"
#include "vm/vm.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

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

// Run Lua source code
bool run(const std::string& source, VM& vm) {
    try {
        // Lexical analysis
        Lexer lexer(source);

        // Parsing
        Parser parser(lexer);
        auto program = parser.parse();

        if (!program) {
            std::cerr << "Parse error" << std::endl;
            return false;
        }

        // Code generation
        CodeGenerator codegen;
        auto function = codegen.generate(program.get());

        if (!function) {
            std::cerr << "Code generation error" << std::endl;
            return false;
        }

        // Execution
        return vm.run(*function);

    } catch (const CompileError& e) {
        std::cerr << e.what() << std::endl;
        return false;
    } catch (const RuntimeError& e) {
        std::cerr << e.what() << std::endl;
        return false;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return false;
    }
}

// Compile file to bytecode
int compileFile(const std::string& inputPath, const std::string& outputPath) {
    try {
        std::string source = readFile(inputPath);
        Lexer lexer(source);
        Parser parser(lexer);
        auto program = parser.parse();
        if (!program) return 1;

        CodeGenerator codegen;
        auto function = codegen.generate(program.get());
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
int runBytecode(const std::string& path, bool verbose) {
    try {
        std::ifstream is(path, std::ios::binary);
        if (!is.is_open()) {
            std::cerr << "Could not open bytecode file: " << path << std::endl;
            return 1;
        }

        auto function = FunctionObject::deserialize(is);
        VM vm;
        vm.setTraceExecution(verbose);
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
int runFile(const std::string& path, bool verbose) {
    try {
        std::string source = readFile(path);
        VM vm;
        vm.setTraceExecution(verbose);

        if (!run(source, vm)) {
            return 1;
        }

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}

// Interactive REPL
void repl(bool verbose) {
    VM vm;
    vm.setTraceExecution(verbose);
    std::string line;

    std::cout << "Lua VM (MVP) - Type 'exit' to quit" << std::endl;

    while (true) {
        std::cout << "> ";
        std::cout.flush();

        if (!std::getline(std::cin, line)) {
            std::cout << std::endl;
            break;
        }

        // Check for exit command
        if (line == "exit" || line == "quit") {
            break;
        }

        // Skip empty lines
        if (line.empty()) {
            continue;
        }

        // Run the line
        run(line, vm);

        // Reset VM for next input (optional - you might want to keep state)
        // vm.reset();
    }

    std::cout << "Goodbye!" << std::endl;
}

// Print usage
void printUsage(const char* program) {
    std::cerr << "Usage: " << program << " [options] [script]" << std::endl;
    std::cerr << "  Options:" << std::endl;
    std::cerr << "    -c, --compile    Compile source to bytecode" << std::endl;
    std::cerr << "    -o, --output     Output file for bytecode (default: out.luac)" << std::endl;
    std::cerr << "    -b, --bytecode   Execute input as pre-compiled bytecode" << std::endl;
    std::cerr << "    -v, --verbose    Print every instruction executed" << std::endl;
    std::cerr << "    -h, --help       Print this help message" << std::endl;
    std::cerr << "  Run without arguments to start REPL" << std::endl;
}

int main(int argc, char* argv[]) {
    bool verbose = false;
    bool compileOnly = false;
    bool isBytecode = false;
    std::string scriptPath = "";
    std::string outputPath = "";
    bool stopFlags = false;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (!stopFlags && (arg == "-v" || arg == "--verbose")) {
            verbose = true;
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
        } else if (!stopFlags && arg[0] == '-') {
            std::cerr << "Unknown option: " << arg << std::endl;
            printUsage(argv[0]);
            return 1;
        } else {
            if (scriptPath.empty()) {
                scriptPath = arg;
            } else {
                std::cerr << "Too many arguments" << std::endl;
                printUsage(argv[0]);
                return 1;
            }
        }
    }

    if (scriptPath.empty()) {
        if (compileOnly || !outputPath.empty()) {
            std::cerr << "Error: No script specified for compilation" << std::endl;
            return 1;
        }
        // No script - start REPL
        repl(verbose);
        return 0;
    } else {
        if (compileOnly) {
            if (outputPath.empty()) outputPath = "out.luac";
            return compileFile(scriptPath, outputPath);
        } else if (isBytecode) {
            return runBytecode(scriptPath, verbose);
        } else {
            // Normal source execution, but if outputPath is set, compile it first too
            if (!outputPath.empty()) {
                compileFile(scriptPath, outputPath);
            }
            return runFile(scriptPath, verbose);
        }
    }
}
