#include "vm/vm.hpp"
#include "compiler/lexer.hpp"
#include "compiler/parser.hpp"
#include "compiler/codegen.hpp"
#include "value/function.hpp"
#include "value/closure.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
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

bool native_tonumber(VM* vm, int argCount) {
    if (argCount < 1 || argCount > 2) {
        vm->runtimeError("tonumber expects 1 or 2 arguments");
        return false;
    }
    
    Value baseVal = (argCount == 2) ? vm->pop() : Value::number(10);
    Value val = vm->pop();
    
    if (val.isNumber() && baseVal.asNumber() == 10) {
        vm->push(val);
        return true;
    }
    
    if (!val.isString()) {
        vm->push(Value::nil());
        return true;
    }
    
    std::string s = vm->getStringValue(val);
    int base = static_cast<int>(baseVal.asNumber());
    
    if (base < 2 || base > 36) {
        vm->runtimeError("base out of range");
        return false;
    }
    
    try {
        size_t pos;
        if (base == 10) {
            double num = std::stod(s, &pos);
            if (pos == s.length()) {
                vm->push(Value::number(num));
                return true;
            }
        } else {
            long num = std::stol(s, &pos, base);
            if (pos == s.length()) {
                vm->push(Value::number(static_cast<double>(num)));
                return true;
            }
        }
    } catch (...) {
        // Fall through to nil
    }
    
    vm->push(Value::nil());
    return true;
}

bool native_rawget(VM* vm, int argCount) {
    if (argCount != 2) {
        vm->runtimeError("rawget expects 2 arguments");
        return false;
    }
    Value key = vm->pop();
    Value table = vm->pop();
    
    if (!table.isTable()) {
        vm->runtimeError("bad argument #1 to 'rawget' (table expected)");
        return false;
    }
    
    TableObject* t = vm->getTable(table.asTableIndex());
    vm->push(t->get(key));
    return true;
}

bool native_rawset(VM* vm, int argCount) {
    if (argCount != 3) {
        vm->runtimeError("rawset expects 3 arguments");
        return false;
    }
    Value val = vm->pop();
    Value key = vm->pop();
    Value table = vm->pop();
    
    if (!table.isTable()) {
        vm->runtimeError("bad argument #1 to 'rawset' (table expected)");
        return false;
    }
    
    if (key.isNil()) {
        vm->runtimeError("table index is nil");
        return false;
    }
    
    TableObject* t = vm->getTable(table.asTableIndex());
    t->set(key, val);
    vm->push(table);
    return true;
}

