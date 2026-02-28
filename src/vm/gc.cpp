#include "vm/gc.hpp"
#include "value/table.hpp"
#include "value/closure.hpp"
#include "value/upvalue.hpp"
#include "vm/vm.hpp"

// TableObject: Mark all keys and values in the table
void TableObject::markReferences() {
    // Tables store Values which may reference other GC objects
    // The VM will handle marking those values through its mark routines
}

// ClosureObject: Mark the upvalues this closure captures
void ClosureObject::markReferences() {
    // Upvalues are referenced by index into VM's upvalue pool
    // The VM will handle marking upvalues separately
}

// UpvalueObject: Mark the closed value if the upvalue is closed
// (Implementation moved to upvalue.cpp)
