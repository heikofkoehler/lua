# Lua VM TODO: Missing Features & Modernization

This document tracks the missing features and planned improvements to bring this custom Lua VM closer to the Lua 5.1-5.4 specifications.

## 1. Language Syntax & Control Flow
- [x] **Tail Call Optimization (TCO):** Implement `OP_TAILCALL` to prevent stack overflow in deep recursion.
- [x] **Goto and Labels:** Add support for `goto` and `::label::` (Lua 5.2+).
- [x] **Bitwise Operators:** Add tokens and opcodes for `&`, `|`, `~`, `<<`, `>>`, and `//` (integer division) (Lua 5.3+).
- [x] **Generic `for` Loop:** Ensure full support for the `for var in iter, state, var do` iterator protocol.

## 2. Metamethods & Data Types
- [x] **Metamethod Completeness:** Add support for `__call`, `__concat`, `__len`, and bitwise metamethods.
- [x] **Weak Tables:** Implement `__mode = "k"` and `__mode = "v"` in the Garbage Collector.
- [x] **Integer Type:** Distinguish between 64-bit integers and doubles in the `Value` NaN-boxing (Lua 5.3+).
- [x] **Userdata:** Implement a general `userdata` type for easier C++ extension development.

## 3. Standard Library Completeness
- [ ] **Pattern Matching:** Replace C++ string methods with Lua-style pattern matching (`%d`, `%a`, `(.-)`, etc.) in `string` library.
- [ ] **C Modules:** Enable `package.loadlib` to load shared libraries (`.so` / `.dll`).
- [ ] **Debug Library:** Implement `debug.getlocal`, `debug.setlocal`, and execution hooks.
- [ ] **Environments:** Implement `_ENV` (Lua 5.2+) or `setfenv`/`getfenv` (Lua 5.1).

## 4. VM & Garbage Collection
- [ ] **Emergency GC:** Trigger garbage collection automatically when an allocation fails.
- [ ] **Upvalue Sharing:** Verify and robustly implement upvalue sharing across coroutine boundaries.
- [ ] **Incremental GC:** Upgrade the mark-and-sweep collector to an incremental or generational model to reduce pause times.

## 5. Tooling & Performance
- [ ] **JIT Compilation:** Explore a basic Template JIT for hot bytecode loops.
- [ ] **LSP Support:** Integrate with a Lua Language Server for better developer experience.
