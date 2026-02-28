#include "vm/vm.hpp"

namespace {

bool native_debug_sethook(VM* vm, int argCount) {
    // Stub implementation to satisfy all.lua
    // debug.sethook([thread,] hook, mask [, count])
    
    // For now, we don't actually implement hooking in the VM.
    // We just consume the arguments and return nothing (nil).
    
    for (int i = 0; i < argCount; i++) {
        vm->pop();
    }
    
    vm->push(Value::nil());
    return true;
}

bool native_debug_setmetatable(VM* vm, int argCount) {
    if (argCount != 2) {
        vm->runtimeError("debug.setmetatable expects 2 arguments");
        return false;
    }
    Value mt = vm->peek(0);
    Value obj = vm->peek(1);

    if (obj.isTable()) {
        obj.asTableObj()->setMetatable(mt);
    } else {
        vm->setTypeMetatable(obj.type(), mt);
    }
    
    for(int i=0; i<argCount; i++) vm->pop();
    vm->push(obj);
    return true;
}

} // anonymous namespace

void registerDebugLibrary(VM* vm, TableObject* debugTable) {
    vm->addNativeToTable(debugTable, "sethook", native_debug_sethook);
    vm->addNativeToTable(debugTable, "setmetatable", native_debug_setmetatable);
}
