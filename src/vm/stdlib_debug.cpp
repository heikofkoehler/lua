#include "vm/vm.hpp"
#include "value/userdata.hpp"
#include "compiler/lexer.hpp"
#include "compiler/parser.hpp"
#include "compiler/codegen.hpp"
#include <iostream>
#include <cstdio>
#include <cstring>

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
        co->baseHookCount = 0;
        for(int i=0; i<argCount; i++) vm->pop();
        vm->push(Value::nil());
        vm->currentCoroutine()->lastResultCount = 1;
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
        if (mask.find('l') != std::string::npos) {
            co->hookMask |= CoroutineObject::MASK_LINE;
            if (co == vm->currentCoroutine()) {
                for (int i = static_cast<int>(co->frames.size()) - 1; i >= 0; i--) {
                    if (!co->frames[i].isC && co->frames[i].chunk) {
                        size_t callIp = co->frames[i].ip >= 3 ? co->frames[i].ip - 3 : 0;
                        co->frames[i].lastLine = co->frames[i].chunk->getLine(callIp);
                        co->frames[i].lastIp = co->frames[i].ip;
                        break;
                    }
                }
            }
        }
    }
    
    if (count > 0) {
        co->hookMask |= CoroutineObject::MASK_COUNT;
        co->baseHookCount = count;
        co->hookCount = count;
    } else {
        co->baseHookCount = 0;
        co->hookCount = 0;
    }

    for(int i=0; i<argCount; i++) vm->pop();
    vm->push(Value::nil());
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_debug_gethook(VM* vm, int argCount) {
    CoroutineObject* co = vm->currentCoroutine();
    if (argCount >= 1) {
        Value val = vm->peek(argCount - 1);
        if (val.isThread()) {
            co = val.asThreadObj();
        }
    }

    std::string maskStr = "";
    if (co->hookMask & CoroutineObject::MASK_CALL) maskStr += "c";
    if (co->hookMask & CoroutineObject::MASK_RET) maskStr += "r";
    if (co->hookMask & CoroutineObject::MASK_LINE) maskStr += "l";

    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(co->hook);
    vm->push(Value::runtimeString(vm->internString(maskStr)));
    vm->push(Value::integer(co->baseHookCount));
    vm->currentCoroutine()->lastResultCount = 3;
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
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

static const char* luaF_getlocalname(FunctionObject* func, int n, size_t pc, int* outSlot = nullptr) {
    for (const auto& l : func->localVars()) {
        if (l.startPC <= pc && pc < l.endPC) {
            n--;
            if (n == 0) {
                if (outSlot) *outSlot = l.slot;
                return l.name.c_str();
            }
        }
    }
    return nullptr;
}

bool native_debug_getlocal(VM* vm, int argCount) {
    CoroutineObject* co = vm->currentCoroutine();
    int argBase = 0;
    if (argCount >= 1 && vm->peek(argCount - 1).isThread()) {
        co = vm->peek(argCount - 1).asThreadObj();
        argBase = 1;
    }
    if (argCount < argBase + 2) {
        vm->runtimeError("bad argument #" + std::to_string(argBase + 1) + " to 'getlocal' (value expected)");
        return false;
    }

    Value f = vm->peek(argCount - 1 - argBase);
    Value indexVal = vm->peek(argCount - 2 - argBase);
    if (!indexVal.isNumber()) {
        vm->runtimeError("bad argument #" + std::to_string(argBase + 2) + " to 'getlocal' (number expected)");
        return false;
    }
    int localIndex = static_cast<int>(indexVal.asNumber());

    // Case 1: f is a function
    if (f.isFunction()) {
        if (f.isClosure()) {
            ClosureObject* cl = f.asClosureObj();
            FunctionObject* func = cl->function();
            int arity = func->arity();
            if (localIndex >= 1 && localIndex <= arity) {
                for (const auto& loc : func->localVars()) {
                    if (loc.slot == localIndex - 1 && loc.startPC == 0) {
                        for (int i = 0; i < argCount; i++) vm->pop();
                        vm->push(Value::runtimeString(vm->internString(loc.name)));
                        vm->currentCoroutine()->lastResultCount = 1;
                        return true;
                    }
                }
            }
        }
        for (int i = 0; i < argCount; i++) vm->pop();
        vm->push(Value::nil());
        vm->currentCoroutine()->lastResultCount = 1;
        return true;
    }

    // Case 2: f is a stack level
    if (!f.isNumber()) {
        vm->runtimeError("bad argument #" + std::to_string(argBase + 1) + " to 'getlocal' (level or function expected)");
        return false;
    }

    int level = static_cast<int>(f.asNumber());
    CallFrame* frame = nullptr;
    size_t stackTop = 0;

    if (level < 0 || static_cast<size_t>(level) >= co->frames.size()) {
        vm->runtimeError("bad argument #" + std::to_string(argBase + 1) + " to 'getlocal' (level out of range)");
        return false;
    }
    size_t idx = co->frames.size() - 1 - level;
    frame = &co->frames[idx];
    if (idx + 1 < co->frames.size()) {
        stackTop = co->frames[idx + 1].stackBase > 0 ? co->frames[idx + 1].stackBase - 1 : 0;
    } else {
        stackTop = co->stack.size();
    }

    // Negative index: vararg
    if (localIndex < 0) {
        int varargIdx = -localIndex - 1;
        if (varargIdx >= 0 && varargIdx < static_cast<int>(frame->varargs.size())) {
            for (int i = 0; i < argCount; i++) vm->pop();
            vm->push(Value::runtimeString(vm->internString("(vararg)")));
            vm->push(frame->varargs[varargIdx]);
            vm->currentCoroutine()->lastResultCount = 2;
            return true;
        }
        for (int i = 0; i < argCount; i++) vm->pop();
        vm->push(Value::nil());
        vm->currentCoroutine()->lastResultCount = 1;
        return true;
    }

    if (localIndex == 0) {
        for (int i = 0; i < argCount; i++) vm->pop();
        vm->push(Value::nil());
        vm->currentCoroutine()->lastResultCount = 1;
        return true;
    }

    // Positive index
    int slot = localIndex - 1;
    if (frame->isC) {
        if (frame->stackBase + slot < stackTop) {
            Value val = co->stack[frame->stackBase + slot];
            for (int i = 0; i < argCount; i++) vm->pop();
            vm->push(Value::runtimeString(vm->internString("(C temporary)")));
            vm->push(val);
            vm->currentCoroutine()->lastResultCount = 2;
            return true;
        }
        for (int i = 0; i < argCount; i++) vm->pop();
        vm->push(Value::nil());
        vm->currentCoroutine()->lastResultCount = 1;
        return true;
    }

    if (frame->closure) {
        FunctionObject* func = frame->closure->function();
        size_t ip = (frame->isInterruptedByHook || frame->ip == 0) ? frame->ip : frame->ip - 1;
        int namedSlot = -1;
        const char* name = luaF_getlocalname(func, localIndex, ip, &namedSlot);
        if (name) {
            slot = namedSlot;
            Value val = (frame->stackBase + slot < co->stack.size()) ? co->stack[frame->stackBase + slot] : Value::nil();
            for (int i = 0; i < argCount; i++) vm->pop();
            vm->push(Value::runtimeString(vm->internString(name)));
            vm->push(val);
            vm->currentCoroutine()->lastResultCount = 2;
            return true;
        }
        // Check temporary
        int maxSlots = 0;
        for (const auto& l : func->localVars()) {
            if (l.slot + 1 > maxSlots) {
                maxSlots = l.slot + 1;
            }
        }
        if (frame->stackBase + slot < stackTop || slot < maxSlots) {
            Value val = (frame->stackBase + slot < stackTop && frame->stackBase + slot < co->stack.size()) 
                        ? co->stack[frame->stackBase + slot] : Value::nil();
            for (int i = 0; i < argCount; i++) vm->pop();
            vm->push(Value::runtimeString(vm->internString("(temporary)")));
            vm->push(val);
            vm->currentCoroutine()->lastResultCount = 2;
            return true;
        }
    }

    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(Value::nil());
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_debug_setlocal(VM* vm, int argCount) {
    CoroutineObject* co = vm->currentCoroutine();
    int argBase = 0;
    if (argCount >= 1 && vm->peek(argCount - 1).isThread()) {
        co = vm->peek(argCount - 1).asThreadObj();
        argBase = 1;
    }
    if (argCount < argBase + 3) {
        vm->runtimeError("bad argument #" + std::to_string(argBase + 1) + " to 'setlocal' (value expected)");
        return false;
    }

    Value levelVal = vm->peek(argCount - 1 - argBase);
    Value indexVal = vm->peek(argCount - 2 - argBase);
    Value newValue = vm->peek(argCount - 3 - argBase);

    if (!levelVal.isNumber() || !indexVal.isNumber()) {
        vm->runtimeError("bad argument to 'setlocal' (number expected)");
        return false;
    }

    int level = static_cast<int>(levelVal.asNumber());
    int localIndex = static_cast<int>(indexVal.asNumber());

    CallFrame* frame = nullptr;
    size_t stackTop = 0;

    if (level < 0 || static_cast<size_t>(level) >= co->frames.size()) {
        vm->runtimeError("bad argument #" + std::to_string(argBase + 1) + " to 'setlocal' (level out of range)");
        return false;
    }
    size_t idx = co->frames.size() - 1 - level;
    frame = &co->frames[idx];
    if (idx + 1 < co->frames.size()) {
        stackTop = co->frames[idx + 1].stackBase > 0 ? co->frames[idx + 1].stackBase - 1 : 0;
    } else {
        stackTop = co->stack.size();
    }

    // Negative index: vararg
    if (localIndex < 0) {
        int varargIdx = -localIndex - 1;
        if (varargIdx >= 0 && varargIdx < static_cast<int>(frame->varargs.size())) {
            frame->varargs[varargIdx] = newValue;
            for (int i = 0; i < argCount; i++) vm->pop();
            vm->push(Value::runtimeString(vm->internString("(vararg)")));
            vm->currentCoroutine()->lastResultCount = 1;
            return true;
        }
        for (int i = 0; i < argCount; i++) vm->pop();
        vm->push(Value::nil());
        vm->currentCoroutine()->lastResultCount = 1;
        return true;
    }

    if (localIndex == 0) {
        for (int i = 0; i < argCount; i++) vm->pop();
        vm->push(Value::nil());
        vm->currentCoroutine()->lastResultCount = 1;
        return true;
    }

    // Positive index
    int slot = localIndex - 1;
    if (frame->isC) {
        if (frame->stackBase + slot < stackTop) {
            co->stack[frame->stackBase + slot] = newValue;
            for (int i = 0; i < argCount; i++) vm->pop();
            vm->push(Value::runtimeString(vm->internString("(C temporary)")));
            vm->currentCoroutine()->lastResultCount = 1;
            return true;
        }
        for (int i = 0; i < argCount; i++) vm->pop();
        vm->push(Value::nil());
        vm->currentCoroutine()->lastResultCount = 1;
        return true;
    }

    if (frame->closure) {
        FunctionObject* func = frame->closure->function();
        size_t ip = (frame->isInterruptedByHook || frame->ip == 0) ? frame->ip : frame->ip - 1;
        int namedSlot = -1;
        const char* name = luaF_getlocalname(func, localIndex, ip, &namedSlot);
        if (name) {
            slot = namedSlot;
            if (frame->stackBase + slot >= co->stack.size()) {
                co->stack.resize(frame->stackBase + slot + 1, Value::nil());
            }
            co->stack[frame->stackBase + slot] = newValue;
            if (newValue.isObj()) vm->writeBarrierBackward(co, newValue.asObj());
            for (int i = 0; i < argCount; i++) vm->pop();
            vm->push(Value::runtimeString(vm->internString(name)));
            vm->currentCoroutine()->lastResultCount = 1;
            return true;
        }
        // Check temporary
        int maxSlots = 0;
        for (const auto& l : func->localVars()) {
            if (l.slot + 1 > maxSlots) {
                maxSlots = l.slot + 1;
            }
        }
        if (frame->stackBase + slot < stackTop || slot < maxSlots) {
            if (frame->stackBase + slot >= co->stack.size()) {
                co->stack.resize(frame->stackBase + slot + 1, Value::nil());
            }
            co->stack[frame->stackBase + slot] = newValue;
            if (newValue.isObj()) vm->writeBarrierBackward(co, newValue.asObj());
            for (int i = 0; i < argCount; i++) vm->pop();
            vm->push(Value::runtimeString(vm->internString("(temporary)")));
            vm->currentCoroutine()->lastResultCount = 1;
            return true;
        }
    }

    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(Value::nil());
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_debug_getupvalue(VM* vm, int argCount) {
    if (argCount != 2) { vm->runtimeError("debug.getupvalue expects 2 arguments"); return false; }
    Value funcVal = vm->peek(1);
    Value indexVal = vm->peek(0);
    
    if (funcVal.isNativeFunction() || funcVal.isCFunction()) {
        for (int i = 0; i < argCount; i++) vm->pop();
        vm->push(Value::nil());
        vm->currentCoroutine()->lastResultCount = 1;
        return true;
    }
    if (!funcVal.isClosure() || !indexVal.isNumber()) {
        vm->runtimeError("debug.getupvalue arguments must be function and number");
        return false;
    }
    
    ClosureObject* closure = funcVal.asClosureObj();
    int index = static_cast<int>(indexVal.asNumber());

    if (closure->isC()) {
        if (index >= 1 && index <= static_cast<int>(closure->upvalueCount())) {
            for (int i = 0; i < argCount; i++) vm->pop();
            vm->push(Value::runtimeString(vm->internString("")));
            vm->push(closure->getCUpvalue(index - 1));
            vm->currentCoroutine()->lastResultCount = 2;
            return true;
        } else {
            for (int i = 0; i < argCount; i++) vm->pop();
            vm->push(Value::nil());
            vm->currentCoroutine()->lastResultCount = 1;
            return true;
        }
    }
    
    if (index >= 1 && index <= static_cast<int>(closure->upvalueCount())) {
        UpvalueObject* upvalue = closure->getUpvalueObj(index - 1);
        std::string name;
        if (closure->function() && static_cast<size_t>(index - 1) < closure->function()->upvalueNames().size()) {
            name = closure->function()->getUpvalueName(index - 1);
        }
        if (name.empty()) {
            name = "(no name)";
        }
        
        vm->pop(); vm->pop();
        vm->push(Value::runtimeString(vm->internString(name)));
        vm->push(upvalue->get(vm->currentCoroutine()->stack));
        vm->currentCoroutine()->lastResultCount = 2;
    } else {
        vm->pop(); vm->pop();
        vm->push(Value::nil());
        vm->currentCoroutine()->lastResultCount = 1;
    }
    return true;
}

bool native_debug_setupvalue(VM* vm, int argCount) {
    if (argCount != 3) { vm->runtimeError("debug.setupvalue expects 3 arguments"); return false; }
    Value funcVal = vm->peek(2);
    Value indexVal = vm->peek(1);
    Value val = vm->peek(0);
    
    if (funcVal.isNativeFunction() || funcVal.isCFunction()) {
        for (int i = 0; i < argCount; i++) vm->pop();
        vm->push(Value::nil());
        vm->currentCoroutine()->lastResultCount = 1;
        return true;
    }
    if (!funcVal.isClosure() || !indexVal.isNumber()) {
        vm->runtimeError("debug.setupvalue arguments must be function, number, value");
        return false;
    }
    
    ClosureObject* closure = funcVal.asClosureObj();
    int index = static_cast<int>(indexVal.asNumber());

    if (closure->isC()) {
        if (index >= 1 && index <= static_cast<int>(closure->upvalueCount())) {
            closure->setCUpvalue(index - 1, val);
            for (int i = 0; i < argCount; i++) vm->pop();
            vm->push(Value::runtimeString(vm->internString("")));
            vm->currentCoroutine()->lastResultCount = 1;
            return true;
        } else {
            for (int i = 0; i < argCount; i++) vm->pop();
            vm->push(Value::nil());
            vm->currentCoroutine()->lastResultCount = 1;
            return true;
        }
    }
    
    if (index >= 1 && index <= static_cast<int>(closure->upvalueCount())) {
        UpvalueObject* upvalue = closure->getUpvalueObj(index - 1);
        std::string name;
        if (closure->function() && static_cast<size_t>(index - 1) < closure->function()->upvalueNames().size()) {
            name = closure->function()->getUpvalueName(index - 1);
        }
        if (name.empty()) {
            name = "(no name)";
        }
        upvalue->set(vm->currentCoroutine()->stack, val);
        
        vm->pop(); vm->pop(); vm->pop();
        vm->push(Value::runtimeString(vm->internString(name)));
    } else {
        vm->pop(); vm->pop(); vm->pop();
        vm->push(Value::nil());
    }
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_debug_upvalueid(VM* vm, int argCount) {
    if (argCount != 2) { vm->runtimeError("debug.upvalueid expects 2 arguments"); return false; }
    Value funcVal = vm->peek(1);
    Value indexVal = vm->peek(0);
    
    if (funcVal.isNativeFunction() || funcVal.isCFunction()) {
        for (int i = 0; i < argCount; i++) vm->pop();
        vm->push(Value::nil());
        vm->currentCoroutine()->lastResultCount = 1;
        return true;
    }
    if (!funcVal.isClosure() || !indexVal.isNumber()) {
        vm->runtimeError("debug.upvalueid arguments must be function and number");
        return false;
    }
    
    ClosureObject* closure = funcVal.asClosureObj();
    int index = static_cast<int>(indexVal.asNumber());
    
    if (index >= 1 && index <= static_cast<int>(closure->upvalueCount())) {
        void* id = closure->isC() ? (void*)closure->getCUpvaluePtr(index - 1)
                                  : (void*)closure->getUpvalueObj(index - 1);
        
        vm->pop(); vm->pop();
        // Return memory address as a light userdata
        UserdataObject* ud = vm->createUserdata(id, 0, true);
        vm->push(Value::userdata(ud));
        vm->currentCoroutine()->lastResultCount = 1;
    } else {
        // Out-of-range: return nil (reference Lua returns nil, not an error)
        vm->pop(); vm->pop();
        vm->push(Value::nil());
        vm->currentCoroutine()->lastResultCount = 1;
    }
    return true;
}

bool native_debug_upvaluejoin(VM* vm, int argCount) {
    if (argCount != 4) { vm->runtimeError("debug.upvaluejoin expects 4 arguments"); return false; }
    Value f1Val = vm->peek(3);
    Value n1Val = vm->peek(2);
    Value f2Val = vm->peek(1);
    Value n2Val = vm->peek(0);
    
    if (!f1Val.isClosure() || !n1Val.isNumber() || !f2Val.isClosure() || !n2Val.isNumber()) {
        vm->runtimeError("debug.upvaluejoin arguments must be function, number, function, number");
        return false;
    }
    
    ClosureObject* f1 = f1Val.asClosureObj();
    int n1 = static_cast<int>(n1Val.asNumber());
    ClosureObject* f2 = f2Val.asClosureObj();
    int n2 = static_cast<int>(n2Val.asNumber());
    
    if (n1 < 1 || n1 > static_cast<int>(f1->upvalueCount()) || 
        n2 < 1 || n2 > static_cast<int>(f2->upvalueCount())) {
        vm->runtimeError("invalid upvalue index");
        return false;
    }
    
    f1->setUpvalue(n1 - 1, f2->getUpvalueObj(n2 - 1));
    
    for(int i=0; i<4; i++) vm->pop();
    vm->push(Value::nil());
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_debug_getregistry(VM* vm, int argCount) {
    for(int i=0; i<argCount; i++) vm->pop();
    vm->push(Value::table(vm->registryTable()));
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_debug_getmetatable(VM* vm, int argCount) {
    if (argCount != 1) { vm->runtimeError("debug.getmetatable expects 1 argument"); return false; }
    Value obj = vm->peek(0);
    for(int i=0; i<argCount; i++) vm->pop();

    Value mt = Value::nil();
    if (obj.isTable()) {
        mt = obj.asTableObj()->getMetatable();
    } else if (obj.isUserdata()) {
        mt = obj.asUserdataObj()->metatable();
    } else {
        mt = vm->getTypeMetatable(obj.type());
    }
    vm->push(mt);
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_debug_getuservalue(VM* vm, int argCount) {
    if (argCount < 1) { vm->runtimeError("debug.getuservalue expects at least 1 argument"); return false; }
    Value udVal = vm->peek(argCount - 1);
    if (udVal.isFile()) {
        for (int i = 0; i < argCount; i++) vm->pop();
        vm->push(Value::nil());
        vm->push(Value::boolean(false));
        vm->currentCoroutine()->lastResultCount = 2;
        return true;
    }
    if (!udVal.isUserdata() || udVal.asUserdataObj()->isLight()) {
        for (int i = 0; i < argCount; i++) vm->pop();
        vm->push(Value::nil());
        vm->push(Value::boolean(false));
        vm->currentCoroutine()->lastResultCount = 2;
        return true;
    }
    UserdataObject* ud = udVal.asUserdataObj();
    int n = 1;
    if (argCount >= 2) {
        n = static_cast<int>(vm->peek(argCount - 2).asNumber());
    }
    
    for (int i = 0; i < argCount; i++) vm->pop();
    
    if (n >= 1 && n <= ud->numUserValues()) {
        vm->push(ud->getUserValue(n - 1));
        vm->push(Value::boolean(true));
    } else {
        vm->push(Value::nil());
        vm->push(Value::boolean(false));
    }
    vm->currentCoroutine()->lastResultCount = 2;
    return true;
}

bool native_debug_setuservalue(VM* vm, int argCount) {
    if (argCount < 2) { vm->runtimeError("debug.setuservalue expects at least 2 arguments"); return false; }
    Value udVal = vm->peek(argCount - 1);
    Value val = vm->peek(argCount - 2);
    int n = 1;
    if (argCount >= 3) {
        n = static_cast<int>(vm->peek(argCount - 3).asNumber());
    }
    
    if (udVal.isFile()) {
        for (int i = 0; i < argCount; i++) vm->pop();
        vm->push(Value::nil());
        vm->currentCoroutine()->lastResultCount = 1;
        return true;
    }
    if (!udVal.isUserdata() || udVal.asUserdataObj()->isLight()) {
        vm->runtimeError("bad argument #1 to 'setuservalue' (userdata expected, got " + vm->typeName(udVal) + ")");
        return false;
    }
    UserdataObject* ud = udVal.asUserdataObj();
    
    if (n >= 1 && n <= ud->numUserValues()) {
        ud->setUserValue(n - 1, val);
        for (int i = 0; i < argCount; i++) vm->pop();
        vm->push(udVal);
    } else {
        for (int i = 0; i < argCount; i++) vm->pop();
        vm->push(Value::nil()); // out of bounds returns nil in Lua 5.4, or error depending on version. Lua 5.4 says: returns u or nil.
    }
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

} // anonymous namespace

void registerDebugLibrary(VM* vm, TableObject* debugTable) {
    TableObject* hookTable = vm->createTable();
    TableObject* hookMt = vm->createTable();
    hookMt->set("__mode", Value::runtimeString(vm->internString("k")));
    hookTable->setMetatable(Value::table(hookMt));
    vm->setRegistry("_HOOKKEY", Value::table(hookTable));

    vm->addNativeToTable(debugTable, "sethook", native_debug_sethook);
    vm->addNativeToTable(debugTable, "gethook", native_debug_gethook);
    vm->addNativeToTable(debugTable, "getmetatable", native_debug_getmetatable);
    vm->addNativeToTable(debugTable, "setmetatable", native_debug_setmetatable);
    vm->addNativeToTable(debugTable, "getuservalue", native_debug_getuservalue);
    vm->addNativeToTable(debugTable, "setuservalue", native_debug_setuservalue);
    vm->addNativeToTable(debugTable, "getlocal", native_debug_getlocal);
    vm->addNativeToTable(debugTable, "setlocal", native_debug_setlocal);
    vm->addNativeToTable(debugTable, "getupvalue", native_debug_getupvalue);
    vm->addNativeToTable(debugTable, "setupvalue", native_debug_setupvalue);
    vm->addNativeToTable(debugTable, "upvalueid", native_debug_upvalueid);
    vm->addNativeToTable(debugTable, "upvaluejoin", native_debug_upvaluejoin);
    vm->addNativeToTable(debugTable, "getregistry", native_debug_getregistry);
    
    vm->addNativeToTable(debugTable, "traceback", [](VM* vm, int argCount) -> bool {
        CoroutineObject* co = vm->currentCoroutine();
        int argBase = 0;
        if (argCount >= 1 && vm->peek(argCount - 1).isThread()) {
            co = vm->peek(argCount - 1).asThreadObj();
            argBase = 1;
        }

        bool hasMessage = false;
        std::string message = "";
        if (argCount > argBase) {
            Value msgVal = vm->peek(argCount - 1 - argBase);
            if (!msgVal.isNil()) {
                if (!msgVal.isString() && !msgVal.isNumber()) {
                    for (int i = 0; i < argCount; i++) vm->pop();
                    vm->push(msgVal);
                    vm->currentCoroutine()->lastResultCount = 1;
                    return true;
                }
                message = msgVal.isString() ? vm->getStringValue(msgVal) : msgVal.toString();
                hasMessage = true;
            }
        }

        int defaultLevel = (co == vm->currentCoroutine()) ? 1 : 0;
        int level = defaultLevel;
        if (argCount > argBase + 1 && vm->peek(argCount - 2 - argBase).isNumber()) {
            level = static_cast<int>(vm->peek(argCount - 2 - argBase).asNumber());
        }

        std::string result = "";
        if (hasMessage) result += message + "\n";
        result += "stack traceback:";

        int start = static_cast<int>(co->frames.size()) - 1 - level;
        int totalFrames = start + 1;
        const int LEVELS1 = 10;
        const int LEVELS2 = 11;
        bool hasSkipped = false;

        for (int i = start; i >= 0; i--) {
            if (totalFrames > LEVELS1 + LEVELS2 && !hasSkipped && i == start - LEVELS1) {
                int numSkipped = totalFrames - (LEVELS1 + LEVELS2);
                result += "\n\t...\t(skipping " + std::to_string(numSkipped) + " levels)";
                i = LEVELS2 - 1;
                hasSkipped = true;
            }
            const auto& frame = co->frames[i];
            result += "\n\t";
            if (frame.closure) {
                FunctionObject* func = frame.closure->function();
                std::string source = formatChunkId(func->chunk()->sourceName());
                int line = -1;
                if (frame.ip > 0 || frame.isInterruptedByHook) {
                    size_t ip = (frame.isInterruptedByHook || frame.ip == 0) ? frame.ip : frame.ip - 1;
                    line = func->chunk()->getLine(ip);
                }

                result += source + ":";
                if (line > 0) {
                    result += std::to_string(line) + ":";
                }
                result += " in ";
                if (frame.isHook) {
                    result += "hook";
                } else if (!frame.metamethodName.empty()) {
                    result += "metamethod '" + frame.metamethodName + "'";
                } else if (frame.isCloseMetamethod) {
                    result += "metamethod 'close'";
                } else {
                    std::string gname = "";
                    if (frame.closure) {
                        gname = vm->findGlobalFuncName(Value::closure(frame.closure));
                    }
                    VM::CallingFuncInfo cinfo = vm->getFrameFuncInfo(i, co);
                    if (!gname.empty()) {
                        result += "function '" + gname + "'";
                    } else if (!cinfo.namewhat.empty() && !cinfo.name.empty()) {
                        result += cinfo.namewhat + " '" + cinfo.name + "'";
                    } else if (func->lineDefined() == 0) {
                        result += "main chunk";
                    } else {
                        result += "function <" + source + ":" + std::to_string(func->lineDefined()) + ">";
                    }
                }
            } else {
                result += "[C]: in ";
                std::string cname = "";
                if (!frame.cFunc.isNil()) {
                    if (frame.cFunc.isNativeFunction()) {
                        cname = vm->getNativeFunctionName(frame.cFunc.asNativeFunctionIndex());
                    }
                    if (cname.empty()) {
                        cname = vm->findGlobalFuncName(frame.cFunc);
                    }
                }
                if (!cname.empty()) {
                    result += "function '" + cname + "'";
                } else {
                    result += "?";
                }
            }
            if (frame.isTailCall) {
                result += "\n\t(...tail calls...)";
            }
        }

        for (int i = 0; i < argCount; i++) vm->pop();
        vm->push(Value::runtimeString(vm->internString(result)));
        vm->currentCoroutine()->lastResultCount = 1;
        return true;
    });

    vm->addNativeToTable(debugTable, "getinfo", [](VM* vm, int argCount) -> bool {
        CoroutineObject* co = vm->currentCoroutine();
        int argBase = 0;
        if (argCount >= 1 && vm->peek(argCount - 1).isThread()) {
            co = vm->peek(argCount - 1).asThreadObj();
            argBase = 1;
        }
        if (argCount <= argBase) {
            vm->push(Value::nil());
            vm->currentCoroutine()->lastResultCount = 1;
            return true;
        }

        Value f = vm->peek(argCount - 1 - argBase);
        std::string what = "flnSurt";
        if (argCount > argBase + 1) {
            Value whatVal = vm->peek(argCount - 2 - argBase);
            if (!whatVal.isNil()) {
                if (!whatVal.isString()) {
                    vm->runtimeError("bad argument #" + std::to_string(argBase + 2) + " to 'getinfo' (string expected, got " + whatVal.typeToString() + ")");
                    return false;
                }
                what = vm->getStringValue(whatVal);
            }
        }

        for (char c : what) {
            switch (c) {
                case 'S':
                case 'l':
                case 'u':
                case 't':
                case 'r':
                case 'n':
                case 'L':
                case 'f':
                    break;
                default:
                    vm->runtimeError("bad argument #" + std::to_string(argBase + 2) + " to 'getinfo' (invalid option)");
                    return false;
            }
        }

        CallFrame* targetFrame = nullptr;
        int targetFrameIndex = -1;
        ClosureObject* closure = nullptr;
        Value cFunc = Value::nil();

        if (f.isNumber()) {
            int level = static_cast<int>(f.asNumber());
            if (level < 0 || static_cast<size_t>(level) >= co->frames.size()) {
                for (int i = 0; i < argCount; i++) vm->pop();
                vm->push(Value::nil());
                vm->currentCoroutine()->lastResultCount = 1;
                return true;
            }
            targetFrameIndex = static_cast<int>(co->frames.size()) - 1 - level;
            targetFrame = &co->frames[targetFrameIndex];
            if (targetFrame->isC) {
                cFunc = targetFrame->cFunc;
            } else {
                closure = targetFrame->closure;
            }
        } else if (f.isClosure()) {
            closure = f.asClosureObj();
        } else if (f.isNativeFunction() || f.isCFunction()) {
            cFunc = f;
        } else {
            vm->runtimeError("bad argument #" + std::to_string(argBase + 1) + " to 'getinfo' (function or level expected)");
            return false;
        }

        TableObject* info = vm->createTable();

        if (what.find('S') != std::string::npos) {
            if (closure && !closure->isC()) {
                FunctionObject* func = closure->function();
                std::string source = func->chunk()->sourceName();
                info->set("source", Value::runtimeString(vm->internString(source)));
                info->set("short_src", Value::runtimeString(vm->internString(formatChunkId(source))));
                info->set("linedefined", Value::integer(func->lineDefined()));
                info->set("lastlinedefined", Value::integer(func->lastLineDefined()));
                if (func->lineDefined() == 0) {
                    info->set("what", Value::runtimeString(vm->internString("main")));
                } else {
                    info->set("what", Value::runtimeString(vm->internString("Lua")));
                }
            } else {
                info->set("what", Value::runtimeString(vm->internString("C")));
                info->set("source", Value::runtimeString(vm->internString("=[C]")));
                info->set("short_src", Value::runtimeString(vm->internString("[C]")));
                info->set("linedefined", Value::integer(-1));
                info->set("lastlinedefined", Value::integer(-1));
            }
        }

        if (what.find('l') != std::string::npos) {
            if (targetFrame && closure && !closure->isC()) {
                size_t ip = (targetFrame->isInterruptedByHook || targetFrame->ip == 0) ? targetFrame->ip : targetFrame->ip - 1;
                int line = closure->function()->chunk()->getLine(ip);
                info->set("currentline", Value::integer(line));
            } else {
                info->set("currentline", Value::integer(-1));
            }
        }

        if (what.find('u') != std::string::npos) {
            if (closure && !closure->isC()) {
                FunctionObject* func = closure->function();
                info->set("nups", Value::integer(func->upvalueCount()));
                info->set("nparams", Value::integer(func->arity()));
                info->set("isvararg", Value::boolean(func->hasVarargs()));
            } else if (closure && closure->isC()) {
                info->set("nups", Value::integer(closure->upvalueCount()));
                info->set("nparams", Value::integer(0));
                info->set("isvararg", Value::boolean(true));
            } else {
                info->set("nups", Value::integer(0));
                info->set("nparams", Value::integer(0));
                info->set("isvararg", Value::boolean(true));
            }
        }

        if (what.find('n') != std::string::npos) {
            if (targetFrame) {
                VM::CallingFuncInfo cinfo = vm->getFrameFuncInfo(targetFrameIndex, co);
                if (!cinfo.name.empty()) {
                    info->set("name", Value::runtimeString(vm->internString(cinfo.name)));
                }
                info->set("namewhat", Value::runtimeString(vm->internString(cinfo.namewhat)));
            } else {
                info->set("namewhat", Value::runtimeString(vm->internString("")));
            }
        }

        if (what.find('t') != std::string::npos) {
            info->set("istailcall", Value::boolean(targetFrame ? targetFrame->isTailCall : false));
            info->set("extraargs", Value::integer(targetFrame ? targetFrame->extraargs : 0));
        }

        if (what.find('r') != std::string::npos) {
            info->set("ftransfer", Value::integer(targetFrame ? targetFrame->ftransfer : 0));
            info->set("ntransfer", Value::integer(targetFrame ? targetFrame->ntransfer : 0));
        }

        if (what.find('L') != std::string::npos) {
            if (closure && !closure->isC()) {
                TableObject* activelines = vm->createTable();
                const auto& code = closure->function()->chunk()->code();
                for (size_t ip = 0; ip < code.size(); ip++) {
                    int line = closure->function()->chunk()->getLine(ip);
                    if (line > 0) {
                        activelines->set(Value::integer(line), Value::boolean(true));
                    }
                }
                info->set("activelines", Value::table(activelines));
            }
        }

        if (what.find('f') != std::string::npos) {
            if (targetFrame) {
                if (targetFrame->closure) {
                    info->set("func", Value::closure(targetFrame->closure));
                } else {
                    info->set("func", targetFrame->cFunc);
                }
            } else {
                info->set("func", f);
            }
        }

        for (int i = 0; i < argCount; i++) vm->pop();
        vm->push(Value::table(info));
        vm->currentCoroutine()->lastResultCount = 1;
        return true;
    });

    vm->addNativeToTable(debugTable, "debug", [](VM* vm, int argCount) -> bool {
        for (int i = 0; i < argCount; i++) vm->pop();
        char buffer[1024];
        for (;;) {
            std::cerr << "lua_debug> ";
            std::cerr.flush();
            if (std::fgets(buffer, sizeof(buffer), stdin) == nullptr) {
                break;
            }
            if (std::strcmp(buffer, "cont\n") == 0 || std::strcmp(buffer, "cont\r\n") == 0 || std::strcmp(buffer, "cont") == 0) {
                break;
            }
            std::string source(buffer);
            std::string sourceName = "=(debug command)";
            try {
                Lexer lexer(source);
                lexer.setSourceName(sourceName);
                Parser parser(lexer);
                auto program = parser.parse();
                if (!program) {
                    std::cerr << "parse error\n";
                    std::cerr.flush();
                    continue;
                }
                CodeGenerator codegen;
                auto function = codegen.generate(program.get(), sourceName);
                if (!function) {
                    std::cerr << "codegen error\n";
                    std::cerr.flush();
                    continue;
                }
                FunctionObject* funcPtr = function.get();
                vm->registerFunction(function.release());
                ClosureObject* closure = vm->createClosure(funcPtr);
                vm->setupRootUpvalues(closure, Value::nil());
                vm->push(Value::closure(closure));
                if (!vm->pcall(1)) {
                    Value errVal = vm->pop();
                    vm->pop(); // pop false
                    std::string errStr = errVal.isString() ? vm->getStringValue(errVal) : errVal.toString();
                    std::cerr << errStr << "\n";
                    std::cerr.flush();
                } else {
                    while (vm->currentCoroutine()->lastResultCount > 0) {
                        vm->pop();
                        vm->currentCoroutine()->lastResultCount--;
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << e.what() << "\n";
                std::cerr.flush();
            }
        }
        vm->currentCoroutine()->lastResultCount = 0;
        return true;
    });
}
