#ifndef LUA_JIT_HPP
#define LUA_JIT_HPP

#include "vm/vm.hpp"
#include "value/function.hpp"

#ifdef USE_JIT
#include <asmjit/core.h>
#include <asmjit/a64.h>
#include <asmjit/x86.h>

class JITCompiler {
public:
    JITCompiler(VM* vm);
    ~JITCompiler();

    // Compile a function to native code for current host architecture
    JITFunc compile(FunctionObject* function);

    // Compile ARM64 code
    JITFunc compileA64(FunctionObject* function);

    // Compile x86_64 code
    JITFunc compileX64(FunctionObject* function);

    // Emit ARM64 machine code into a CodeHolder
    bool assembleA64(FunctionObject* function, asmjit::CodeHolder& code);

    // Emit x86_64 machine code into a CodeHolder
    bool assembleX64(FunctionObject* function, asmjit::CodeHolder& code);

private:
    VM* vm_;
    static asmjit::JitRuntime rt_;
};

#else

class JITCompiler {
public:
    JITCompiler(VM* /*vm*/) {}
    JITFunc compile(FunctionObject*) { return nullptr; }
};

#endif

#endif // LUA_JIT_HPP
