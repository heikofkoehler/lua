# Register VM Translator: Multires Call Redesign

## Problem

The translator uses static SP (stack pointer) tracking to map stack positions
to registers. For dynamic (multires) calls, the number of results is unknown
at compile time, breaking the SP model.

### Failing Pattern: `return f()`

Bytecode:
```
OP_TAILCALL 0          ; or OP_CALL with C=0
OP_RETURN_VALUE_MULTI 0
OP_RETURN
```

Translation issue:
- `OP_TAILCALL`/`OP_CALL` with multires places results at `R(funcReg)`
- `OP_RETURN_VALUE_MULTI` needs to return from `R(funcReg)`
- But translator computes destination as `R(sp - fixed)` which is wrong
- The SP after a multires call is undefined (dynamic result count)

### Root Cause

The SP analysis assumes static stack effects. For `OP_CALL` with `C=0`
(multires), the result count is dynamic. The translator has no way to know
where the results are using SP alone.

Similarly, `OP_TAILCALL` is not in the SP analysis groups, causing translation
to fail when we try to give it fallthrough semantics.

## Redesign: Explicit Destination Tracking

Instead of SP-based tracking for multires, use explicit destination tracking
via side tables.

### Design

1. **Side table**: `std::unordered_map<size_t, int> callDest_` mapping from
   CALL instruction output PC to destination register (`funcReg`).

2. **Emission**:
   - When emitting `ROP_CALL` with `C=0` (multires), record
     `callDest_[out_.size() - 1] = funcReg`.
   - When emitting `ROP_RETURN` with `B=0` (from `RETURN_VALUE_MULTI`),
     look up the preceding CALL's destination. For the `return f()` pattern,
     the CALL immediately precedes, so we can use a simple `lastCallDest_`
     variable. For more complex patterns, use the side table.

3. **SP Analysis**:
   - For `OP_TAILCALL`: Treat as having fallthrough (since we convert to CALL
     in emission). SP effect: pop `argc+1` (function + args), push 0 (results
     handled via side table, not SP).
   - For `OP_CALL` with `C=0`: SP effect is dynamic. For the `return f()`
     pattern, the results are immediately returned, so we can treat as
     pop `argc+1`, push 0.

4. **VM**:
   - `ROP_CALL` with `C=0`: Already handles multires via `frame.resultCount`.
   - `ROP_RETURN` with `B=0`: Already reads from `frame.resultCount`.
   - The destination register (`A` operand) must be correct. The translator
     ensures this via the side table.

### Implementation Steps

1. Add `lastCallDest_` member variable to translator class.
2. Add `callDest_` side table (for non-adjacent patterns, future).
3. Update SP analysis for `OP_TAILCALL` to have fallthrough and proper SP effect.
4. Update emission for `OP_TAILCALL` to convert to `ROP_CALL` with `C=0`,
   recording destination.
5. Update emission for `OP_CALL` with `C=0` to record destination.
6. Update emission for `OP_RETURN_VALUE_MULTI` to use recorded destination.
7. Reset `lastCallDest_` after use and on any non-CALL opcode.

### Caveats

- This handles the `return f()` pattern. More complex patterns like
  `local a, b = f(); return a` need the full side table lookup.
- The SP after a multires CALL is still undefined. Code that uses the SP
  after a multires call (without immediate return) will need the side table.
- TAILCALL frame reuse (proper tail calls) is still TODO. Current
  implementation does a regular call, which is correct but not optimal.
