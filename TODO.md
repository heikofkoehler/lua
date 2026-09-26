# Lua VM TODO: Missing Features & Modernization

This document tracks the features, specifications, and modernization improvements of this custom Lua VM against the Lua 5.1-5.5 specifications.

## 1. Language Syntax & Control Flow
- [x] **Tail Call Optimization (TCO):** Implement `OP_TAILCALL` to prevent stack overflow in deep recursion.
- [x] **Goto and Labels:** Add support for `goto` and `::label::` (Lua 5.2+).
- [x] **Bitwise Operators:** Add tokens and opcodes for `&`, `|`, `~`, `<<`, `>>`, and `//` (integer division) (Lua 5.3+).
- [x] **Generic `for` Loop:** Ensure full support for the `for var in iter, state, var do` iterator protocol.
- [x] **Lua 5.5 Global Declarations:** `global *`, `global none`, `global <const> *`, and initialized global bindings (`global a = 1`).
- [x] **Lua 5.5 Named Varargs:** Syntax and prologue handling for `function f(a, ...t)` creating a table with `.n`.

## 2. Metamethods & Data Types
- [x] **Metamethod Completeness:** Add support for `__call`, `__concat`, `__len`, and bitwise metamethods.
- [x] **Weak Tables:** Implement `__mode = "k"` and `__mode = "v"` in the Garbage Collector, including ephemerons.
- [x] **Integer Type:** Distinguish between 64-bit integers and doubles in the `Value` NaN-boxing (Lua 5.3+).
- [x] **Userdata:** Implement a general `userdata` type for easier C++ extension development.

## 3. Standard Library Completeness
- [x] **Pattern Matching:** Replace C++ string methods with Lua-style pattern matching (`%d`, `%a`, `(.-)`, etc.) in `string` library.
- [x] **C Modules:** Enable `package.loadlib` to load shared libraries (`.so` / `.dll`).
- [x] **Debug Library:** Implement `debug.getlocal`, `debug.setlocal`, and execution hooks.
- [x] **Environments:** Implement `_ENV` (Lua 5.2+) or `setfenv`/`getfenv` (Lua 5.1).
- [x] **Lua 5.5 Standard Library:** Implement `table.create(nseq, nrec)`, full binary `string.pack`/`unpack`/`packsize`, dual-return `utf8.offset` and lax mode, `collectgarbage("param")`.

## 4. VM & Garbage Collection
- [x] **Emergency GC:** Trigger garbage collection automatically when an allocation fails.
- [x] **Upvalue Sharing:** Verify and robustly implement upvalue sharing across coroutine boundaries.
- [x] **Incremental GC:** Upgrade the mark-and-sweep collector to an incremental or generational model to reduce pause times.

## 5. Tooling, APIs & Performance
- [x] **Standalone Bytecode Compiler (`luac`)**: Standard `luac` CLI tool supporting `-o`, `-l` (disassemble/list), `-s` (strip debug info), `-p` (parse-only), `-v` (version), and multi-file bytecode combination.
- [x] **JIT Compilation:** High-performance ARM64 Template JIT using AsmJit with hotspot detection, compiling arithmetic, bitwise, comparison, table access, closure, and call operations.
- [x] **Lua C API & Dynamic Library (`liblua`)**: Lua 5.4/5.5 compatible C API (`lua.h`, `lauxlib.h`, `lualib.h`, `luaconf.h`) and shared library build for dynamic module loading and Luarocks packages.
- [x] **Modern C++17/20 Embedding API**: Header-only C++ interface (`include/lua/lua.hpp`) with RAII `lua::Context`, automatic stack marshaling, STL container conversions, callable lambda bindings, and multi-return tuples.
- [x] **Lua 5.5 Specification Compliance**: 100% pass rate on official Lua 5.5.0 test suite (`all.lua`).
- [ ] **LSP Support:** Integrate with a Lua Language Server for better developer experience.
- [ ] **x86_64 JIT Backend:** Extend AsmJit code generation to x86_64 architecture.
