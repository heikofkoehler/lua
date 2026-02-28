#include "vm/vm.hpp"
#include <ctime>
#include <chrono>
#include <iostream>
#include <cstdlib>
#include <cstdio>

namespace {

bool native_os_clock(VM* vm, int argCount) {
    auto now = std::chrono::steady_clock::now();
    auto duration = now.time_since_epoch();
    double seconds = std::chrono::duration<double>(duration).count();
    
    for(int i=0; i<argCount; i++) vm->pop();
    vm->push(Value::number(seconds));
    return true;
}

bool native_os_time(VM* vm, int argCount) {
    if (argCount == 0) {
        vm->push(Value::number(static_cast<double>(std::time(nullptr))));
        return true;
    }
    // TODO: Support table argument for specific time
    for(int i=0; i<argCount; i++) vm->pop();
    vm->push(Value::number(static_cast<double>(std::time(nullptr))));
    return true;
}

bool native_os_difftime(VM* vm, int argCount) {
    if (argCount != 2) {
        vm->runtimeError("os.difftime expects 2 arguments");
        return false;
    }
    Value v2 = vm->peek(0);
    Value v1 = vm->peek(1);
    if (!v1.isNumber() || !v2.isNumber()) {
        vm->runtimeError("os.difftime expects number arguments");
        return false;
    }
    double diff = v1.asNumber() - v2.asNumber();
    
    for(int i=0; i<argCount; i++) vm->pop();
    vm->push(Value::number(diff));
    return true;
}

bool native_os_exit(VM* vm, int argCount) {
    int code = 0;
    if (argCount >= 1) {
        Value val = vm->peek(argCount - 1);
        if (val.isBool()) code = val.asBool() ? 0 : 1;
        else if (val.isNumber()) code = static_cast<int>(val.asNumber());
    }
    std::exit(code);
    return true;
}

bool native_os_getenv(VM* vm, int argCount) {
    if (argCount != 1) {
        vm->runtimeError("os.getenv expects 1 argument");
        return false;
    }
    Value var = vm->peek(0);
    const char* val = std::getenv(vm->getStringValue(var).c_str());
    
    for(int i=0; i<argCount; i++) vm->pop();
    
    if (val) {
        vm->push(Value::runtimeString(vm->internString(val)));
    } else {
        vm->push(Value::nil());
    }
    return true;
}

bool native_os_remove(VM* vm, int argCount) {
    if (argCount != 1) {
        vm->runtimeError("os.remove expects 1 argument");
        return false;
    }
    Value filename = vm->peek(0);
    int res = std::remove(vm->getStringValue(filename).c_str());
    
    for(int i=0; i<argCount; i++) vm->pop();
    
    if (res == 0) {
        vm->push(Value::boolean(true));
    } else {
        vm->push(Value::nil());
        vm->push(Value::runtimeString(vm->internString("could not remove file")));
    }
    return true;
}

bool native_os_rename(VM* vm, int argCount) {
    if (argCount != 2) {
        vm->runtimeError("os.rename expects 2 arguments");
        return false;
    }
    Value newname = vm->peek(0);
    Value oldname = vm->peek(1);
    int res = std::rename(vm->getStringValue(oldname).c_str(), vm->getStringValue(newname).c_str());
    
    for(int i=0; i<argCount; i++) vm->pop();
    
    if (res == 0) {
        vm->push(Value::boolean(true));
    } else {
        vm->push(Value::nil());
        vm->push(Value::runtimeString(vm->internString("could not rename file")));
    }
    return true;
}

bool native_os_setlocale(VM* vm, int argCount) {
    std::string locale = "C";
    if (argCount >= 1) {
        Value v = vm->peek(argCount - 1);
        if (v.isString()) locale = vm->getStringValue(v);
    }
    const char* res = std::setlocale(LC_ALL, locale.c_str());
    
    Value result = Value::nil();
    if (res) {
        result = Value::runtimeString(vm->internString(res));
    }
    
    for(int i=0; i<argCount; i++) vm->pop();
    vm->push(result);
    return true;
}

} // anonymous namespace

void registerOSLibrary(VM* vm, TableObject* osTable) {
    vm->addNativeToTable(osTable, "clock", native_os_clock);
    vm->addNativeToTable(osTable, "time", native_os_time);
    vm->addNativeToTable(osTable, "difftime", native_os_difftime);
    vm->addNativeToTable(osTable, "exit", native_os_exit);
    vm->addNativeToTable(osTable, "getenv", native_os_getenv);
    vm->addNativeToTable(osTable, "remove", native_os_remove);
    vm->addNativeToTable(osTable, "rename", native_os_rename);
    vm->addNativeToTable(osTable, "setlocale", native_os_setlocale);
}
