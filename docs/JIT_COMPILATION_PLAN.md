# JIT Compilation Plan: Template JIT

## 1. Objective
Enhance the performance of the Lua VM by implementing a **Template Just-In-Time (JIT) Compiler**. The goal is to identify hot loops and frequently executed functions during runtime and compile their bytecode into native machine code, replacing the overhead of the switch-based dispatch loop (`src/vm/run_impl.cpp`) with direct execution.

## 2. Current Implementation Status ✅

### 2.1 Completed Infrastructure
- **Library Integration**: AsmJit library integrated for ARM64 machine code generation
- **Hotness Detection**: Function hotness counters implemented with thresholds (50 for OP_LOOP, 10 for OP_CALL/OP_TAIL_CALL)
- **JIT Compiler Class**: `JITCompiler` class with AsmJit runtime and compilation methods
- **Memory Management**: Executable memory allocation via AsmJit's JitRuntime
- **VM Integration**: JIT compiler instantiated in VM, friend class access to internal state

### 2.2 Completed Templates (Phases 2 & 4)
The following opcode categories are fully implemented with native ARM64 code generation in `src/vm/jit.cpp`:

**Stack Operations:**
- `OP_CONSTANT`, `OP_CONSTANT_LONG` - Load constants directly to stack
- `OP_GET_LOCAL`, `OP_SET_LOCAL` - Load/store local variables relative to frame base
- `OP_POP`, `OP_DUP`, `OP_SWAP` - Stack manipulations
- `OP_NIL`, `OP_TRUE`, `OP_FALSE` - Immediate literals

**Arithmetic & Bitwise Operations:**
- `OP_ADD`, `OP_SUB`, `OP_MUL`, `OP_DIV` - Double-precision arithmetic with fast inline paths
- `OP_IDIV`, `OP_MOD`, `OP_POW` - Integer division, modulo, and exponentiation
- `OP_NEG` - Unary arithmetic negation
- `OP_BAND`, `OP_BOR`, `OP_BXOR`, `OP_BNOT`, `OP_SHL`, `OP_SHR` - 64-bit integer bitwise operations

**Comparisons & Logic:**
- `OP_EQUAL` - Equality comparison with type checking
- `OP_LESS`, `OP_LESS_EQUAL`, `OP_GREATER`, `OP_GREATER_EQUAL` - Ordering comparisons
- `OP_NOT` - Logical negation (truthy/falsy conversion)

**Control Flow & Loops:**
- `OP_JUMP` - Unconditional forward jumps
- `OP_LOOP` - Backward jumps for tight loop constructs
- `OP_JUMP_IF_FALSE` - Conditional branches based on falsy values (nil/false)
- `OP_FORPREP`, `OP_FORLOOP` - Specialized numeric for-loop mechanics with loop counter updates

**Table & Field Access:**
- `OP_NEW_TABLE` - Table instantiation
- `OP_GET_TABLE`, `OP_SET_TABLE` - Indexed table reads and writes (with array fast paths)
- `OP_LEN` - Length operator for strings and tables

**Globals & Upvalues:**
- `OP_GET_GLOBAL`, `OP_SET_GLOBAL` - Global environment variable access
- `OP_GET_TABUP`, `OP_SET_TABUP`, `OP_GET_TABUP_LONG`, `OP_SET_TABUP_LONG` - Table upvalue indexing (`_ENV` accesses)
- `OP_GET_UPVALUE`, `OP_SET_UPVALUE`, `OP_CLOSE_UPVALUE` - Lexical closure upvalue access and closing

**Closures & Functions:**
- `OP_CLOSURE`, `OP_CLOSURE_LONG` - Nested closure generation and upvalue capture
- `OP_CONCAT` - String concatenation
- `OP_CALL`, `OP_CALL_MULTI` - Function call invocation with single and multiple result expectations
- `OP_TAILCALL`, `OP_TAILCALL_MULTI` - Proper tail call optimization in native code
- `OP_RETURN`, `OP_RETURN_VALUE`, `OP_RETURN_VALUE_MULTI` - Function return mechanics

