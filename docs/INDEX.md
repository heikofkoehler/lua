# Lua VM Documentation Index

Welcome to the internal documentation for the Lua VM project. These documents provide design and implementation details useful for programmers working on the codebase.

## Core Architecture
- [VM Architecture and Execution Loop](VM_ARCHITECTURE.md): The execution stack, call frames, and the main dispatch loop.
- [Value Representation (NaN-Boxing)](VALUE_REPRESENTATION.md): How all Lua values are packed into 64-bit doubles.
- [Compiler Pipeline](COMPILER_PIPELINE.md): Lexing, parsing, AST, and bytecode generation.

## Systems
- [Garbage Collector Implementation](GC_IMPLEMENTATION.md): Details on the tri-color incremental and generational collector.
- [Standard Library Implementation](STDLIB_IMPLEMENTATION.md): Overview of implemented libraries and native function architecture.
- [C API Documentation](C_API.md): Guide to the Lua-compatible C API.
- [REPL Features and Implementation](REPL.md): Interactive REPL details including autocomplete and multi-line support.

## Feature Spotlights
- [Dot Notation and Method Calls](DOT_NOTATION.md): Implementation details for table access and `obj:method()` syntax.
- [JIT Compilation Plan](JIT_COMPILATION_PLAN.md): Current status and future goals for the experimental JIT compiler.
