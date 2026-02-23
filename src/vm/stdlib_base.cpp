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

bool native_tostring(VM* vm, int argCount) {
    if (argCount != 1) {
        vm->runtimeError("tostring expects 1 argument");
        return false;
    }
    Value val = vm->pop();
    
    // Check for __tostring metamethod
    Value meta = vm->getMetamethod(val, "__tostring");
    if (!meta.isNil()) {
        vm->push(meta);
        vm->push(val);
        vm->callValue(1, 1);
        return true;
    }
    
    // Default conversion
    std::string str = vm->getStringValue(val);
    size_t strIdx = vm->internString(str);
    vm->push(Value::runtimeString(strIdx));
    return true;
}

bool native_next(VM* vm, int argCount) {
    if (argCount < 1) {
        vm->runtimeError("next expects at least 1 argument");
        return false;
    }
    Value table = vm->peek(argCount - 1); // First arg
    Value key = Value::nil();
    if (argCount >= 2) {
        key = vm->peek(argCount - 2); // Second arg
    }

    if (!table.isTable()) {
        vm->runtimeError("bad argument #1 to 'next' (table expected)");
        return false;
    }

    // Pop args
    for(int i=0; i<argCount; i++) vm->pop();

    TableObject* t = vm->getTable(table.asTableIndex());
    std::pair<Value, Value> nextPair = t->next(key);

    if (nextPair.first.isNil()) {
        vm->push(Value::nil());
    } else {
        vm->push(nextPair.first);
        vm->push(nextPair.second);
    }
    return true;
}

bool native_pairs(VM* vm, int argCount) {
    if (argCount != 1) {
        vm->runtimeError("pairs expects 1 argument");
        return false;
    }
    Value table = vm->peek(0);
    if (!table.isTable()) {
        vm->runtimeError("bad argument #1 to 'pairs' (table expected)");
        return false;
    }
    
    // Look up 'next' in globals (simplification, real Lua might use upvalue)
    auto it = vm->globals().find("next");
    if (it == vm->globals().end()) {
        vm->runtimeError("global 'next' not found");
        return false;
    }
    
    // Pop argument
    vm->pop();
    
    vm->push(it->second); // next
    vm->push(table);      // table
    vm->push(Value::nil()); // initial key
    return true;
}

bool native_ipairs_iter(VM* vm, int argCount) {
    // Iterator function for ipairs: f(t, i) -> i+1, t[i+1]
    if (argCount != 2) {
        vm->runtimeError("ipairs iterator expects 2 arguments");
        return false;
    }
    Value index = vm->pop();
    Value table = vm->pop();
    
    if (!index.isNumber()) {
        vm->runtimeError("ipairs iterator expects number index");
        return false;
    }
    
    double i = index.asNumber();
    i += 1;
    
    if (table.isTable()) {
        TableObject* t = vm->getTable(table.asTableIndex());
        Value val = t->get(Value::number(i));
        
        if (val.isNil()) {
            vm->push(Value::nil());
        } else {
            vm->push(Value::number(i));
            vm->push(val);
        }
    } else {
        vm->push(Value::nil());
    }
    return true;
}

bool native_ipairs(VM* vm, int argCount) {
    if (argCount != 1) {
        vm->runtimeError("ipairs expects 1 argument");
        return false;
    }
    Value table = vm->peek(0);
    if (!table.isTable()) {
        vm->runtimeError("bad argument #1 to 'ipairs' (table expected)");
        return false;
    }
    
    // Create ipairs iterator if not already registered/available?
    // We need to push the native function native_ipairs_iter.
    // It's not a global, so we need to register it once or look it up.
    // Or we can register it as a hidden native function.
    // Simpler: Register it as a global "__ipairs_iter" (hidden convention) or just create a new function value each time?
    // Creating a new native function value is cheap (just an index).
    // But we need to register the function pointer in the VM.
    // We can register it once in initStandardLibrary and store its index?
    // But stdlib_base.cpp doesn't have state.
    // We can look up "__ipairs_iter" in globals if we register it there.
    
    auto it = vm->globals().find("__ipairs_iter");
    if (it == vm->globals().end()) {
        vm->runtimeError("internal error: __ipairs_iter not found");
        return false;
    }
    
    // Pop argument
    vm->pop();
    
    vm->push(it->second); // iter
    vm->push(table);      // table
    vm->push(Value::number(0)); // initial index
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
    
    size_t tostringIdx = vm->registerNativeFunction("tostring", native_tostring);
    vm->globals()["tostring"] = Value::nativeFunction(tostringIdx);
    
    size_t typeIdx = vm->registerNativeFunction("type", [](VM* vm, int argCount) -> bool {
        if (argCount != 1) {
            vm->runtimeError("type expects 1 argument");
            return false;
        }
        Value val = vm->pop();
        std::string typeName = val.typeToString();
        size_t strIdx = vm->internString(typeName);
        vm->push(Value::runtimeString(strIdx));
        return true;
    });
    vm->globals()["type"] = Value::nativeFunction(typeIdx);
    
    size_t nextIdx = vm->registerNativeFunction("next", native_next);
    vm->globals()["next"] = Value::nativeFunction(nextIdx);
    
    size_t pairsIdx = vm->registerNativeFunction("pairs", native_pairs);
    vm->globals()["pairs"] = Value::nativeFunction(pairsIdx);
    
    size_t ipairsIterIdx = vm->registerNativeFunction("__ipairs_iter", native_ipairs_iter);
    vm->globals()["__ipairs_iter"] = Value::nativeFunction(ipairsIterIdx);
    
    size_t ipairsIdx = vm->registerNativeFunction("ipairs", native_ipairs);
    vm->globals()["ipairs"] = Value::nativeFunction(ipairsIdx);
}
