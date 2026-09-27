# Direct Register Code Generation

## Goal

Eliminate the stack-to-register translator (`rtranslate.cpp`) by compiling
directly from AST to register bytecode.

## Current Architecture

```
Lua source
  → Lexer → Parser → AST
  → CodeGen (stack bytecode, OP_*)
  → RTranslate (register bytecode, ROP_*)
  → Register VM
```

## New Architecture

```
Lua source
  → Lexer → Parser → AST
  → RCodeGen (register bytecode, ROP_*)
  → Register VM
```

The stack codegen (`codegen.cpp`) and translator (`rtranslate.cpp`) are
deleted. The register VM (`rrun.cpp`) is unchanged.

## Design

### Register Allocation

- Each function gets a fixed register file.
- Parameters: R(0)..R(nparams-1)
- Locals: allocated sequentially after params.
- Temporaries: allocated from a free list, released after use.
- No SP tracking. No stack simulation.

### Visitor Pattern

`RCodeGen` implements `ASTVisitor`, emitting `ROP_*` instructions directly.

For each AST node:
- `LiteralNode`: Emit `ROP_LOADK` or `ROP_LOADNIL`.
- `BinaryNode`: Recursively gen left/right into temps, emit `ROP_ADD` etc.
- `CallNode`: Gen function and args, emit `ROP_CALL`.
- `LocalDecl`: Allocate register, gen init value into it.
- `IfStmt`: Gen condition, emit `ROP_JMPIF`, gen branches, patch jumps.
- `WhileStmt`: Emit loop header, gen condition/body, patch.
- `ForNumStmt`: Emit `ROP_FORPREP`/`ROP_FORLOOP`.
- `FuncDef`: Create child `RCodeGen` for body, emit `ROP_CLOSURE`.
- `ReturnStmt`: Gen values, emit `ROP_RETURN`.

### Key Differences from Translator

1. **No SP**: Registers are allocated explicitly, not via stack simulation.
2. **No dynamic SP**: Multires is handled by the VM via `frame.resultCount`,
   not by translator SP tracking.
3. **Direct**: No intermediate stack bytecode. The AST maps directly to
   register operations.

## Implementation Plan

### Phase 1: Foundation
- [x] Create `rcodegen.hpp` / `rcodegen.cpp` with `RCodeGen` class
- [x] Register allocator (alloc/free, locals table)
- [x] Instruction emitter (wrappers around RInstruction)
- [x] Jump patching (forward/backward)

### Phase 2: Expressions
- [x] Literals (nil, boolean, number, string)
- [x] Unary ops (-, not, #, ~)
- [x] Binary ops (+, -, *, /, %, ^, .., ==, ~=, <, >, <=, >=, and, or)
- [x] Variable access (local, upvalue, global)
- [x] Table constructor
- [x] Table access (index)

### Phase 3: Statements
- [x] Local declaration
- [x] Assignment
- [x] If/elseif/else
- [x] While loop
- [x] Repeat-until
- [x] Numeric for
- [x] Generic for
- [x] Return
- [x] Break

### Phase 4: Functions
- [x] Function definition (closure creation)
- [x] Parameters (including vararg ...)
- [x] Call (fixed and multires)
- [x] Tail calls
- [x] Upvalues

### Phase 5: Integration
- [x] Wire into `lua` binary (--register flag uses RCodeGen directly)
- [x] Delete `rtranslate.cpp` / `rtranslate.hpp`
- [ ] Delete stack `codegen.cpp` (or keep for --nojit mode?) — KEEP FOR NOW per recommendation
- [x] Update tests (408/408 CTests pass)

## Open Questions

1. Should we keep the stack VM for --nojit mode? If yes, keep codegen.cpp.
   If no, delete it. (Recommendation: keep for now, delete later.)

2. How to handle the existing 100/204 passing register tests? They test the
   translator output. With direct codegen, they should still pass if the
   semantics are correct.

3. The register VM (`rrun.cpp`) is unchanged. Only the producer of ROP_*
   changes.
