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
int runFile(const std::string& path) {
    try {
        std::string source = readFile(path);
        VM vm;

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
void repl() {
    VM vm;
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
    std::cerr << "Usage: " << program << " [script]" << std::endl;
    std::cerr << "  Run without arguments to start REPL" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc == 1) {
        // No arguments - start REPL
        repl();
        return 0;
    } else if (argc == 2) {
        // Run file
        return runFile(argv[1]);
    } else {
        // Invalid arguments
        printUsage(argv[0]);
        return 1;
    }
}
