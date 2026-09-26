# Lua VM Documentation Index

Welcome to the internal documentation for the Lua VM project. These documents provide design and implementation details useful for programmers working on the codebase.

## Core Architecture
- [VM Architecture and Execution Loop](VM_ARCHITECTURE.md): The execution stack, call frames, and the main dispatch loop.
- [Value Representation (NaN-Boxing)](VALUE_REPRESENTATION.md): How all Lua values are packed into 64-bit doubles.
- [Compiler Pipeline](COMPILER_PIPELINE.md): Lexing, parsing, AST, and bytecode generation.

## Systems
- [Garbage Collector Implementation](GC_IMPLEMENTATION.md): Details on the tri-color incremental and generational collector.
- [Standard Library Implementation](STDLIB_IMPLEMENTATION.md): Overview of implemented libraries and native function architecture.
- [C API & Modern C++ Embedding API](C_API.md): Guide to the Lua-compatible C API, shared library embedding, and the modern C++17/20 interface (`lua::Context`).
- [REPL Features and Implementation](REPL.md): Interactive REPL details including autocomplete and multi-line support.

## Feature Spotlights
- [Dot Notation and Method Calls](DOT_NOTATION.md): Implementation details for table access and `obj:method()` syntax.
- [Bytecode Disassembler](REPL.md#bytecode-disassembly): How to use the `-L` flag to inspect compiled code.
- [JIT Compilation Plan & Status](JIT_COMPILATION_PLAN.md): Current architecture and opcode implementation of the ARM64 template JIT compiler.
- [Lua 5.5 Compliance Plan & Status](LUA_5_5_COMPLIANCE_PLAN.md): Gap analysis, test suite diagnostic matrix, and completed roadmap for full Lua 5.5 compliance.
