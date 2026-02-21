#include "vm/vm.hpp"

namespace {

bool native_collectgarbage(VM* vm, int /* argCount */) {
    // collectgarbage() - run garbage collector
    vm->collectGarbage();
    vm->push(Value::nil());
    return true;
}

} // anonymous namespace

void registerBaseLibrary(VM* vm) {
    // Register collectgarbage as a global function
    size_t funcIndex = vm->registerNativeFunction("collectgarbage", native_collectgarbage);
    vm->globals()["collectgarbage"] = Value::nativeFunction(funcIndex);
}
