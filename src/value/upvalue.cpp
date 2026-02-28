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
        closed_ = value;
    } else {
        // Use the owner coroutine's stack
        owner_->stack[stackIndex_] = value;
    }
}

void UpvalueObject::close(const std::vector<Value>& /*currentStack*/) {
    if (!isClosed_) {
        // Capture value from owner's stack
        closed_ = owner_->stack[stackIndex_];
        isClosed_ = true;
        owner_ = nullptr; // No longer need owner once closed
    }
}

void UpvalueObject::markReferences() {
    if (isClosed_) {
        if (closed_.isObj()) {
            closed_.asObj()->mark();
            closed_.asObj()->markReferences();
        }
    } else {
        // Open upvalue: keep owner coroutine alive
        if (owner_) {
            owner_->mark();
            owner_->markReferences();
        }
    }
}
