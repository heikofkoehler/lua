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
    
    const int SP_DYNAMIC = -1;
    
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
        switch (op) {
            case OpCode::OP_CONSTANT:
            case OpCode::OP_GET_LOCAL:
            case OpCode::OP_SET_LOCAL:
            case OpCode::OP_GET_UPVALUE:
            case OpCode::OP_SET_UPVALUE:
            case OpCode::OP_CLOSE:
            case OpCode::OP_ROTATE:
            case OpCode::OP_TAILCALL:
            case OpCode::OP_TAILCALL_MULTI:
            case OpCode::OP_RETURN_VALUE:
            case OpCode::OP_RETURN_VALUE_MULTI:
            case OpCode::OP_GET_VARARG:
                return 2;
            case OpCode::OP_GET_TABUP:
            case OpCode::OP_SET_TABUP:
            case OpCode::OP_TBC:
            case OpCode::OP_JUMP:
            case OpCode::OP_JUMP_IF_FALSE:
            case OpCode::OP_LOOP:
            case OpCode::OP_CALL:
            case OpCode::OP_CALL_MULTI:
            case OpCode::OP_DEF_GLOBAL:
            case OpCode::OP_YIELD_MULTI:
                return 3;
            case OpCode::OP_FORPREP:
            case OpCode::OP_FORLOOP:
                return 4;
            case OpCode::OP_GET_TABUP_LONG:
            case OpCode::OP_SET_TABUP_LONG:
            case OpCode::OP_DEF_GLOBAL_LONG:
                return 5;
            case OpCode::OP_CONSTANT_LONG:
                return 4;
            // OP_POP, OP_DUP, OP_SWAP, OP_NIL, OP_TRUE, OP_FALSE,
            // arithmetic, comparison, etc. are 1 byte (default)
            default:
                return 1;
        }
    };

    auto propagate = [&](size_t target, int targetSp) {
        auto it = sp.find(target);
        if (it == sp.end()) {
            sp[target] = targetSp;
            worklist.push_back(target);
        } else if (it->second != targetSp && targetSp != SP_DYNAMIC && it->second != SP_DYNAMIC) {
            error = "inconsistent stack depth at offset " + std::to_string(target) +
                    " (existing=" + std::to_string(it->second) +
                    ", new=" + std::to_string(targetSp) + ")";
        }
    };

    int arity = func->arity();
    sp[0] = arity;
    worklist.push_back(0);

    while (!worklist.empty() && error.empty()) {
        size_t off = worklist.back();
        worklist.pop_back();
        int curSp = sp[off];
        OpCode op = opcodeAt(off);
        size_t len = instrLen(op, off);
        
        int afterSp = curSp;
        bool hasFallthrough = true;
        std::vector<size_t> targets;

        // Compute stack effect based on VM's push/pop behavior
        switch (op) {
            // Push 1
            case OpCode::OP_CONSTANT:
            case OpCode::OP_CONSTANT_LONG:
            case OpCode::OP_NIL:
            case OpCode::OP_TRUE:
            case OpCode::OP_FALSE:
            case OpCode::OP_GET_LOCAL:
            case OpCode::OP_GET_UPVALUE:
            case OpCode::OP_GET_TABUP:
            case OpCode::OP_GET_TABUP_LONG:
            case OpCode::OP_NEW_TABLE:
            case OpCode::OP_CLOSURE:
            case OpCode::OP_CLOSURE_LONG:
            case OpCode::OP_GET_VARARG_COUNT:
            case OpCode::OP_PACK_VARARG_TABLE:
                afterSp = curSp + 1;
                break;
            // Pop 1
            case OpCode::OP_SET_LOCAL:
            case OpCode::OP_SET_UPVALUE:
            case OpCode::OP_SET_TABUP:
            case OpCode::OP_SET_TABUP_LONG:
            case OpCode::OP_POP:
            case OpCode::OP_CLOSE_UPVALUE:
            case OpCode::OP_DEF_GLOBAL:
            case OpCode::OP_DEF_GLOBAL_LONG:
                afterSp = curSp - 1;
                break;
            // Pop 1, push 1 (net 0)
            case OpCode::OP_NEG:
            case OpCode::OP_NOT:
            case OpCode::OP_BNOT:
            case OpCode::OP_LEN:
            case OpCode::OP_GET_VARARG_ITEM:
                afterSp = curSp; // net 0
                break;
            // Pop 2, push 1 (net -1)
            case OpCode::OP_ADD:
            case OpCode::OP_SUB:
            case OpCode::OP_MUL:
            case OpCode::OP_DIV:
            case OpCode::OP_IDIV:
            case OpCode::OP_MOD:
            case OpCode::OP_POW:
            case OpCode::OP_BAND:
            case OpCode::OP_BOR:
            case OpCode::OP_BXOR:
            case OpCode::OP_SHL:
            case OpCode::OP_SHR:
            case OpCode::OP_CONCAT:
            case OpCode::OP_EQUAL:
            case OpCode::OP_LESS:
            case OpCode::OP_LESS_EQUAL:
            case OpCode::OP_GREATER:
            case OpCode::OP_GREATER_EQUAL:
            case OpCode::OP_GET_TABLE:
                afterSp = curSp - 1;
                break;
            // Pop 3 (net -3)
            case OpCode::OP_SET_TABLE:
                afterSp = curSp - 3;
                break;
            // Push 1 (DUP)
            case OpCode::OP_DUP:
                afterSp = curSp + 1;
                break;
            // No change
            case OpCode::OP_SWAP:
            case OpCode::OP_TBC:
            case OpCode::OP_ROTATE:
                afterSp = curSp;
                break;
            // CLOSE: resize to slot (only shrinks)
            case OpCode::OP_CLOSE: {
                uint8_t slot = byteAt(off + 1);
                afterSp = (curSp > (int)slot) ? slot : curSp;
                break;
            }
            // Jumps
            case OpCode::OP_JUMP: {
                uint16_t offset = u16At(off + 1);
                targets.push_back(off + 3 + offset);
                hasFallthrough = false;
                break;
            }
            case OpCode::OP_JUMP_IF_FALSE: {
                uint16_t offset = u16At(off + 1);
                targets.push_back(off + 3 + offset);
                // Does NOT pop
                afterSp = curSp;
                break;
            }
            case OpCode::OP_LOOP: {
                uint16_t offset = u16At(off + 1);
                targets.push_back(off + 3 - offset);
                hasFallthrough = false;
                break;
            }
            // Calls
            case OpCode::OP_CALL: {
                uint8_t argc = byteAt(off + 1);
                uint8_t retcEnc = byteAt(off + 2);
                if (retcEnc == 0) {
                    afterSp = SP_DYNAMIC;
                } else {
                    int retc = (int)retcEnc - 1;
                    afterSp = curSp - argc - 1 + retc;
                }
                break;
            }
            case OpCode::OP_TAILCALL:
            case OpCode::OP_RETURN:
                hasFallthrough = false;
                break;
            case OpCode::OP_RETURN_VALUE:
            case OpCode::OP_RETURN_VALUE_MULTI:
                hasFallthrough = false;
                break;
            // Dynamic
            case OpCode::OP_CALL_MULTI:
            case OpCode::OP_TAILCALL_MULTI:
            case OpCode::OP_SET_TABLE_MULTI:
            case OpCode::OP_YIELD_MULTI:
                if (op == OpCode::OP_CALL_MULTI || op == OpCode::OP_YIELD_MULTI) {
                    afterSp = SP_DYNAMIC;
                } else if (op == OpCode::OP_SET_TABLE_MULTI) {
                    afterSp = SP_DYNAMIC;
                } else {
                    hasFallthrough = false;
                }
                break;
            case OpCode::OP_GET_VARARG: {
                uint8_t retcEnc = byteAt(off + 1);
                if (retcEnc == 0) {
                    afterSp = SP_DYNAMIC;
                } else {
                    afterSp = curSp + (retcEnc - 1);
                }
                break;
            }
            // FORPREP pushes 2 (or 1)
            case OpCode::OP_FORPREP: {
                uint16_t offset = u16At(off + 2);
                size_t target = off + 4 + offset;
                targets.push_back(target);
                bool stepDefault = (byteAt(off + 1) & 0x80) != 0;
                afterSp = curSp + (stepDefault ? 2 : 1);
                break;
            }
            case OpCode::OP_FORLOOP: {
                uint16_t offset = u16At(off + 2);
                size_t target = off + 4 - offset;
                targets.push_back(target);
                break;
            }
            // IO_CLOSE: pop 1
            case OpCode::OP_IO_CLOSE:
                afterSp = curSp - 1;
                break;
            // DEF_GLOBAL_TABLE: pop 3
            case OpCode::OP_DEF_GLOBAL_TABLE:
                afterSp = curSp - 3;
                break;
            default:
                // For unhandled opcodes, assume no stack change and continue
                // (better than failing, allows us to find the real bugs)
                afterSp = curSp;
                break;
        }

        if (hasFallthrough) {
            propagate(off + len, afterSp);
            if (!error.empty()) return {};
        }
        for (size_t t : targets) {
            propagate(t, afterSp);
            if (!error.empty()) return {};
        }
    }

    return sp;
}

