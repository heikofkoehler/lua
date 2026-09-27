#include "value/upvalue.hpp"
#include "value/coroutine.hpp"
#include "vm/vm.hpp"
#include <algorithm>

UpvalueObject::~UpvalueObject() {
    if (!isClosed_ && owner_) {
        auto& list = owner_->openUpvalues;
        auto it = std::find(list.begin(), list.end(), this);
        if (it != list.end()) {
            list.erase(it);
        }
        owner_ = nullptr;
    }
}

Value UpvalueObject::get(const std::vector<Value>& /*currentStack*/) const {
    if (isClosed_) {
        return closed_;
    }
    // Use the owner coroutine's stack
    if (owner_ && stackIndex_ < owner_->stack.size()) {
        return owner_->stack[stackIndex_];
    }
    return closed_;
}

void UpvalueObject::set(std::vector<Value>& /*currentStack*/, const Value& value) {
    if (isClosed_) {
        if (VM::currentVM) VM::currentVM->writeBarrier(this, value);
        closed_ = value;
    } else {
        // Use the owner coroutine's stack
        if (owner_) {
            if (VM::currentVM && value.isObj()) {
                // Forward barrier: if owner is black and value is white during MARK,
                // gray the value directly (more precise than re-graying the owner).
                VM::currentVM->writeBarrier(owner_, value.asObj());
                // Backward barrier: re-gray owner if it was black (conservative).
                VM::currentVM->writeBarrierBackward(owner_, value.asObj());
            }
            if (stackIndex_ < owner_->stack.size()) {
                owner_->stack[stackIndex_] = value;
            }
        } else {
            closed_ = value;
        }
    }
}

void UpvalueObject::close(const std::vector<Value>& currentStack) {
    if (!isClosed_) {
        // Capture value from owner's stack
        if (owner_) {
            if (stackIndex_ < owner_->stack.size()) {
                closed_ = owner_->stack[stackIndex_];
            }
            owner_ = nullptr; // No longer need owner once closed
        } else if (stackIndex_ < currentStack.size()) {
            closed_ = currentStack[stackIndex_];
        }
        if (VM::currentVM) VM::currentVM->writeBarrier(this, closed_);
        isClosed_ = true;
    }
}

void UpvalueObject::markReferences() {
    // Handled in blackenObject
}
