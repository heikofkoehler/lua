#include "value/userdata.hpp"
#include "vm/vm.hpp"

void UserdataObject::setMetatable(const Value& mt) {
    if (mt.isObj() && VM::currentVM) {
        VM::currentVM->writeBarrier(this, mt.asObj());
    }
    metatable_ = mt;
}

void UserdataObject::setUserValue(int index, const Value& val) {
    if (index >= 0 && static_cast<size_t>(index) < userValues_.size()) {
        if (val.isObj() && VM::currentVM) {
            VM::currentVM->writeBarrier(this, val.asObj());
        }
        userValues_[index] = val;
    }
}

void UserdataObject::markReferences() {
    // Handled in blackenObject
}
