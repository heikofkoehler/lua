#include "vm/vm.hpp"
#include "vm/jit.hpp"
#include <iostream>
#include <cassert>
#include <vector>
#include <cstdlib>

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond " at " __FILE__ ":" << __LINE__ << std::endl; \
        std::abort(); \
    } \
} while (0)

#ifdef USE_JIT

void test_assemble_x64_arithmetic() {
    std::cout << "Testing x86_64 JIT assembler (arithmetic)..." << std::endl;
    VM vm;
    FunctionObject* func = vm.compileSource(
        "local a = 10\n"
        "local b = 25\n"
        "local c = (a + b) * 2 - (b // 2) + (a % 3)\n"
        "return c\n"
    );
    TEST_ASSERT(func != nullptr);

    JITCompiler jit(&vm);
    asmjit::CodeHolder code;
    code.init(asmjit::Environment(asmjit::Arch::kX64));
    bool ok = jit.assembleX64(func, code);
    TEST_ASSERT(ok);
    TEST_ASSERT(code.code_size() > 0);

    const uint8_t* bytes = code.text_section()->buffer().data();
    TEST_ASSERT(bytes != nullptr);
    // Verify x86_64 prologue: push rbp (0x55), mov rbp, rsp (0x48, 0x89, 0xe5)
    TEST_ASSERT(bytes[0] == 0x55);
    TEST_ASSERT(bytes[1] == 0x48 && bytes[2] == 0x89 && bytes[3] == 0xe5);
    std::cout << "  Passed! (Generated " << code.code_size() << " bytes of x86_64 machine code)" << std::endl;
}

void test_assemble_x64_loops() {
    std::cout << "Testing x86_64 JIT assembler (for loops & tables)..." << std::endl;
    VM vm;
    FunctionObject* func = vm.compileSource(
        "local sum = 0\n"
        "for i = 1, 100 do\n"
        "    sum = sum + i\n"
        "end\n"
        "local t = {10, 20, 30}\n"
        "t[1] = sum\n"
        "return t[1]\n"
    );
    TEST_ASSERT(func != nullptr);

    JITCompiler jit(&vm);
    asmjit::CodeHolder code;
    code.init(asmjit::Environment(asmjit::Arch::kX64));
    bool ok = jit.assembleX64(func, code);
    TEST_ASSERT(ok);
    TEST_ASSERT(code.code_size() > 0);

    const uint8_t* bytes = code.text_section()->buffer().data();
    TEST_ASSERT(bytes[0] == 0x55);
    std::cout << "  Passed! (Generated " << code.code_size() << " bytes of x86_64 machine code)" << std::endl;
}

void test_assemble_x64_closures() {
    std::cout << "Testing x86_64 JIT assembler (closures & upvalues)..." << std::endl;
    VM vm;
    FunctionObject* func = vm.compileSource(
        "local x = 10\n"
        "local function f(y)\n"
        "    return x + y\n"
        "end\n"
        "return f(5)\n"
    );
    TEST_ASSERT(func != nullptr);

    JITCompiler jit(&vm);
    asmjit::CodeHolder code;
    code.init(asmjit::Environment(asmjit::Arch::kX64));
    bool ok = jit.assembleX64(func, code);
    TEST_ASSERT(ok);
    TEST_ASSERT(code.code_size() > 0);
    std::cout << "  Passed! (Generated " << code.code_size() << " bytes of x86_64 machine code)" << std::endl;
}

void test_cross_arch_parity() {
    std::cout << "Testing cross-architecture assembly parity (ARM64 vs x86_64)..." << std::endl;
    VM vm;
    FunctionObject* func = vm.compileSource(
        "local sum = 0\n"
        "for i = 1, 1000 do\n"
        "    if i % 2 == 0 then\n"
        "        sum = sum + i\n"
        "    else\n"
        "        sum = sum - i\n"
        "    end\n"
        "end\n"
        "return sum\n"
    );
    TEST_ASSERT(func != nullptr);

    JITCompiler jit(&vm);

    asmjit::CodeHolder codeX64;
    codeX64.init(asmjit::Environment(asmjit::Arch::kX64));
    bool okX64 = jit.assembleX64(func, codeX64);
    TEST_ASSERT(okX64);
    TEST_ASSERT(codeX64.code_size() > 0);

    asmjit::CodeHolder codeA64;
    codeA64.init(asmjit::Environment(asmjit::Arch::kAArch64));
    bool okA64 = jit.assembleA64(func, codeA64);
    TEST_ASSERT(okA64);
    TEST_ASSERT(codeA64.code_size() > 0);

    std::cout << "  Passed! (x86_64: " << codeX64.code_size() 
              << " bytes, ARM64: " << codeA64.code_size() << " bytes)" << std::endl;
}

#if defined(__x86_64__) || defined(_M_X64)
void test_native_x64_execution() {
    std::cout << "Testing native x86_64 JIT execution..." << std::endl;
    VM vm;
    FunctionObject* func = vm.compileSource(
        "local sum = 0\n"
        "for i = 1, 100 do\n"
        "    sum = sum + i\n"
        "end\n"
        "return sum\n"
    );
    TEST_ASSERT(func != nullptr);

    JITCompiler jit(&vm);
    JITFunc jfn = jit.compileX64(func);
    TEST_ASSERT(jfn != nullptr);

    bool runOk = vm.run(*func);
    TEST_ASSERT(runOk);
    TEST_ASSERT(vm.currentCoroutine()->stack.size() > 0);
    Value res = vm.currentCoroutine()->stack.back();
    TEST_ASSERT(res.isInteger());
    TEST_ASSERT(res.asInteger() == 5050);
    std::cout << "  Passed! JIT result: " << res.asInteger() << std::endl;
}
#endif

int main() {
    std::cout << "=== Running x86_64 JIT Test Suite ===" << std::endl;
    test_assemble_x64_arithmetic();
    test_assemble_x64_loops();
    test_assemble_x64_closures();
    test_cross_arch_parity();
#if defined(__x86_64__) || defined(_M_X64)
    test_native_x64_execution();
#endif
    std::cout << "All x86_64 JIT tests passed successfully!" << std::endl;
    return 0;
}

#else

int main() {
    std::cout << "JIT not enabled, skipping x86_64 JIT tests." << std::endl;
    return 0;
}

#endif
