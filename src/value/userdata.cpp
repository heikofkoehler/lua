#include "value/userdata.hpp"
#include "vm/vm.hpp"

void UserdataObject::setMetatable(const Value& mt) {
    if (mt.isObj() && VM::currentVM) {
        VM::currentVM->writeBarrier(this, mt.asObj());
    }
    metatable_ = mt;
}

void UserdataObject::markReferences() {
    // Handled in blackenObject
}
