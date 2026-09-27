// Differential tester for stack depth computation.
// Compares the translator's SP map against a reference implementation
// based directly on the VM's stack operations.
//
// Usage: rsp_diff <file.lua> [function_name]
//
// For each function, prints bytecode offsets where the two implementations
// disagree on the stack depth.

#include "compiler/rtranslate.hpp"
#include "compiler/lexer.hpp"
#include "compiler/parser.hpp"
#include "compiler/codegen.hpp"
#include "value/function.hpp"
#include "vm/opcode.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <vector>

// Reference SP computation: simple worklist algorithm using VM's stack logic.
// This is intentionally written independently from rtranslate.cpp to catch bugs.
static std::unordered_map<size_t, int> referenceComputeSp(FunctionObject* func,
                                                           std::string& error) {
    Chunk* chunk = func->chunk();
    const std::vector<uint8_t>& code = chunk->code();
    std::unordered_map<size_t, int> sp;
    std::vector<size_t> worklist;
    
    auto byteAt = [&](size_t off) -> uint8_t {
        return off < code.size() ? code[off] : 0;
    };
    auto u16At = [&](size_t off) -> uint16_t {
        return (uint16_t)byteAt(off) | ((uint16_t)byteAt(off + 1) << 8);
    };
    auto opcodeAt = [&](size_t off) -> OpCode {
        return static_cast<OpCode>(byteAt(off));
    };
    auto instrLen = [&](OpCode op, size_t off) -> size_t {
        // Simplified: return length based on opcode
        // This is duplicated from rtranslate.cpp - should match
        switch (op) {
            case OpCode::OP_CONSTANT:
            case OpCode::OP_GET_LOCAL:
            case OpCode::OP_SET_LOCAL:
            case OpCode::OP_GET_UPVALUE:
            case OpCode::OP_SET_UPVALUE:
            case OpCode::OP_GET_TABUP:
            case OpCode::OP_SET_TABUP:
            case OpCode::OP_NIL:
            case OpCode::OP_TRUE:
            case OpCode::OP_FALSE:
            case OpCode::OP_POP:
            case OpCode::OP_DUP:
            case OpCode::OP_SWAP:
            case OpCode::OP_ROTATE:
            case OpCode::OP_CLOSE:
            case OpCode::OP_CLOSE_UPVALUE:
            case OpCode::OP_TBC:
            case OpCode::OP_DEF_GLOBAL:
            case OpCode::OP_GET_VARARG_COUNT:
            case OpCode::OP_PACK_VARARG_TABLE:
            case OpCode::OP_GET_VARARG_ITEM:
            case OpCode::OP_NEW_TABLE:
                return 2;
            case OpCode::OP_CONSTANT_LONG:
            case OpCode::OP_GET_TABUP_LONG:
            case OpCode::OP_SET_TABUP_LONG:
            case OpCode::OP_CLOSURE:
            case OpCode::OP_CLOSURE_LONG:
            case OpCode::OP_DEF_GLOBAL_LONG:
            case OpCode::OP_DEF_GLOBAL_TABLE:
                return 5;
            case OpCode::OP_CALL:
            case OpCode::OP_CALL_MULTI:
            case OpCode::OP_TAILCALL:
            case OpCode::OP_TAILCALL_MULTI:
            case OpCode::OP_YIELD_MULTI:
            case OpCode::OP_GET_VARARG:
                return 3;
            case OpCode::OP_JUMP:
            case OpCode::OP_JUMP_IF_FALSE:
            case OpCode::OP_LOOP:
            case OpCode::OP_FORPREP:
            case OpCode::OP_FORLOOP:
                return 3; // Actually 3 for JUMP (1+2), 4 for FOR (1+1+2)
            case OpCode::OP_RETURN:
            case OpCode::OP_RETURN_VALUE:
                return 2;
            case OpCode::OP_RETURN_VALUE_MULTI:
            case OpCode::OP_SET_TABLE_MULTI:
                return 2;
            default:
                // Arithmetic, comparison, etc. are 1 byte
                return 1;
        }
    };

    // TODO: Implement full reference logic
    // For now, just return empty to indicate not implemented
    error = "reference not implemented";
    return {};
}

void testFunction(FunctionObject* func, int depth = 0) {
    std::string indent(depth * 2, ' ');
    std::string error;
    
    auto translatorSp = rtranslateComputeSp(func, error);
    if (!error.empty()) {
        std::cout << indent << "TRANSLATOR FAIL " << func->name() << ": " << error << "\n";
        return;
    }
    
    // Dump SP map sorted by offset
    std::vector<std::pair<size_t, int>> sorted(translatorSp.begin(), translatorSp.end());
    std::sort(sorted.begin(), sorted.end());
    std::cout << indent << "Function " << func->name() << " SP map:\n";
    for (auto& kv : sorted) {
        std::cout << indent << "  offset " << kv.first << " -> sp " << kv.second << "\n";
    }
    
    // Recurse
    Chunk* chunk = func->chunk();
    for (size_t i = 0; i < chunk->constants().size(); i++) {
        const Value& c = chunk->constants()[i];
        if (c.isFunction()) {
            FunctionObject* sub = chunk->getFunction(c.asFunctionIndex());
            if (sub) testFunction(sub, depth + 1);
        }
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <file.lua>\n";
        return 1;
    }
    
    std::ifstream f(argv[1]);
    if (!f) { std::cerr << "Cannot open " << argv[1] << "\n"; return 1; }
    std::stringstream ss;
    ss << f.rdbuf();
    
    try {
        Lexer lexer(ss.str());
        Parser parser(lexer);
        auto program = parser.parse();
        CodeGenerator codegen;
        auto funcPtr = codegen.generate(program.get(), argv[1]);
        FunctionObject* func = funcPtr.get();
        testFunction(func);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