void testFunction(FunctionObject* func, int depth = 0) {
    std::string indent(depth * 2, ' ');
    std::string error;
    std::string refError;
    
    auto translatorSp = rtranslateComputeSp(func, error);
    auto refSp = referenceComputeSp(func, refError);
    
    if (!error.empty()) {
        std::cout << indent << "TRANSLATOR FAIL " << func->name() << ": " << error << "\n";
        // Dump partial SP map if available
        if (!translatorSp.empty()) {
            std::cout << indent << "  Partial map has " << translatorSp.size() << " entries\n";
            // Check specific offsets
            for (size_t off : {8085, 8143}) {
                auto it = translatorSp.find(off);
                if (it != translatorSp.end()) {
                    std::cout << indent << "  sp[" << off << "] = " << it->second << "\n";
                }
            }
        }
    }
    if (!refError.empty()) {
        std::cout << indent << "REF FAIL " << func->name() << ": " << refError << "\n";
    }
    if (!error.empty() || !refError.empty()) {
        return;
    }
    
    // Compare
    bool match = true;
    for (auto& kv : translatorSp) {
        auto it = refSp.find(kv.first);
        if (it == refSp.end()) {
            std::cout << indent << "DIFF at offset " << kv.first
                      << ": translator=" << kv.second << ", ref=MISSING\n";
            match = false;
        } else if (it->second != kv.second) {
            std::cout << indent << "DIFF at offset " << kv.first
                      << ": translator=" << kv.second
                      << ", ref=" << it->second << "\n";
            match = false;
        }
    }
    for (auto& kv : refSp) {
        if (translatorSp.find(kv.first) == translatorSp.end()) {
            std::cout << indent << "DIFF at offset " << kv.first
                      << ": translator=MISSING, ref=" << kv.second << "\n";
            match = false;
        }
    }
    if (match) {
        std::cout << indent << "OK " << func->name() << " (" << translatorSp.size() << " offsets match)\n";
    } else {
        std::cout << indent << "MISMATCH " << func->name() << "\n";
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
