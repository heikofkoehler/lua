#include "vm/jit.hpp"
#include <iostream>

#ifdef USE_JIT

JITCompiler::JITCompiler(VM* vm) : vm_(vm) {}
JITCompiler::~JITCompiler() {}

JITFunc JITCompiler::compile(FunctionObject* function) {
    if (function->getJITCode()) return function->getJITCode();

    (void)vm_; // Suppress unused warning for now

    using namespace asmjit;

    CodeHolder code;
    code.init(rt_.environment());
    a64::Assembler a(&code);

    // For now, let's just emit a function that returns true
    // bool (*JITFunc)(VM* vm)
    
    // on ARM64:
    // arg0 (vm) is in X0
    // return value is in W0 (or X0)

    a.mov(a64::w0, 1);
    a.ret(a64::x30); // x30 is the link register (LR)

    JITFunc fn;
    Error err = rt_.add(&fn, &code);
    if (err != kErrorOk) {
        return nullptr;
    }

    function->setJITCode(fn);
    return fn;
}

#endif
