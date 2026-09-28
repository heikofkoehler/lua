# Register VM GC Weak-Table Bug

**Status:** Open, root cause identified but not fixed  
**Affects:** `--vm=register` only (stack VM works correctly)  
**Test:** `lua-5.5.0-tests/gc.lua:286` (official Lua test suite)  
**Branch:** `register-vm`  
**Date:** 2026-09-28

## Symptom

The official Lua test suite fails at `gc.lua:286`:

```lua
-- gc.lua lines 283-286
x,y,z=nil
collectgarbage()
assert(next(a) == string.rep('$', 11))
```

The assertion expects a weak table `a` (with `__mode='kv'`) to contain only one entry (the `$` string) after garbage collection. In register mode, extra dead table entries survive the collection.

## Minimal Reproduction

```lua
local a = {}; setmetatable(a, {__mode = 'v'});
do
  local t = {}
  a["key"] = t
end
collectgarbage()
local count = 0
for k,v in pairs(a) do count = count + 1 end
print(count)  -- Register: 1 (BUG), Stack: 0 (correct)
```

Key observations:
- Affects `do` blocks, numeric `for` loops – any scope where a local table is stored in a weak table then goes out of scope
- `while` loops work correctly (register gets reused, overwriting the stale reference)
- Calling `collectgarbage()` 5 times collects the object; 1 call does not
- Storing the table in a global first (`_G.my_t = t`), then clearing the global, works correctly
- The bug is **register-VM only** – stack VM passes even under allocation stress

## Root Cause Analysis

### The BLACK Marking in `addObject`

`src/vm/gc_impl.cpp`, `VM::addObject()`:

```cpp
void VM::addObject(GCObject* object) {
    object->setNext(gcObjects_);
    gcObjects_ = object;
    bytesAllocated_ += object->size();

    // In incremental GC: if a cycle is in progress (MARK, ATOMIC, or SWEEP),
    // newly allocated objects must be marked black so they are not swept prematurely.
    if (gcState_ != GCState::PAUSE) {
        object->setColor(GCObject::Color::BLACK);
    }
}
```

New objects allocated while a GC cycle is in progress are marked BLACK (reachable). This prevents use-after-free when the GC runs during allocation.

### Why It Breaks Weak Tables

1. The `t` table is allocated via `ROP_NEWTABLE` → `createTable()` → `allocateObject()`
2. If `gcState_ != PAUSE` at allocation time (GC cycle in progress), `t` is marked BLACK
3. `t` is stored in weak table `a` via `a["key"] = t`
4. The `do` block ends; the local register holding `t` is cleared (via `ROP_LOADNIL` in `RCodeGen::popScope()`)
5. `collectgarbage()` runs a full GC cycle:
   - **Mark phase:** `t` is already BLACK, so it's not re-evaluated. The weak-table handling in `blackenObject()` correctly skips marking weak values, but `t` is already BLACK from allocation.
   - **Weak table processing:** `processWeakTables()` checks if entries are WHITE before removing them. Since `t` is BLACK, the entry is kept.
   - **Sweep phase:** Only WHITE objects are freed. `t` (BLACK) survives.
6. On subsequent GC cycles, the stale BLACK marking is eventually cleared (via sweep resetting survivors to WHITE), and the object is collected. This is why 5x `collectgarbage()` works.

### Why Removing BLACK Breaks Things

Removing the BLACK marking causes `test_incremental_gc` and `test_incremental_gc_register` to SEGFAULT. New objects allocated during an in-progress GC cycle get swept prematurely before they're anchored in the object graph (use-after-free).

The BLACK marking is necessary for correctness, but it interacts badly with weak tables.

### Why Stack VM Is Not Affected

The GC code (`gc_impl.cpp`) is shared between both VMs. The difference is in allocation timing:

- The register VM's allocation pattern (or `checkGC` call sites) leaves `gcState_ != PAUSE` more frequently at the point where weak-table values are allocated
- The stack VM's allocation pattern does not trigger the BLACK marking for these specific objects
- Even under allocation stress (1000 table allocations before the test), the stack VM works correctly

The exact difference in GC state timing between the two VMs has not been fully characterized.

## Attempted Fixes

### 1. Register Clearing on Scope Exit (Committed)

**File:** `src/compiler/rcodegen.cpp`, `RCodeGen::popScope()`

Emit `ROP_LOADNIL` for each dead local register when a lexical scope exits:

```cpp
if (s.baseReg > 0) {
    for (const auto& pair : s.locals) {
        int reg = pair.second.reg;
        emitAB(ROpCode::ROP_LOADNIL, reg, 0);
    }
}
```

**Result:** Safe (408/408 CTests pass, including module tests), but does NOT fix the weak-table bug. The object is kept alive by the BLACK GC marking, not by the register.

**Commit:** `46304ac`

### 2. Broad Register Clearing (Reverted)

Cleared the entire `[baseReg, nextReg_)` range on scope exit.

**Result:** Broke 3 module tests (`test_modules_register`, `test_package_searchers_register`, `test_require_register`). Too aggressive – cleared live temporaries.

### 3. Key Write Barrier (Reverted)

Added `writeBarrier(table, key)` in `ROP_SETTABLE` for table keys.

**Result:** No effect on the bug.

### 4. Remove BLACK Marking (Reverted)

Removed the `if (gcState_ != PAUSE) setColor(BLACK)` from `addObject()`.

**Result:** Fixed the weak-table issue, but caused SEGFAULTs in `test_incremental_gc` (use-after-free). The BLACK marking is necessary.

### 5. Double GC Cycle (Reverted)

Modified `VM::collectGarbage()` to run two full cycles instead of one.

**Result:** No effect – the object is marked BLACK in both cycles.

## Proper Fix (Not Implemented)

The fix needs to distinguish "BLACK because truly reachable" from "BLACK because allocated during GC":

**Option A:** Track allocation cycle numbers. In `collectGarbage()`, reset BLACK objects allocated in the previous cycle to WHITE before starting the new mark phase.

**Option B:** In `addObject()`, instead of marking BLACK, push the new object onto the gray stack (to be scanned). If the object is truly unreachable, it will be scanned, found to have no incoming references from roots, and... (still needs cycle tracking to avoid keeping it alive).

**Option C:** Defer the BLACK marking: only mark BLACK if the GC is in MARK phase (not ATOMIC or SWEEP). Objects allocated during ATOMIC/SWEEP are less likely to be prematurely swept.

**Option D:** In `processWeakTables()`, treat BLACK objects that were not marked in the current cycle as WHITE for weak-entry removal purposes. Requires tracking which objects were marked in the current cycle.

All options require deeper GC surgery and careful testing to avoid reintroducing the use-after-free.

## Workaround

None. The official test suite cannot pass in register mode until this is fixed.

## References

- `src/vm/gc_impl.cpp`: `VM::addObject()` (line 12), `VM::collectGarbage()` (line 632), `VM::processWeakTables()` (line 94), `blackenObject()` (line 301)
- `src/compiler/rcodegen.cpp`: `RCodeGen::popScope()` (surgical clearing)
- Test: `/home/hatch/workspace/lua-review/lua-5.5.0-tests/gc.lua:286`