bool native_warn(VM* vm, int argCount) {
    for (int i = 0; i < argCount; i++) {
        Value val = vm->peek(argCount - 1 - i);
        std::cerr << "Lua warning: " << val.toString() << std::endl;
    }
    
    // Pop arguments
    for (int i = 0; i < argCount; i++) vm->pop();
    
    vm->push(Value::nil());
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

bool native_error(VM* vm, int argCount) {
    if (argCount < 1) {
        vm->runtimeError("error expects at least 1 argument");
        return false;
    }
    Value msg = vm->pop();
    vm->runtimeError(vm->getStringValue(msg));
    return false;
}

bool native_pcall(VM* vm, int argCount) {
    if (argCount < 1) {
        vm->runtimeError("pcall expects at least 1 argument");
        return false;
    }

    // Arguments are on stack: [..., func, arg1, arg2, ...]
    // We want to call 'func' with 'argCount-1' arguments
    
    // We need a way to run a protected call in the VM.
    // Since we don't have a dedicated VM::pcall yet, we'll try to use callValue
    // and handle the error if it occurs.
    
    // In our current VM, runtimeError sets hadError_ to true and returns false from run().
    // We need to capture that.
    
    // However, native_pcall is a native function, it's called FROM VM::run().
    // If we call VM::callValue here, and it fails, it will set hadError_=true.
    
    // Let's implement this by using the newly added VM flags.
    // Wait, native_pcall is called WITHIN the VM loop.
    
    // This is complex without full VM support for pcall.
    // A simpler way:
    // 1. Mark VM as 'in pcall'
    // 2. Call the function
    // 3. If it returns false (due to error), catch it, push false + error msg.
    
    // But VM::callValue might return false and stop the whole VM.
    
    vm->runtimeError("pcall not fully implemented yet");
    return false;
}

bool native_load(VM* vm, int argCount) {
    if (argCount < 1) {
        vm->runtimeError("load expects at least 1 argument");
        return false;
    }
    Value chunkVal = vm->pop();
    if (!chunkVal.isString()) {
        vm->runtimeError("load expects string argument");
        return false;
    }
    
    std::string source = vm->getStringValue(chunkVal);
    // Use VM::runSource logic but return the closure instead of running it.
    // We need a way to compile only.
    
    vm->runtimeError("load not fully implemented yet");
    return false;
}

bool native_assert(VM* vm, int argCount) {
    if (argCount < 1) {
        vm->runtimeError("bad argument #1 to 'assert' (value expected)");
        return false;
    }

    // Peek the first argument (the value to check)
    Value v = vm->peek(argCount - 1);

    if (v.isNil() || (v.isBool() && !v.asBool())) {
        std::string message = "assertion failed!";
        if (argCount >= 2) {
            Value msgVal = vm->peek(argCount - 2);
            message = vm->getStringValue(msgVal);
        }
        vm->runtimeError(message);
        return false;
    }

    // Success: Return all arguments.
    // They are already on the stack, just leave them there.
    return true;
}

bool native_loadfile(VM* vm, int argCount) {
    if (argCount != 1) {
        vm->runtimeError("loadfile expects 1 argument");
        return false;
    }
    Value pathVal = vm->pop();
    if (!pathVal.isString() && !pathVal.isRuntimeString()) {
        vm->runtimeError("loadfile expects string argument");
        return false;
    }

    std::string path = vm->getStringValue(pathVal);

    std::ifstream file(path);
    if (!file.is_open()) {
        vm->push(Value::nil());
        size_t errIdx = vm->internString("Could not open file: " + path);
        vm->push(Value::runtimeString(errIdx));
        return true;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();

    try {
        Lexer lexer(source);
        Parser parser(lexer);
        auto program = parser.parse();
        if (!program) {
            vm->push(Value::nil());
            size_t errIdx = vm->internString("Parse error in " + path);
            vm->push(Value::runtimeString(errIdx));
            return true;
        }

        CodeGenerator codegen;
        auto chunk = codegen.generate(program.get());
        if (!chunk) {
            vm->push(Value::nil());
            size_t errIdx = vm->internString("Code generation error in " + path);
            vm->push(Value::runtimeString(errIdx));
            return true;
        }

        // Create FunctionObject and Closure
        FunctionObject* function = new FunctionObject(path, 0, std::move(chunk));
        vm->registerFunction(function);
        size_t closureIndex = vm->createClosure(function);
        vm->push(Value::closure(closureIndex));
        return true;

    } catch (const CompileError& e) {
        vm->push(Value::nil());
        size_t errIdx = vm->internString(e.what());
        vm->push(Value::runtimeString(errIdx));
        return true;
    } catch (const std::exception& e) {
        vm->push(Value::nil());
        size_t errIdx = vm->internString(e.what());
        vm->push(Value::runtimeString(errIdx));
        return true;
    }
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

    size_t errorIdx = vm->registerNativeFunction("error", native_error);
    vm->globals()["error"] = Value::nativeFunction(errorIdx);

    size_t assertIdx = vm->registerNativeFunction("assert", native_assert);
    vm->globals()["assert"] = Value::nativeFunction(assertIdx);

    size_t loadfileIdx = vm->registerNativeFunction("loadfile", native_loadfile);
    vm->globals()["loadfile"] = Value::nativeFunction(loadfileIdx);

    size_t loadIdx = vm->registerNativeFunction("load", native_load);
    vm->globals()["load"] = Value::nativeFunction(loadIdx);

    size_t pcallIdx = vm->registerNativeFunction("pcall", native_pcall);
    vm->globals()["pcall"] = Value::nativeFunction(pcallIdx);

    size_t tonumberIdx = vm->registerNativeFunction("tonumber", native_tonumber);
    vm->globals()["tonumber"] = Value::nativeFunction(tonumberIdx);

    size_t rawgetIdx = vm->registerNativeFunction("rawget", native_rawget);
    vm->globals()["rawget"] = Value::nativeFunction(rawgetIdx);

    size_t rawsetIdx = vm->registerNativeFunction("rawset", native_rawset);
    vm->globals()["rawset"] = Value::nativeFunction(rawsetIdx);

    size_t warnIdx = vm->registerNativeFunction("warn", native_warn);
    vm->globals()["warn"] = Value::nativeFunction(warnIdx);

    // Register _VERSION
    size_t versionIdx = vm->internString("Lua 5.5");
    vm->globals()["_VERSION"] = Value::runtimeString(versionIdx);

    // Initialize package table
    size_t packageIdx = vm->createTable();
    TableObject* package = vm->getTable(packageIdx);
    vm->globals()["package"] = Value::table(packageIdx);

    size_t loadedIdx = vm->createTable();
    package->set(Value::runtimeString(vm->internString("loaded")), Value::table(loadedIdx));

    package->set(Value::runtimeString(vm->internString("path")), Value::runtimeString(vm->internString("./?.lua;./?/init.lua")));

    // Define require in Lua
    const char* requireScript = 
        "function require(modname)\n"
        "    if package.loaded[modname] then return package.loaded[modname] end\n"
        "    local errors = \"\"\n"
        "    local path = package.path .. \";\"\n"
        "    local start = 1\n"
        "    while true do\n"
        "        local sep = string.find(path, \";\", start)\n"
        "        if not sep then break end\n"
        "        local template = string.sub(path, start, sep - 1)\n"
        "        local filename = string.gsub(template, \"?\", modname)\n"
        "        local f, err = loadfile(filename)\n"
        "        if f then\n"
        "            local res = f()\n"
        "            if res == nil then res = true end\n"
        "            package.loaded[modname] = res\n"
        "            return res\n"
        "        end\n"
        "        errors = errors .. \"\\n\\tno file '\" .. filename .. \"'\"\n"
        "        start = sep + 1\n"
        "    end\n"
        "    error(\"module '\" .. modname .. \"' not found:\" .. errors)\n"
        "end\n";

    vm->runSource(requireScript, "require_init");
}
