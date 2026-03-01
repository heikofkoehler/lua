#include "value/upvalue.hpp"
#include "value/coroutine.hpp"
#include "vm/vm.hpp"

Value UpvalueObject::get(const std::vector<Value>& /*currentStack*/) const {
    if (isClosed_) {
        return closed_;
    }
    // Use the owner coroutine's stack
    return owner_->stack[stackIndex_];
}

void UpvalueObject::set(std::vector<Value>& /*currentStack*/, const Value& value) {
    if (isClosed_) {
        if (VM::currentVM) VM::currentVM->writeBarrier(this, value);
        closed_ = value;
    } else {
        // Use the owner coroutine's stack
        if (VM::currentVM && value.isObj()) {
            // Stack modification: move owner coroutine to gray if it was black
            VM::currentVM->writeBarrierBackward(owner_, value.asObj());
        }
        owner_->stack[stackIndex_] = value;
    }
}

void UpvalueObject::close(const std::vector<Value>& /*currentStack*/) {
    if (!isClosed_) {
        // Capture value from owner's stack
        closed_ = owner_->stack[stackIndex_];
        if (VM::currentVM) VM::currentVM->writeBarrier(this, closed_);
        isClosed_ = true;
        owner_ = nullptr; // No longer need owner once closed
    }
}

void UpvalueObject::markReferences() {
    // Handled in blackenObject
}
