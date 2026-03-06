# VM Architecture and Execution Loop

This document describes the internal structure of the Lua virtual machine and how it executes bytecode.

## Core Components

The VM implementation is split across several files in `src/vm/`:
- `vm.cpp/hpp`: Core VM state, object management, and high-level execution API.
- `run_impl.cpp`: The main instruction dispatch loop.
- `opcode.hpp`: Definition of all bytecode instructions.

## The Execution Stack

The VM is stack-based. Every coroutine has its own value stack (`std::vector<Value>`).
- **Locals**: Local variables are stored directly on the stack.
- **Temporaries**: Intermediate results of expressions are pushed and popped.
- **Function Calls**: Arguments are pushed, followed by the closure. When a function is called, a new `CallFrame` is created that points to the base of its arguments on the stack.

## Call Frames

Function calls are managed through `CallFrame` structures:
- `closure`: The `ClosureObject` being executed.
- `ip`: The instruction pointer (offset into the chunk's code).
- `stackBase`: The absolute index in the value stack where this frame starts.
- `retCount`: Number of expected return values (supports Lua's `MULTRET` protocol).

## The Instruction Loop

The heart of the VM is the `run()` function in `src/vm/run_impl.cpp`. It features a large `switch` statement (optimized by the compiler) that dispatches opcodes.

### Key Opcodes

- **Stack Manipulation**: `OP_PUSH`, `OP_POP`, `OP_DUP`.
- **Variables**: `OP_GET_GLOBAL`, `OP_SET_GLOBAL`, `OP_GET_LOCAL`, `OP_SET_LOCAL`.
- **Tables**: `OP_NEW_TABLE`, `OP_GET_TABLE`, `OP_SET_TABLE`.
- **Functions**: `OP_CALL`, `OP_RETURN`, `OP_CLOSURE` (creates a closure from a function template).
- **Upvalues**: `OP_GET_UPVALUE`, `OP_SET_UPVALUE`, `OP_CLOSE_UPVALUE`.
- **Control Flow**: `OP_JUMP`, `OP_JUMP_IF_FALSE`, `OP_FOR_PREP`, `OP_FOR_LOOP`.

## Upvalue Management

Closures capture variables from outer scopes using **Upvalues**.
- While the outer function is running, an upvalue is "open" and points directly to a location on the stack.
- When the outer variable goes out of scope (e.g., the function returns), the upvalue is "closed" — the value is moved from the stack into the `UpvalueObject` itself so it can persist as long as the closure exists.

## Coroutines

Coroutines are implemented as first-class objects (`CoroutineObject`). Each coroutine tracks its own stack, call frame list, and status (`RUNNING`, `SUSPENDED`, `DEAD`). 
- `coroutine.yield()` saves the current `ip` and frame state and returns control to the `resume()` caller.
- `coroutine.resume()` restores the state and continues execution from the saved `ip`.

## Error Handling

Errors are handled using C++ exceptions (`RuntimeError`). 
- `pcall` and `xpcall` catch these exceptions, unwind the stack to the protected boundary, and return a boolean status to Lua code.
- Standard library errors (like `math.sqrt(-1)`) also throw `RuntimeError`.
