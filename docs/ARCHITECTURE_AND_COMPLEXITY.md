# Lua VM: Architecture, State & Complexity Deep Dive

## 1. Executive Summary

This codebase is a modern, standalone, standards-compliant implementation of the **Lua programming language** written in **C++17**, fully supporting the language specifications and standard libraries from **Lua 5.1 through Lua 5.5**.

Rather than being a simple bytecode interpreter, the project incorporates advanced systems programming techniques and production-grade developer tooling:
- **64-bit NaN-Boxed Value Representation** (`sizeof(Value) == 8` with zero heap allocation for primitive types).
- **Dual-Mode Garbage Collector** (Tri-color incremental mark-and-sweep + generational collector with forward/backward write barriers).
- **Dual-Architecture Native Template JIT Compiler** (Native machine code generation for **ARM64** and **x86_64** architectures using AsmJit).
- **Dual Embedding Interfaces** (Binary-compatible PUC-Rio C API + Modern C++17/20 header-only RAII interface).
- **Integrated Language Server Protocol (LSP)** (`lua-lsp` & `lua --lsp`) providing IDE intelligence over JSON-RPC 2.0 without external dependencies.
- **100% Specification & Test Suite Compliance** (208 core unit tests, official Lua 5.5 test suites, C/C++ API tests, JIT execution tests, `luac` tests, and LSP tests).

---

## 2. Codebase Scale & Subsystem Breakdown

The repository contains approximately **~35,700 lines of modern C++ code** (excluding third-party libraries such as AsmJit and linenoise):

| Subsystem | Primary Source Files | Approximate LOC | Architectural Role |
| :--- | :--- | :--- | :--- |
| **Compiler & AST** | `lexer.cpp`, `lexer_string.cpp`, `parser.cpp`, `parser_statement.cpp`, `codegen.cpp`, `chunk.cpp`, `ast.hpp` | **~6,200** | Lexical analysis, recursive descent parsing, AST construction, scope analysis, jump fixup resolution, and bytecode generation. |
| **VM Core & Runtime** | `vm.cpp`, `run_impl.cpp`, `vm.hpp`, `opcode.hpp` | **~8,000** | Bytecode execution loop, coroutine lifecycle, multi-stack unwinding, call frame management, tail call optimization (TCO), `<close>` variable escalation, and error handling (`pcall`/`xpcall`). |
| **Standard Library** | `stdlib_*.cpp` (10 modules: base, string, table, math, io, os, utf8, coroutine, debug, socket) | **~8,200** | Complete standard libraries matching Lua 5.1–5.5 specifications, including full pattern matching engine, binary packing (`string.pack`), and built-in TCP socket networking. |
| **Template JIT Engine** | `jit.cpp` (ARM64), `jit_x64.cpp` (x86_64), `jit.hpp` | **~2,700** | Native template JIT compiler using AsmJit, hot loop/call counter profiling, stack frame synchronization, SSE2/NEON float math, and ABI calling convention bridges. |
| **Embedding Layer** | `lua_api.cpp` (C API), `include/lua/lua.hpp` (Modern C++ API) | **~2,450** | Binary-compatible PUC-Rio C API (`lua_State*`) and modern header-only C++17/20 RAII interface with automatic stack marshaling and lambda bindings. |
| **Developer Tooling & LSP** | `src/lsp/*` (`analysis.cpp`, `server.cpp`, `json.hpp`), `luac.cpp`, `main.cpp` | **~3,300** | Standalone bytecode compiler/disassembler (`luac`), interactive REPL with linenoise line editing, and native Language Server Protocol server (`lua-lsp`). |
| **Value & Object Model** | `value.hpp`, `table.cpp`, `closure.hpp`, `upvalue.hpp`, `userdata.hpp`, `coroutine.hpp` | **~1,500** | IEEE 754 NaN-boxing implementation, hybrid array/hash tables, lexical upvalues (open/closed stack migration), and ephemeron support. |
| **Garbage Collector** | `gc_impl.cpp`, `gc.hpp` | **~850** | Tri-color incremental mark-and-sweep, generational minor/major collection, write barriers, and emergency GC invocation on memory exhaustion. |
| **Test Suites & Fixtures** | `tests/*.cpp`, `tests/*.py`, `tests/*.sh`, 208 Lua scripts | **~2,500+** | 208 unit tests, C API tests, C++ API tests, cross-architecture JIT tests, `luac` tests, LSP JSON-RPC tests, and fixture modules (`lfs`, `cjson`). |

---

## 3. Subsystem Deep Dive & Technical Complexity

### 3.1 64-Bit NaN-Boxed Value Model (`sizeof(Value) == 8`)

