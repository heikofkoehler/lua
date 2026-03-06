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

### 2.2 Completed Templates (Phase 2)
The following opcodes are fully implemented with native ARM64 code generation:

**Stack Operations:**
- `OP_CONSTANT` - Load constants directly to stack
- `OP_GET_LOCAL` - Load local variables from frame base
- `OP_SET_LOCAL` - Store to local variables
- `OP_POP` - Adjust stack pointer
- `OP_NIL` - Push nil values

**Arithmetic Operations:**
- `OP_ADD` - Double precision floating point addition with NaN-boxing
- `OP_SUB` - Double precision floating point subtraction
- `OP_LESS_EQUAL` - Floating point comparison with boolean result
- `OP_GREATER_EQUAL` - Floating point comparison with boolean result

**Control Flow:**
- `OP_JUMP` - Unconditional jumps to labeled addresses
- `OP_LOOP` - Backward jumps for loop constructs
- `OP_JUMP_IF_FALSE` - Conditional jumps based on falsy values (nil/false)

**Function Operations:**
- `OP_RETURN` - Return nil from functions with stack cleanup
- `OP_RETURN_VALUE` - Return single value from functions with stack cleanup

### 2.3 Completed Execution Integration (Phase 3)
- **Entry Thunk**: `JITFunc` typedef for compiled function pointers
- **Context Loading**: VM and coroutine state loaded from registers at function entry
- **Stack Mapping**: Direct access to `currentCoroutine_->stack` via base pointer register
- **State Synchronization**: Stack top and frame pointers updated in coroutine object
- **Fallback Mechanism**: JIT functions return instruction pointer on completion/failure

### 2.4 Completed Hotness Tracking
- **Counters**: `hotness_` field in `FunctionObject` with increment methods
- **Thresholds**: Different thresholds for different opcodes (loops: 50, calls: 10)
- **Triggering**: Automatic JIT compilation when hotness exceeds threshold
- **Caching**: Compiled JIT code cached in `FunctionObject::jitCode_`

## 3. Remaining Implementation (Phase 4+)

### 3.1 Missing Basic Opcodes
The following fundamental opcodes need template implementation:
- `OP_MUL`, `OP_DIV`, `OP_MOD` - Arithmetic operations
- `OP_NEGATE` - Unary negation
- `OP_NOT` - Logical NOT
- `OP_EQUAL`, `OP_LESS`, `OP_GREATER` - Comparison operations
- `OP_TRUE`, `OP_FALSE` - Boolean literals
- `OP_CALL`, `OP_TAIL_CALL` - Function call mechanics
- `OP_CLOSURE` - Closure creation with upvalue capture

### 3.2 Missing Complex Operations
- `OP_NEW_TABLE` - Table creation and initialization
- `OP_SET_TABLE`, `OP_GET_TABLE` - Table operations
- `OP_SET_UPVALUE`, `OP_GET_UPVALUE` - Upvalue access
- `OP_CLOSE_UPVALUE` - Upvalue closing
- String operations (`OP_CONCAT`, etc.)
- Metamethod support

### 3.3 Missing Advanced Features
- **Multi-return Values**: Current implementation only handles single returns
- **Error Handling**: Exception propagation and stack unwinding
- **Garbage Collection**: Periodic GC checks during JIT execution
- **Debug Support**: Source line mapping and debug hooks
- **Coroutine Support**: Yield/resume across JIT boundaries

### 3.4 C++ Callbacks for Complex Opcodes
For opcodes requiring significant C++ logic:
1. Implement static helper methods in `VM` class
2. Add C ABI function calls in JIT templates
3. Handle register preservation across calls
4. Maintain stack consistency during callbacks

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