### 2.3 Completed Execution Integration
- **Entry Thunk**: `JITFunc` typedef for compiled function pointers
- **Context Loading**: VM and coroutine state loaded from registers at function entry
- **Stack Mapping**: Direct access to `currentCoroutine_->stack` via base pointer register
- **Stack Headroom**: Automatic dynamic stack capacity validation (`jitEnsureStack`)
- **State Synchronization**: Stack top and frame pointers synchronized on calls and interpreter fallback
- **Fallback Mechanism**: JIT functions return instruction pointer on completion/unhandled instructions

### 2.4 Completed Hotness Tracking
- **Counters**: `hotness_` field in `FunctionObject` with increment methods
- **Thresholds**: Tuned thresholds for loop jumps and calls
- **Triggering**: Automatic JIT compilation when hotness exceeds threshold
- **Caching**: Compiled JIT code cached in `FunctionObject::jitCode_`

## 3. Future Extensions (Phase 5+)

### 3.1 x86_64 Architecture Backend
- Extend AsmJit code generation templates to support x86_64 calling conventions and instruction sets alongside ARM64.

### 3.2 Inline Caching & Polymorphic Type Feedback
- Record seen types at `OP_GET_TABLE` / `OP_SET_TABLE` sites to emit monomorphic table access inline caches without indirect calls.

### 3.3 Trace JIT / Loop Invariant Code Motion
- Further specialize hot inner loops by hoisting bounds checks and invariant table lookups out of loops.

## 4. Architectural Approach: Template JIT
A Template JIT is the most pragmatic approach for a stack-based VM. Instead of building a complex Intermediate Representation (IR) and optimizing it, the JIT maps each individual `OpCode` directly to a pre-written "template" of machine code.

### 4.1 Backend / Code Generation
- **Library**: Use a lightweight C++ JIT assembler library such as **[AsmJit](https://github.com/asmjit/asmjit)** to handle machine code emission for x86_64/ARM64. It abstracts away binary encoding and provides a clean C++ API.
- **Memory**: The JIT will allocate executable memory pages (`mmap` with `PROT_EXEC` on POSIX, `VirtualAlloc` with `PAGE_EXECUTE_READWRITE` on Windows) to write the compiled templates.

## 5. Integration with the Existing VM

### 5.1 Profiling and Hotness Detection
- Introduce a `hotnessCounter` to `FunctionObject` or `Chunk`.
- In `run_impl.cpp`, increment this counter on specific instructions, such as `OP_LOOP` or `OP_CALL`.
- When the counter exceeds a predefined threshold (e.g., 500 iterations), the VM pauses execution of that chunk and triggers the compilation of the chunk into native code.

### 5.2 Stack and State Mapping
The JIT code will need to interact seamlessly with the existing C++ state.
- **Stack**: The JIT will not attempt to map the Lua stack to CPU registers. It will read and write directly to `currentCoroutine_->stack` using a base pointer register (e.g., pointing to `stack.data() + stackBase`).
- **Context Pointer**: Pass the `VM*` instance pointer as the primary argument to the JIT-compiled function, allowing machine code to call back into C++ VM methods.

### 5.3 The JIT / Interpreter Boundary
- **Entry Thunk**: A C++ function pointer cast that calls into the generated machine code, passing the `VM*` context.
- **Exit / Fallback**: If the JIT encounters a complex instruction it cannot handle (or an error/GC trigger), it updates the VM's `Instruction Pointer (IP)` and returns. The interpreter loop resumes from that IP.

## 6. Known Challenges & Considerations

1. **NaN-boxing in Assembly**: Extracting the type tag and double values via bitwise operations in assembly will require careful encoding.
2. **Garbage Collection**: The GC expects to be triggered automatically via `checkGC()`. The JIT must periodically (or on allocations) call the C++ `checkGC()` routine to prevent memory exhaustion during tight loops.
3. **C++ `std::vector` Reallocation**: The Lua stack (`currentCoroutine_->stack`) is a `std::vector`. If an operation causes it to reallocate, the base data pointer held in the JIT's register will become invalid. The JIT must either trigger a C++ callback for any `push()` that risks capacity, or reload the data pointer after any stack growth.
4. **Cross-Platform ABI**: Managing register preservation and function calls differs between Windows (MSVC) and POSIX (System V), as well as between x86_64 and ARM64. Using a library like AsmJit's `Compiler` API helps abstract these ABI differences.