Standard PUC-Rio Lua uses a 16-byte tagged union (`TValue` containing an 8-byte payload plus an integer type tag). In contrast, this implementation employs **64-bit NaN-boxing**, encoding all runtime values directly into the IEEE 754 Quiet NaN payload:

```
Double Float:
[ 0 / 1 ][ 11-bit Exponent != 0x7FF ][ 52-bit Mantissa Fraction ]

NaN-Boxed Object / Primitive:
[   1   ][ 11-bit Exponent == 0x7FF ][ 1 ][ 3-bit Tag ][ 48-bit Payload ]
```

- **Base Tag Mask**: Quiet NaN with sign bit set (`0xFFFF000000000000ULL`).
- **Primitive Values**: Tagged immediates for `NIL` (`0xFFF1`), `BOOLEAN` (`0xFFF2`), and native function indexes (`0xFFF8`).
- **Inline Signed Integers**: Stored with tag `0xFFF3` and a 48-bit signed integer payload directly inside the 64-bit value, avoiding heap allocations for integers. Full 64-bit integers exceeding 48 bits are represented via heap-allocated `Int64Object` (`0xFFFF`).
- **Heap Object Pointers**: Tables (`0xFFF5`), closures (`0xFFF6`), coroutines (`0xFFFA`), userdata (`0xFFF9`), files (`0xFFFD`), and sockets (`0xFFFE`) embed their 48-bit virtual address pointers directly into the payload.
- **Cache Locality**: 8-byte values halve stack memory consumption and double cache line efficiency during bytecode interpretation and JIT execution.

### 3.2 Compiler & AST Pipeline

The compiler is structured into three clear stages:
1. **Lexer (`src/compiler/lexer.cpp`)**: Tokenizes source into Lua tokens, tracking line numbers and column offsets for diagnostics. Handles long bracket literals (`[===[ ... ]===]`), hex floats, and escape sequences.
2. **Parser (`src/compiler/parser.cpp`, `src/compiler/ast.hpp`)**: Constructs a clean Abstract Syntax Tree (AST) using recursive descent with precedence climbing for binary expressions. Validates Lua 5.5 grammar including `global` declarations, `<const>` / `<close>` attributes, and named varargs (`function f(a, ...t)`).
3. **Code Generator (`src/compiler/codegen.cpp`)**: Translates AST nodes into register/stack bytecode instructions. Manages lexical scope tables, handles local variable allocation, tracks open upvalues, resolves forward/backward jump offsets, and generates metadata for debugging and disassembly.

### 3.3 Stack-Based Virtual Machine & Coroutine Runtime

The execution engine (`src/vm/run_impl.cpp`) is a stack-based virtual machine operating over ~70 specialized opcodes:
- **Stack Representation**: Each coroutine owns a contiguous `std::vector<Value> stack` with dynamic capacity validation (`jitEnsureStack`) guaranteeing safe headroom for instructions.
- **Call Frames (`CallFrame`)**: Manages the instruction pointer (`ip`), frame stack base, function closure pointer, and return value expectations (`retCount`).
- **Lexical Scoping & Upvalues**:
  - Open upvalues point directly to active stack slots.
  - When a stack frame exits or `OP_CLOSE` executes, upvalues migrate dynamically to the heap (`UpvalueObject::close()`).
- **Tail Call Optimization (TCO)**: `OP_TAILCALL` and `OP_TAILCALL_MULTI` reuse the active stack frame without pushing a new `CallFrame`, enabling unbound tail recursion in constant stack space.
- **To-Be-Closed Variables (`<close>`)**: Implements Lua 5.4+ deterministic resource management. When leaving a block or handling runtime errors, `__close` metamethods are invoked with error escalation semantics.

### 3.4 Dual-Mode Garbage Collection Engine

The memory manager (`src/vm/gc_impl.cpp`, `src/vm/gc.hpp`) provides two operational GC modes:

```
[ Young Generation / Allocations ] ---> [ Generational Minor GC ]
                                                 | (Promote / Age)
                                                 v
                                    [ Old Generation / Mark-Sweep ]
                                                 |
                                    [ Tri-Color Incremental Slices ]
```

1. **Tri-Color Incremental Collector**:
   - Objects are categorized as `WHITE` (unvisited), `GRAY` (visited, children unvisited), or `BLACK` (visited, children visited).
   - Execution is divided into bounded step slices proportional to allocation volume, eliminating stop-the-world pauses in real-time workloads.
2. **Generational Collector**:
   - Separates young objects from old objects (`age_ >= 1`). Minor GC cycles sweep only young objects, minimizing tracing overhead for short-lived temporary values.
