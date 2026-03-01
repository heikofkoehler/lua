# JIT Compilation Plan: Template JIT

## 1. Objective
Enhance the performance of the Lua VM by implementing a **Template Just-In-Time (JIT) Compiler**. The goal is to identify hot loops and frequently executed functions during runtime and compile their bytecode into native machine code, replacing the overhead of the switch-based dispatch loop (`src/vm/run_impl.cpp`) with direct execution.

## 2. Architectural Approach: Template JIT
A Template JIT is the most pragmatic approach for a stack-based VM. Instead of building a complex Intermediate Representation (IR) and optimizing it, the JIT maps each individual `OpCode` directly to a pre-written "template" of machine code. 

### 2.1 Backend / Code Generation
- **Library**: Use a lightweight C++ JIT assembler library such as **[AsmJit](https://github.com/asmjit/asmjit)** to handle machine code emission for x86_64/ARM64. It abstracts away binary encoding and provides a clean C++ API.
- **Memory**: The JIT will allocate executable memory pages (`mmap` with `PROT_EXEC` on POSIX, `VirtualAlloc` with `PAGE_EXECUTE_READWRITE` on Windows) to write the compiled templates.

## 3. Integration with the Existing VM

### 3.1 Profiling and Hotness Detection
- Introduce a `hotnessCounter` to `FunctionObject` or `Chunk`.
- In `run_impl.cpp`, increment this counter on specific instructions, such as `OP_LOOP` or `OP_CALL`.
- When the counter exceeds a predefined threshold (e.g., 500 iterations), the VM pauses execution of that chunk and triggers the compilation of the chunk into native code.

### 3.2 Stack and State Mapping
The JIT code will need to interact seamlessly with the existing C++ state.
- **Stack**: The JIT will not attempt to map the Lua stack to CPU registers. It will read and write directly to `currentCoroutine_->stack` using a base pointer register (e.g., pointing to `stack.data() + stackBase`).
- **Context Pointer**: Pass the `VM*` instance pointer as the primary argument to the JIT-compiled function, allowing machine code to call back into C++ VM methods.

### 3.3 The JIT / Interpreter Boundary
- **Entry Thunk**: A C++ function pointer cast that calls into the generated machine code, passing the `VM*` context.
- **Exit / Fallback**: If the JIT encounters a complex instruction it cannot handle (or an error/GC trigger), it updates the VM's `Instruction Pointer (IP)` and returns. The interpreter loop resumes from that IP.

## 4. Implementation Phases

### Phase 1: Profiling & Infrastructure
1. Add an external dependency for assembly generation (e.g., AsmJit).
2. Add execution counters (`hotness`) to `FunctionObject`.
3. Add a `triggerJIT` threshold check in `OP_LOOP` and `OP_CALL`.
4. Create a `JITCompiler` class capable of allocating executable memory and returning a function pointer.

### Phase 2: Basic Template Generation
1. Implement templates for basic stack manipulation: `OP_GET_LOCAL`, `OP_SET_LOCAL`, `OP_CONSTANT`, `OP_POP`.
2. Implement templates for basic arithmetic: `OP_ADD`, `OP_SUB`, `OP_MUL`, `OP_LESS`, `OP_JUMP`, `OP_JUMP_IF_FALSE`, `OP_LOOP`.
3. Since our values use **NaN-boxing**, the assembly must unpack the 64-bit integer, verify the double type, perform the arithmetic, and pack it back.

### Phase 3: Context Switching & Execution
1. Create the Entry Thunk: `typedef bool (*JITFunc)(VM*);`
2. At the start of a JIT compiled function, load `VM*` from the ABI argument register (e.g., `RDI` on x86_64, `X0` on ARM64).
3. Read `currentCoroutine_->stack.data()` into a dedicated register to act as the stack base.
4. Execute the templates.
5. Provide an exit routine that saves the current IP back to the `CallFrame` and returns `true` (success) or `false` (error) to the C++ caller.

### Phase 4: C++ Callbacks for Complex Opcodes
1. Opcodes like `OP_NEW_TABLE`, `OP_CLOSURE`, or `OP_CALL` involve significant C++ logic and memory allocation.
2. The JIT will emit a standard C ABI function call (using `CALL` instruction) back to static helper methods on the `VM` class to execute these complex routines.
3. The JIT must correctly handle caller-saved registers before making these C++ calls.

## 5. Known Challenges & Considerations

1. **NaN-boxing in Assembly**: Extracting the type tag and double values via bitwise operations in assembly will require careful encoding.
2. **Garbage Collection**: The GC expects to be triggered automatically via `checkGC()`. The JIT must periodically (or on allocations) call the C++ `checkGC()` routine to prevent memory exhaustion during tight loops.
3. **C++ `std::vector` Reallocation**: The Lua stack (`currentCoroutine_->stack`) is a `std::vector`. If an operation causes it to reallocate, the base data pointer held in the JIT's register will become invalid. The JIT must either trigger a C++ callback for any `push()` that risks capacity, or reload the data pointer after any stack growth.
4. **Cross-Platform ABI**: Managing register preservation and function calls differs between Windows (MSVC) and POSIX (System V), as well as between x86_64 and ARM64. Using a library like AsmJit's `Compiler` API helps abstract these ABI differences.
