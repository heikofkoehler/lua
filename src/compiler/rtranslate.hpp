#ifndef LUA_RTRANSLATE_HPP
#define LUA_RTRANSLATE_HPP

// Stack-to-register bytecode translator (Phase 2 of register-vm migration).
//
// Translates the existing stack-based bytecode (Chunk) into register-based
// instructions (RInstruction). The translation is a direct rename:
//   stack slot N  ->  register R(N)
// because the stack depth at every instruction is statically known.
//
// This inherits correctness from the stack codegen: we don't reinterpret
// Lua semantics, we just rename stack positions to registers.
//
// Usage:
//   RTranslateResult r = translateToRegister(func);
//   if (!r.ok) { /* r.error */ }
//   ropDisassembleCode(r.code, func->name());

#include "vm/rinstruction.hpp"
#include <string>
#include <vector>

class FunctionObject;

struct RTranslateResult {
    std::vector<RInstruction> code;
    int maxRegisters = 0;  // Frame size needed (max R index + 1)
    bool ok = false;
    std::string error;
};

RTranslateResult translateToRegister(FunctionObject* func);

#endif // LUA_RTRANSLATE_HPP
