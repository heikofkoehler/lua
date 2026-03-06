# Garbage Collector Implementation

## Overview

The Lua VM uses a **tri-color incremental garbage collector** based on the mark-and-sweep algorithm. This design reduces "stop-the-world" pause times by interleaving collection work with regular program execution.

## Architecture

### Tri-color Marking

Objects are categorized into three colors:
- **White**: Not yet reached by the collector. Candidates for collection at the end of a cycle.
- **Gray**: Reached by the collector, but its children (referenced objects) haven't been fully explored.
- **Black**: Reached by the collector and all its immediate children have been reached.

### GC Phases

The collector operates in four main states:
1.  **PAUSE**: Initial state. No collection work is being done.
2.  **MARK**: Incremental marking phase. Processes objects from the `grayStack_`.
3.  **ATOMIC**: Final marking phase. Re-scans roots and finishes the gray stack in a single non-interruptible step. Handles weak tables.
4.  **SWEEP**: Incremental/Atomic sweeping phase. Frees white objects and resets surviving objects to white for the next cycle.

### GC Modes

The collector supports two modes of operation:

### 1. Incremental Mode (Default)
Standard tri-color marking as described above. collection work is spread across allocations to maintain low latency.

### 2. Generational Mode
Optimized for short-lived objects (Lua 5.4 style).
- **Young Generation**: New objects are created here.
- **Old Generation**: Objects that survive multiple collection cycles are promoted to the old generation.
- **Minor Collection**: Frequently collects only the young generation.
- **Major Collection**: Performs a full collection cycle.
- **Promotion**: Surviving objects must persist through two minor collection cycles before being promoted to 'old', ensuring they are truly long-lived.

## Write Barriers

To maintain the tri-color invariant (black objects never point to white objects) and the generational invariant (old objects pointing to young objects must be tracked), the VM implements write barriers:
- **Forward Barrier**: If a white object is stored in a black object, the white object is moved to gray.
- **Backward Barrier**: If a black object is modified, it is moved back to gray. Used for objects that are modified frequently, like the coroutine stack.
- **Generational Barrier**: When an old object is modified to point to a young object, the old object is added to the `rememberedSet_`.

Implemented in:
- `TableObject::set` (Forward/Generational)
- `ClosureObject::setUpvalue` (Forward)
- `UpvalueObject::set/close` (Forward)
- `VM::OP_SET_LOCAL` (Backward for Coroutine)
- `UserdataObject::setMetatable` (Forward)

## Implementation Details

### Incremental Steps

Collection work is triggered during allocations in `checkGC`. The amount of work is proportional to the allocation size, ensuring the collector keeps pace with garbage creation.

### Root Set

Roots include:
- Global variables (`globals_`)
- Internal registry (`registry_`)
- Type metatables
- Current and main coroutines

### Memory Tracking

- `bytesAllocated_`: Tracks total bytes currently held by GC-managed objects.
- `nextGC_`: The threshold that triggers the start of a new GC cycle.
- `memoryLimit_`: The hard limit for Emergency GC.

## Performance Characteristics

- **Pause Times**: Significantly reduced compared to stop-the-world mark-and-sweep.
- **Throughput**: Slightly lower due to write barrier overhead and state management.
- **Space**: Minimal overhead for color storage and the gray stack.

## JIT Considerations

The JIT compiler must interact carefully with the garbage collector to maintain correctness and performance:

### GC Checks in JIT Code
JIT-compiled functions need periodic GC checks, especially during loops:
- **Allocation Points**: Any operation that might allocate (table creation, string concatenation) must trigger `checkGC()`
- **Loop Safety**: Long-running loops should include GC checkpoints to prevent memory exhaustion
- **Fallback Mechanism**: Complex operations that allocate fall back to the interpreter, which handles GC automatically

### Current Limitations
The current JIT implementation lacks GC integration:
- No GC checkpoints in compiled loops
- Allocation-heavy operations not yet supported
- Potential for memory leaks in hot JIT code

### Future Enhancements
- Add C ABI calls to `VM::checkGC()` in JIT templates
- Implement GC-safe points in loop constructs
- Support allocation operations with proper GC coordination
