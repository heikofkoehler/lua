#include "vm/vm.hpp"
#include <iostream>
#include <thread>
#include <chrono>

namespace {

bool native_print(VM* vm, int argCount) {
    for (int i = 0; i < argCount; i++) {
        // Peek from bottom of the call arguments
        // stack: [func, arg0, arg1, ..., argN]
        // peek(argCount - 1 - i)
        Value val = vm->peek(argCount - 1 - i);
        std::cout << val.toString();
        if (i < argCount - 1) std::cout << "\t";
    }
    std::cout << std::endl;

    // Pop arguments
    for (int i = 0; i < argCount; i++) vm->pop();

    vm->push(Value::nil());
    return true;
}

bool native_sleep(VM* vm, int argCount) {
    if (argCount != 1) {
        vm->runtimeError("sleep expects 1 argument");
        return false;
    }
    Value val = vm->pop();
    if (!val.isNumber()) {
        vm->runtimeError("sleep expects number argument (seconds)");
        return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(val.asNumber() * 1000)));
    vm->push(Value::nil());
    return true;
}

bool native_collectgarbage(VM* vm, int /* argCount */) {
    // collectgarbage() - run garbage collector
    vm->collectGarbage();
    vm->push(Value::nil());
    return true;
}

bool native_setmetatable(VM* vm, int argCount) {
    if (argCount != 2) {
        vm->runtimeError("setmetatable expects 2 arguments");
        return false;
    }
    Value metatable = vm->pop();
    Value table = vm->pop();

    if (!table.isTable()) {
        vm->runtimeError("bad argument #1 to 'setmetatable' (table expected)");
        return false;
    }

    if (!metatable.isNil() && !metatable.isTable()) {
        vm->runtimeError("bad argument #2 to 'setmetatable' (nil or table expected)");
        return false;
    }

    TableObject* t = vm->getTable(table.asTableIndex());
    t->setMetatable(metatable);
    vm->push(table);
    return true;
}

bool native_getmetatable(VM* vm, int argCount) {
    if (argCount != 1) {
        vm->runtimeError("getmetatable expects 1 argument");
        return false;
    }
    Value obj = vm->pop();

    if (obj.isTable()) {
        TableObject* t = vm->getTable(obj.asTableIndex());
        vm->push(t->getMetatable());
    } else {
        vm->push(Value::nil());
    }
    return true;
}

} // anonymous namespace

void registerBaseLibrary(VM* vm) {
    // Register collectgarbage as a global function
    size_t gcIdx = vm->registerNativeFunction("collectgarbage", native_collectgarbage);
    vm->globals()["collectgarbage"] = Value::nativeFunction(gcIdx);

    size_t printIdx = vm->registerNativeFunction("print", native_print);
    vm->globals()["print"] = Value::nativeFunction(printIdx);

    size_t sleepIdx = vm->registerNativeFunction("sleep", native_sleep);
    vm->globals()["sleep"] = Value::nativeFunction(sleepIdx);

    size_t setmtIdx = vm->registerNativeFunction("setmetatable", native_setmetatable);
    vm->globals()["setmetatable"] = Value::nativeFunction(setmtIdx);

    size_t getmtIdx = vm->registerNativeFunction("getmetatable", native_getmetatable);
    vm->globals()["getmetatable"] = Value::nativeFunction(getmtIdx);
}
