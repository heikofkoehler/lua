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
    vm->push(Value::number(static_cast<double>(co->baseHookCount)));
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
        vm->currentCoroutine()->lastResultCount = 1;
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
        // Simple fallback if PC-based lookup fails
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
        vm->currentCoroutine()->lastResultCount = 1;
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
        vm->currentCoroutine()->lastResultCount = 1;
        return true;
    }

    FunctionObject* func = frame->closure->function();
    const auto& locals = func->localVars();
    
    std::string name = "";
    int slot = -1;
    bool found = false;

    for (const auto& l : locals) {
        if (l.slot == index - 1) {
            name = l.name;
            slot = l.slot;
            found = true;
            break;
        }
    }

    for(int i=0; i<argCount; i++) vm->pop();

    if (found) {
        if (newValue.isObj()) {
            vm->writeBarrierBackward(vm->currentCoroutine(), newValue.asObj());
        }
        vm->currentCoroutine()->stack[frame->stackBase + slot] = newValue;
        vm->push(Value::runtimeString(vm->internString(name)));
    } else {
        vm->push(Value::nil());
    }
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_debug_getupvalue(VM* vm, int argCount) {
    if (argCount != 2) { vm->runtimeError("debug.getupvalue expects 2 arguments"); return false; }
    Value funcVal = vm->peek(1);
    Value indexVal = vm->peek(0);
    
    if (!funcVal.isClosure() || !indexVal.isNumber()) {
        vm->runtimeError("debug.getupvalue arguments must be function and number");
        return false;
    }
    
    ClosureObject* closure = funcVal.asClosureObj();
    int index = static_cast<int>(indexVal.asNumber());
    
    if (index >= 1 && index <= static_cast<int>(closure->upvalueCount())) {
        UpvalueObject* upvalue = closure->getUpvalueObj(index - 1);
        std::string name;
        if (closure->function() && static_cast<size_t>(index - 1) < closure->function()->upvalueNames().size()) {
            name = closure->function()->getUpvalueName(index - 1);
        }
        if (name.empty()) {
            name = (index == 1) ? "_ENV" : ("upvalue_" + std::to_string(index));
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
    
    if (!funcVal.isClosure() || !indexVal.isNumber()) {
        vm->runtimeError("debug.setupvalue arguments must be function, number, value");
        return false;
    }
    
    ClosureObject* closure = funcVal.asClosureObj();
    int index = static_cast<int>(indexVal.asNumber());
    
    if (index >= 1 && index <= static_cast<int>(closure->upvalueCount())) {
        UpvalueObject* upvalue = closure->getUpvalueObj(index - 1);
        std::string name;
        if (closure->function() && static_cast<size_t>(index - 1) < closure->function()->upvalueNames().size()) {
            name = closure->function()->getUpvalueName(index - 1);
        }
        if (name.empty()) {
            name = (index == 1) ? "_ENV" : ("upvalue_" + std::to_string(index));
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
    
    if (!funcVal.isClosure() || !indexVal.isNumber()) {
        vm->runtimeError("debug.upvalueid arguments must be function and number");
        return false;
    }
    
    ClosureObject* closure = funcVal.asClosureObj();
    int index = static_cast<int>(indexVal.asNumber());
    
    if (index >= 1 && index <= static_cast<int>(closure->upvalueCount())) {
        UpvalueObject* upvalue = closure->getUpvalueObj(index - 1);
        
        vm->pop(); vm->pop();
        // Return memory address as a light userdata or number representation
        vm->push(Value::number(reinterpret_cast<uint64_t>(upvalue)));
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
    
    // We need to return a table representation of the registry.
    // Our VM currently uses std::unordered_map<std::string, Value> registry_;
    // Let's create a Lua table and copy the string-keyed entries.
    TableObject* regTable = vm->createTable();
    for (const auto& pair : vm->getRegistryMap()) {
        regTable->set(Value::runtimeString(vm->internString(pair.first)), pair.second);
    }
    
    vm->push(Value::table(regTable));
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
    if (!udVal.isUserdata()) {
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
    
    if (!udVal.isUserdata()) {
        vm->runtimeError("bad argument #1 to 'setuservalue' (userdata expected)");
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
        std::string message = "";
        int level = 1;
        int argStart = 0;

        if (argCount >= 1 && vm->peek(argCount - 1).isString()) {
            message = vm->getStringValue(vm->peek(argCount - 1));
            argStart = 1;
        }
        if (argCount > argStart && vm->peek(argCount - 1 - argStart).isNumber()) {
            level = static_cast<int>(vm->peek(argCount - 1 - argStart).asNumber());
        }

        std::string result = "";
        if (!message.empty()) result += message + "\n";
        result += "stack traceback:\n";

        auto& frames = vm->currentCoroutine()->frames;
        for (int i = static_cast<int>(frames.size()) - level; i >= 0; i--) {
            const auto& frame = frames[i];
            result += "  ";
            if (frame.closure) {
                FunctionObject* func = frame.closure->function();
                std::string source = func->chunk()->sourceName();
                if (!source.empty() && source[0] == '@') source = source.substr(1);

                int line = -1;
                if (frame.ip > 0) {
                    line = func->chunk()->getLine(frame.ip - 1);
                }

                if (frame.isCloseMetamethod) {
                    result += source + ":" + (line != -1 ? std::to_string(line) : "?") + ": in metamethod 'close'";
                } else {
                    result += source + ":" + (line != -1 ? std::to_string(line) : "?") + ": in function '";
                    result += func->name() + "'";
                }
            } else {
                result += "[C function]: in ?";
            }
            result += "\n";
        }

        for(int i=0; i<argCount; i++) vm->pop();
        vm->push(Value::runtimeString(vm->internString(result)));
        vm->currentCoroutine()->lastResultCount = 1;
        return true;
    });
    vm->addNativeToTable(debugTable, "getinfo", [](VM* vm, int argCount) -> bool {
        if (argCount < 1) {
            vm->push(Value::nil());
            vm->currentCoroutine()->lastResultCount = 1;
            return true;
        }
        Value f = vm->peek(argCount - 1);
        std::string what = (argCount >= 2) ? vm->getStringValue(vm->peek(argCount - 2)) : "flnSu";
        TableObject* info = vm->createTable();

        ClosureObject* closure = nullptr;
        int line = -1;
        std::string source = "=[C]";
        CallFrame* targetFrame = nullptr;

        if (f.isNumber()) {
            int level = static_cast<int>(f.asNumber());
            // level 1 in Lua is the caller of getinfo, which is getFrame(level) in our VM
            targetFrame = vm->getFrame(level);
            if (targetFrame) {
                CallFrame* frame = targetFrame;
                if (frame->isC) {
                    info->set("what", Value::runtimeString(vm->internString("C")));
                    info->set("source", Value::runtimeString(vm->internString("=[C]")));
                    info->set("short_src", Value::runtimeString(vm->internString("[C]")));
                    info->set("currentline", Value::integer(-1));
                    info->set("linedefined", Value::integer(-1));
                    info->set("lastlinedefined", Value::integer(-1));
                    info->set("nups", Value::integer(0));
                    info->set("nparams", Value::integer(0));
                    info->set("isvararg", Value::boolean(true));
                    if (!frame->cFunc.isNil()) {
                        info->set("func", frame->cFunc);
                        std::string cname = "?";
                        if (frame->cFunc.isNativeFunction()) {
                            std::string n = vm->getNativeFunctionName(frame->cFunc.asNativeFunctionIndex());
                            if (!n.empty()) cname = n;
                        }
                        if (cname == "?") {
                            for (const auto& [gname, gval] : vm->globals()) {
                                if (gval == frame->cFunc) {
                                    cname = gname;
                                    break;
                                }
                            }
                        }
                        info->set("name", Value::runtimeString(vm->internString(cname)));
                    } else {
                        info->set("name", Value::runtimeString(vm->internString("?")));
                    }
                    if (what.find('t') != std::string::npos) {
                        info->set("istailcall", Value::boolean(frame->isTailCall));
                        info->set("extraargs", Value::integer(frame->extraargs));
                    }
                    for(int i=0; i<argCount; i++) vm->pop();
                    vm->push(Value::table(info));
                    vm->currentCoroutine()->lastResultCount = 1;
                    return true;
                }
                closure = frame->closure;
                if (closure) {
                    size_t ip = (frame->ip > 0) ? frame->ip - 1 : 0;
                    line = frame->closure->function()->chunk()->getLine(ip);
                    source = frame->closure->function()->chunk()->sourceName();
                }
            } else {
                for(int i=0; i<argCount; i++) vm->pop();
                vm->push(Value::nil());
                vm->currentCoroutine()->lastResultCount = 1;
                return true;
            }
        } else if (f.isClosure()) {
            closure = f.asClosureObj();
            source = closure->function()->chunk()->sourceName();
        } else if (f.isNativeFunction()) {
            info->set("what", Value::runtimeString(vm->internString("C")));
            info->set("source", Value::runtimeString(vm->internString("=[C]")));
            info->set("short_src", Value::runtimeString(vm->internString("[C]")));
            if (what.find('t') != std::string::npos) {
                info->set("istailcall", Value::boolean(false));
                info->set("extraargs", Value::integer(0));
            }
            for(int i=0; i<argCount; i++) vm->pop();
            vm->push(Value::table(info));
            vm->currentCoroutine()->lastResultCount = 1;
            return true;
        }

        if (closure) {
            FunctionObject* func = closure->function();
            std::string fname = func->name();
            if (targetFrame && targetFrame->isCloseMetamethod) {
                fname = "close";
                info->set("namewhat", Value::runtimeString(vm->internString("metamethod")));
            }
            info->set("name", Value::runtimeString(vm->internString(fname)));
            info->set("what", Value::runtimeString(vm->internString("Lua")));
            info->set("source", Value::runtimeString(vm->internString(source)));

            std::string short_src = source;
            if (!short_src.empty() && (short_src[0] == '@' || short_src[0] == '=')) {
                short_src = short_src.substr(1);
            }
            if (short_src.length() > 60) short_src = "..." + short_src.substr(short_src.length() - 57);
            info->set("short_src", Value::runtimeString(vm->internString(short_src)));

            info->set("nups", Value::integer(func->upvalueCount()));
            info->set("nparams", Value::integer(func->arity()));
            info->set("isvararg", Value::boolean(func->hasVarargs()));

            if (line != -1) {
                info->set("currentline", Value::integer(line));
            }

            info->set("linedefined", Value::integer(func->lineDefined()));
            info->set("lastlinedefined", Value::integer(func->lastLineDefined()));

            info->set("func", Value::closure(closure));
        }

        if (what.find('t') != std::string::npos) {
            info->set("istailcall", Value::boolean(targetFrame ? targetFrame->isTailCall : false));
            info->set("extraargs", Value::integer(targetFrame ? targetFrame->extraargs : 0));
        }

        for(int i=0; i<argCount; i++) vm->pop();
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
