#ifndef LUA_JIT_HPP
#define LUA_JIT_HPP

#include "vm/vm.hpp"
#include "value/function.hpp"

#ifdef USE_JIT
#include <asmjit/core.h>
#include <asmjit/a64.h>

class JITCompiler {
public:
    JITCompiler(VM* vm);
    ~JITCompiler();

    // Compile a function to native code
    // Returns function pointer on success, nullptr on failure
    JITFunc compile(FunctionObject* function);

private:
    VM* vm_;
    asmjit::JitRuntime rt_;
};

#else

class JITCompiler {
public:
    JITCompiler(VM* vm) : vm_(vm) {}
    JITFunc compile(FunctionObject*) { return nullptr; }
private:
    VM* vm_;
};

#endif

#endif // LUA_JIT_HPP