3. **Write Barriers (`luaC_barrier`)**:
   - Backward and forward barriers ensure that whenever an old `BLACK` object receives a reference to a young `WHITE` object, invariants are preserved by graying the container or moving the young object to the remembered set.
4. **Ephemerons & Weak Tables**:
   - Weak keys (`__mode = "k"`), weak values (`__mode = "v"`), and weak pairs correctly collect cyclic structures.
5. **Emergency GC**:
   - Triggered automatically when an allocation throws `std::bad_alloc`, performing an immediate full collection cycle before failing.

### 3.5 Dual-Architecture Native Template JIT (ARM64 & x86_64)

The JIT compiler utilizes [AsmJit](https://github.com/asmjit/asmjit) to emit native machine code directly into executable pages at runtime:

```
                                [ Hotness Counter ]
                                (OP_LOOP / OP_CALL)
                                         |
                                         v
                         [ Host Architecture Detection ]
                                /                \
                   (AArch64 / ARM64)          (x86_64)
                          v                      v
                  [ compileA64 ]          [ compileX64 ]
                  (src/vm/jit.cpp)      (src/vm/jit_x64.cpp)
```

- **Hotness Detection**: Function objects track execution frequency. When loop iterations or call counts exceed predefined thresholds, the JIT compiles the bytecode chunk into native machine code.
- **Register Allocations**:
  - **ARM64**: Callee-saved `x19` (VM*), `x20` (Coroutine*), `x21` (stack.data()), `x22` (CallFrame*), `x23` (local_base), `x24` (top_reg).
  - **x86_64**: Callee-saved `rbx` (VM*), `r12` (Coroutine*), `r13` (stack.data()), `r14` (local_base), `r15` (top_reg), `rbp` (CallFrame*).
- **ABI Portability**:
  - Full support for **System V AMD64** (macOS, Linux: `rdi, rsi, rdx, rcx, r8, r9`) and **Windows x64** (`rcx, rdx, r8, r9` with 32-byte shadow stack space).
  - Strict 16-byte stack frame alignment maintained throughout prologue and epilogue.
- **Opcode Fast Paths**:
  - Signed 48-bit integer arithmetic with bitmask tag extraction.
  - Scalar double SSE2 / NEON floating-point math (`addsd`, `subsd`, `mulsd`, `divsd`).
  - IEEE 754 compliant float comparisons using `ucomisd` with parity flag check (`jp`) to handle NaN comparisons correctly.
  - Direct array table indexing bypassing hash lookups for sequential integer keys.
  - Specialized numeric for-loops (`OP_FORPREP`, `OP_FORLOOP`).
- **Interpreter Fallback**:
  - The JIT emits bailout transitions. If an unhandled opcode, error, or frame modification occurs, the JIT synchronizes register state back into the coroutine stack and returns the target bytecode offset, resuming execution seamlessly in the interpreter.

### 3.6 Dual Embedding Interfaces

1. **Classic PUC-Rio C API (`src/api/lua_api.cpp`, `src/api/lua.h`)**:
   - Implements standard Lua 5.4/5.5 C API functions (`lua_pcall`, `lua_gettable`, `lua_pushcclosure`, `luaL_register`, etc.).
   - Verified binary compatibility with third-party C modules including `luafilesystem` (`lfs`) and `lua-cjson`.
2. **Modern C++17/20 Header-Only API (`include/lua/lua.hpp`)**:
   - **RAII Lifecycle**: `lua::Context` encapsulates the VM with clean RAII cleanup.
   - **Proxy Table Syntax**: Natural table access via overloaded operators:
     ```cpp
     lua::Context ctx;
     ctx["config"]["timeout"] = 5000;
     int timeout = ctx["config"]["timeout"];
     ```
   - **Automatic Type Marshaling**: Automatic stack conversion for `std::string`, `std::vector<T>`, `std::map<K, V>`, `std::optional<T>`, and `std::tuple<Args...>`.
   - **Type-Safe Function Binding**: Bind C++ lambdas, free functions, and class member functions directly without manual stack manipulation.

### 3.7 Native Language Server Protocol (LSP) Engine

The `src/lsp/` subsystem implements a lightweight Language Server Protocol engine over JSON-RPC 2.0:
- Invoked via standalone `lua-lsp` binary or `lua --lsp` flag.
- Parses Lua files on open/change into AST representation.
- Provides IDE features for editors (VS Code, Neovim, Helix):
  - Syntax error diagnostics with exact line/column ranges.
  - Document symbols (`textDocument/documentSymbol`).
  - Hover documentation (`textDocument/hover`).
  - Jump-to-definition (`textDocument/definition`).
  - Context-aware autocompletion (`textDocument/completion`).

---

## 4. Algorithmic Complexity Matrix

| Operation | Time Complexity | Space Complexity | Description & Cache Characteristics |
| :--- | :--- | :--- | :--- |
| **Bytecode Instruction Dispatch** | $\mathcal{O}(1)$ | $\mathcal{O}(1)$ | Switch-based dispatch loop in C++; direct jump in JIT machine code. |
| **Local Variable Access** | $\mathcal{O}(1)$ | $\mathcal{O}(1)$ | Direct pointer indexing relative to frame `local_base`. |
| **Table Sequential Index (`t[i]`)** | $\mathcal{O}(1)$ | $\mathcal{O}(1)$ | Direct index into contiguous `std::vector<Value> array_`. |
| **Table Hash Index (`t[k]`)** | $\mathcal{O}(1)$ avg | $\mathcal{O}(1)$ | Hash lookup in `std::unordered_map` with 64-bit value hashing. |
| **Function Call & TCO** | $\mathcal{O}(1)$ | $\mathcal{O}(1)$ | Frame activation. Tail calls reuse active frame without stack allocation. |
| **Lexical Upvalue Access** | $\mathcal{O}(1)$ | $\mathcal{O}(1)$ | Pointer dereference through `UpvalueObject*`. |
| **Generational Minor GC** | $\mathcal{O}(\text{young})$ | $\mathcal{O}(1)$ | Traces only young generation objects and remembered set roots. |
| **Incremental GC Step** | $\mathcal{O}(K)$ | $\mathcal{O}(1)$ | Slices mark/sweep work into small bounded units proportional to allocation request. |
| **JIT Compilation** | $\mathcal{O}(N)$ instructions | $\mathcal{O}(N)$ bytes | Single-pass linear template emission into native executable memory. |

---

## 5. Architectural Comparison

| Attribute | Standard PUC-Rio Lua 5.4/5.5 | LuaJIT 2.1 | **This Lua VM** |
| :--- | :--- | :--- | :--- |
| **Language Specification** | Lua 5.4 / 5.5 | Lua 5.1 + partial 5.2 | **Full Lua 5.1 – 5.5** |
| **Implementation Language** | ANSI C89 | ANSI C89 + DynASM | **Modern C++17** |
| **Value Size** | 16 bytes (`TValue` union) | 8 bytes (NaN-boxing) | **8 bytes (NaN-boxing)** |
| **Execution Architecture** | Register-based Interpreter | Fast Interpreter + Tracing JIT | **Stack Interpreter + Dual Template JIT** |
| **JIT Target Architectures** | None | x86, x64, ARM, ARM64, MIPS, PPC | **ARM64 & x86_64** |
| **Garbage Collector** | Generational & Incremental | Bi-color Incremental | **Tri-color Incremental & Generational** |
| **Embedding Interface** | C Stack API | C Stack API + FFI | **C Stack API + Modern C++17/20 RAII API** |
| **To-Be-Closed Variables (`<close>`)** | Yes (5.4+) | No | **Yes** |
| **Lua 5.5 Features** | Yes | No | **Yes** (`global`, named varargs, `table.create`) |
| **Developer Tooling** | `lua`, `luac` | `luajit` | `lua`, `luac`, **`lua-lsp` (Language Server)** |

---

## 6. Verification and Test Infrastructure

The codebase incorporates a comprehensive test matrix executed via CMake (`ninja test` or `cmake --build build --target test`):
- **Core Unit Test Suite (`tests/run_all_tests.sh`)**: **208 / 208** Lua test scripts verifying language syntax, control flow, tables, coroutines, metatables, GC edge cases, and standard library completeness.
- **C API Test Suite (`tests/test_c_api.cpp`)**: Verifies stack manipulation, closures, table traversal, error handling, and external dynamic modules.
- **Modern C++ Embedding Test Suite (`tests/test_cpp_api.cpp`)**: Tests RAII context lifecycle, expression evaluation, container conversions, lambda bindings, and proxy syntax.
- **Cross-Architecture JIT Test Suite (`tests/test_jit_x64.cpp`)**: Validates machine code emission for ARM64 and x86_64, verifying prologue byte patterns, loop structures, and execution parity.
- **Standalone `luac` Compiler Test Suite (`tests/test_luac.sh`)**: Verifies disassembly (`-l`), parse-only (`-p`), stripping (`-s`), multi-file combination, and versioning (`-v`).
- **Language Server Protocol Test Suite (`tests/test_lsp.py` / `tests/test_lsp.sh`)**: Verifies CLI flags and JSON-RPC 2.0 communication for diagnostics, symbols, hover, definitions, and autocompletion.
