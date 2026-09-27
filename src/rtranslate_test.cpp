// Test harness for the stack-to-register translator.
// Compiles Lua files with the existing codegen, translates each function,
// and reports success/failure with disassembly on demand.
//
// Usage: rtranslate_test <file.lua> [--disassemble]

#include "compiler/rtranslate.hpp"
#include "compiler/lexer.hpp"
#include "compiler/parser.hpp"
#include "compiler/codegen.hpp"
#include "value/function.hpp"
#include <fstream>
#include <iostream>
#include <sstream>

static int failures = 0;
static int successes = 0;

void translateFunction(FunctionObject* func, bool disassemble, int depth = 0) {
    std::string indent(depth * 2, ' ');
    RTranslateResult r = translateToRegister(func);
    if (!r.ok) {
        std::cout << indent << "FAIL " << func->name() << ": " << r.error << "\n";
        failures++;
    } else {
        std::cout << indent << "OK " << func->name()
                  << " (" << r.code.size() << " instrs, "
                  << r.maxRegisters << " regs)\n";
        successes++;
        if (disassemble) {
            ropDisassembleCode(r.code, func->name());
        }
    }
    // Recurse into sub-functions
    Chunk* chunk = func->chunk();
    for (size_t i = 0; i < chunk->constants().size(); i++) {
        const Value& c = chunk->constants()[i];
        if (c.isFunction()) {
            FunctionObject* sub = chunk->getFunction(c.asFunctionIndex());
            if (sub) translateFunction(sub, disassemble, depth + 1);
        }
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <file.lua> [--disassemble]\n";
        return 1;
    }
    bool disassemble = (argc > 2 && std::string(argv[2]) == "--disassemble");

    std::ifstream f(argv[1]);
    if (!f) { std::cerr << "Cannot open " << argv[1] << "\n"; return 1; }
    std::stringstream ss;
    ss << f.rdbuf();

    try {
        Lexer lexer(ss.str());
        Parser parser(lexer);
        auto program = parser.parse();
        if (!program) { std::cerr << "Parse failed\n"; return 1; }

        CodeGenerator codegen;
        auto func = codegen.generate(program.get(), argv[1]);
        if (!func) { std::cerr << "Codegen failed\n"; return 1; }

        translateFunction(func.get(), disassemble);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    std::cout << "\n" << successes << " translated, " << failures << " failed\n";
    return failures > 0 ? 1 : 0;
}
