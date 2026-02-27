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
        auto chunk = codegen.generate(program.get());

        if (!chunk) {
            std::cerr << "Code generation error" << std::endl;
            return false;
        }

        // Execution
        return vm.run(*chunk);

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
    std::cerr << "    -v, --verbose    Print every instruction executed" << std::endl;
    std::cerr << "    -h, --help       Print this help message" << std::endl;
    std::cerr << "  Run without arguments to start REPL" << std::endl;
}

int main(int argc, char* argv[]) {
    bool verbose = false;
    std::string scriptPath = "";
    bool stopFlags = false;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (!stopFlags && (arg == "-v" || arg == "--verbose")) {
            verbose = true;
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
        // No script - start REPL
        repl(verbose);
        return 0;
    } else {
        // Run file
        return runFile(scriptPath, verbose);
    }
}
