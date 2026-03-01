#include "value/closure.hpp"
#include "value/upvalue.hpp"
#include "vm/vm.hpp"

void ClosureObject::setUpvalue(size_t index, UpvalueObject* upvalue) {
    if (index < upvalues_.size()) {
        if (upvalue && VM::currentVM) {
            VM::currentVM->writeBarrier(this, upvalue);
        }
        upvalues_[index] = upvalue;
    }
}

void ClosureObject::markReferences() {
    // Handled in blackenObject
}
