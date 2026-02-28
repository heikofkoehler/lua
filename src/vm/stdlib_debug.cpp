#include "vm/vm.hpp"
#include "value/userdata.hpp"

namespace {

bool native_debug_sethook(VM* vm, int argCount) {
    CoroutineObject* co = vm->currentCoroutine();
    int argBase = 0;

    if (argCount >= 1 && vm->peek(argCount - 1).isThread()) {
        co = vm->peek(argCount - 1).asThreadObj();
        argBase = 1;
    }

    if (argCount <= argBase) {
        // Reset hook
        co->hook = Value::nil();
        co->hookMask = 0;
        co->hookCount = 0;
        for(int i=0; i<argCount; i++) vm->pop();
        vm->push(Value::nil());
        return true;
    }

    Value hook = vm->peek(argCount - 1 - argBase);
    Value maskVal = (argCount > argBase + 1) ? vm->peek(argCount - 2 - argBase) : Value::runtimeString(vm->internString(""));
    int count = (argCount > argBase + 2) ? static_cast<int>(vm->peek(argCount - 3 - argBase).asNumber()) : 0;

    co->hook = hook;
    co->hookMask = 0;
    if (maskVal.isString()) {
        std::string mask = vm->getStringValue(maskVal);
        if (mask.find('c') != std::string::npos) co->hookMask |= CoroutineObject::MASK_CALL;
        if (mask.find('r') != std::string::npos) co->hookMask |= CoroutineObject::MASK_RET;
        if (mask.find('l') != std::string::npos) co->hookMask |= CoroutineObject::MASK_LINE;
    }
    
    if (count > 0) {
        co->hookMask |= CoroutineObject::MASK_COUNT;
        co->baseHookCount = count;
        co->hookCount = count;
    } else {
        co->hookCount = 0;
    }

    for(int i=0; i<argCount; i++) vm->pop();
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
    } else if (obj.isUserdata()) {
        obj.asUserdataObj()->setMetatable(mt);
    } else {
        vm->setTypeMetatable(obj.type(), mt);
    }
    
    for(int i=0; i<argCount; i++) vm->pop();
    vm->push(obj);
    return true;
}

bool native_debug_getlocal(VM* vm, int argCount) {
    if (argCount < 2) {
        vm->runtimeError("debug.getlocal expects at least 2 arguments");
        return false;
    }
    int index = static_cast<int>(vm->peek(0).asNumber());
    int level = static_cast<int>(vm->peek(1).asNumber());

    CallFrame* frame = vm->getFrame(level);
    if (!frame || !frame->closure) {
        for(int i=0; i<argCount; i++) vm->pop();
        vm->push(Value::nil());
        return true;
    }

    FunctionObject* func = frame->closure->function();
    const auto& locals = func->localVars();

    // Find local with given slot index that is active at current IP
    std::string name = "";
    bool found = false;
    int slot = -1;

    for (const auto& l : locals) {
        if (l.slot == index - 1 && frame->ip >= l.startPC && frame->ip <= l.endPC) {
            name = l.name;
            slot = l.slot;
            found = true;
            break;
        }
    }

    if (!found) {
        // Simple fallback if PC-based lookup fails: just use the slot directly if it's within range
        // This is useful for parameters which might have startPC=0
        for (const auto& l : locals) {
            if (l.slot == index - 1) {
                name = l.name;
                slot = l.slot;
                found = true;
                break;
            }
        }
    }

    for(int i=0; i<argCount; i++) vm->pop();

    if (found) {
        vm->push(Value::runtimeString(vm->internString(name)));
        vm->push(vm->currentCoroutine()->stack[frame->stackBase + slot]);
        vm->currentCoroutine()->lastResultCount = 2;
    } else {
        vm->push(Value::nil());
    }
    return true;
}

bool native_debug_setlocal(VM* vm, int argCount) {
    if (argCount < 3) {
        vm->runtimeError("debug.setlocal expects at least 3 arguments");
        return false;
    }
    Value newValue = vm->peek(0);
    int index = static_cast<int>(vm->peek(1).asNumber());
    int level = static_cast<int>(vm->peek(2).asNumber());

    CallFrame* frame = vm->getFrame(level);
    if (!frame || !frame->closure) {
        for(int i=0; i<argCount; i++) vm->pop();
        vm->push(Value::nil());
        return true;
    }

    FunctionObject* func = frame->closure->function();
    const auto& locals = func->localVars();
    
    std::string name = "";
    int slot = -1;
    bool found = false;

    for (const auto& l : locals) {
        if (l.slot == index - 1) { // Simplified: just use slot index
            name = l.name;
            slot = l.slot;
            found = true;
            break;
        }
    }

    for(int i=0; i<argCount; i++) vm->pop();

    if (found) {
        vm->currentCoroutine()->stack[frame->stackBase + slot] = newValue;
        vm->push(Value::runtimeString(vm->internString(name)));
    } else {
        vm->push(Value::nil());
    }
    return true;
}

} // anonymous namespace

void registerDebugLibrary(VM* vm, TableObject* debugTable) {
    vm->addNativeToTable(debugTable, "sethook", native_debug_sethook);
    vm->addNativeToTable(debugTable, "setmetatable", native_debug_setmetatable);
    vm->addNativeToTable(debugTable, "getlocal", native_debug_getlocal);
    vm->addNativeToTable(debugTable, "setlocal", native_debug_setlocal);
    
    vm->addNativeToTable(debugTable, "traceback", [](VM* vm, int argCount) -> bool {
        // Simple traceback implementation
        std::string result = "stack traceback:\n";
        auto& frames = vm->currentCoroutine()->frames;
        for (int i = static_cast<int>(frames.size()) - 1; i >= 0; i--) {
            const auto& frame = frames[i];
            result += "  ";
            if (frame.closure) {
                result += frame.closure->function()->name();
            } else {
                result += "[C function]";
            }
            result += "\n";
        }
        
        for(int i=0; i<argCount; i++) vm->pop();
        vm->push(Value::runtimeString(vm->internString(result)));
        return true;
    });

    vm->addNativeToTable(debugTable, "getinfo", [](VM* vm, int argCount) -> bool {
        if (argCount < 1) {
            vm->push(Value::nil());
            return true;
        }
        Value f = vm->peek(0);
        TableObject* info = vm->createTable();
        
        if (f.isClosure()) {
            FunctionObject* func = f.asClosureObj()->function();
            info->set("name", Value::runtimeString(vm->internString(func->name())));
            info->set("what", Value::runtimeString(vm->internString("Lua")));
            info->set("nups", Value::number(func->upvalueCount()));
        } else if (f.isNativeFunction()) {
            info->set("what", Value::runtimeString(vm->internString("C")));
        }
        
        for(int i=0; i<argCount; i++) vm->pop();
        vm->push(Value::table(info));
        return true;
    });
}
