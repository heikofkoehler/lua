#include "vm/vm.hpp"
#include "compiler/lexer.hpp"
#include "compiler/parser.hpp"
#include "compiler/codegen.hpp"
#include "value/function.hpp"
#include "value/closure.hpp"
#include "value/table.hpp"
#include "value/userdata.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

bool native_collectgarbage(VM* vm, int argCount) {
    if (vm->isClosing()) {
        for (int i = 0; i < argCount; i++) vm->pop();
        vm->push(Value::boolean(false));
        vm->currentCoroutine()->lastResultCount = 1;
        return true;
    }

    std::string opt = "collect";
    if (argCount >= 1) {
        Value var = vm->peek(argCount - 1);
        if (var.isString()) {
            opt = vm->getStringValue(var);
        }
    }

    if (opt == "count") {
        double count = static_cast<double>(vm->bytesAllocated()) / 1024.0;
        for(int i=0; i<argCount; i++) vm->pop();
        vm->push(Value::number(count));
        vm->currentCoroutine()->lastResultCount = 1;
        return true;
    } else if (opt == "isrunning") {
        for(int i=0; i<argCount; i++) vm->pop();
        vm->push(Value::boolean(vm->gcEnabled()));
        vm->currentCoroutine()->lastResultCount = 1;
        return true;
    } else if (opt == "incremental") {
        VM::GCMode old = vm->gcMode();
        vm->setGCMode(VM::GCMode::INCREMENTAL);
        for(int i=0; i<argCount; i++) vm->pop();
        vm->push(Value::runtimeString(vm->internString(old == VM::GCMode::INCREMENTAL ? "incremental" : "generational")));
        vm->currentCoroutine()->lastResultCount = 1;
        return true;
    } else if (opt == "generational") {
        VM::GCMode old = vm->gcMode();
        vm->setGCMode(VM::GCMode::GENERATIONAL);
        vm->setGCState(VM::GCState::PAUSE); // Reset to allow fresh generational cycle
        for(int i=0; i<argCount; i++) vm->pop();
        vm->push(Value::runtimeString(vm->internString(old == VM::GCMode::INCREMENTAL ? "incremental" : "generational")));
        vm->currentCoroutine()->lastResultCount = 1;
        return true;
    } else if (opt == "step") {
        if (vm->gcMode() == VM::GCMode::GENERATIONAL) {
            do {
                vm->gcStep();
            } while (vm->gcState() != VM::GCState::PAUSE);
            for(int i=0; i<argCount; i++) vm->pop();
            vm->push(Value::boolean(true));
            vm->currentCoroutine()->lastResultCount = 1;
            return true;
        }

        // Get step size hint (default 0 = just one step)
        int64_t siz = 0;
        if (argCount >= 2) {
            Value szVal = vm->peek(0);
            if (szVal.isInteger()) siz = szVal.asInteger();
            else if (szVal.isNumber()) siz = static_cast<int64_t>(szVal.asNumber());
        }
        for(int i=0; i<argCount; i++) vm->pop();

        // Perform incremental GC work. The 'siz' controls how many steps.
        // A step size of 0 means one single step; larger sizes mean more work.
        // We return true when a complete collection cycle finishes (state->PAUSE).
        // In Lua, the step size parameter 'siz' is given in Kbytes.
        // A value of 0 means a basic minimal step (1 step).
        // Each basic step processes ~256 bytes, so 1 KB corresponds to ~4 steps.
        int steps = (siz <= 0) ? 1 : static_cast<int>(siz * 4);

        bool completed = false;
        for (int i = 0; i < steps; i++) {
            vm->gcStep();
            if (vm->gcState() == VM::GCState::PAUSE) {
                completed = true;
                break;
            }
        }

        vm->push(Value::boolean(completed));
        vm->currentCoroutine()->lastResultCount = 1;
        return true;
    } else if (opt == "stop") {
        vm->setGCEnabled(false);
        for(int i=0; i<argCount; i++) vm->pop();
        vm->currentCoroutine()->lastResultCount = 0;
        return true;
    } else if (opt == "restart") {
        vm->setGCEnabled(true);
        for(int i=0; i<argCount; i++) vm->pop();
        vm->currentCoroutine()->lastResultCount = 0;
        return true;
    }
    else if (opt == "param") {
        // collectgarbage("param", name, [newvalue])
        static std::unordered_map<std::string, int> gc_params = {
            {"minormul", 200}, {"majorminor", 100}, {"minormajor", 100},
            {"pause", 200}, {"stepmul", 100}, {"stepsize", 200}
        };
        if (argCount >= 2) {
            std::string param = vm->getStringValue(vm->peek(argCount - 2));
            int prev = gc_params.count(param) ? gc_params[param] : 100;
            if (argCount >= 3) {
                int val = static_cast<int>(vm->peek(0).asNumber());
                gc_params[param] = val;
            }
            for (int i = 0; i < argCount; i++) vm->pop();
            vm->push(Value::number(prev));
            vm->currentCoroutine()->lastResultCount = 1;
            return true;
        }
    } else if (opt == "setmemorylimit") {
        if (argCount < 2) {
            vm->runtimeError("collectgarbage('setmemorylimit') expects a limit in bytes");
            return false;
        }
        double limit = vm->peek(0).asNumber();
        vm->setMemoryLimit(static_cast<size_t>(limit));
        for(int i=0; i<argCount; i++) vm->pop();
        vm->push(Value::nil());
        vm->currentCoroutine()->lastResultCount = 1;
        return true;
    }

    // Default: full collect
    vm->collectGarbage();
    for(int i=0; i<argCount; i++) vm->pop();
    vm->push(Value::integer(0));
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_setmetatable(VM* vm, int argCount) {
    if (argCount != 2) {
        vm->runtimeError("setmetatable expects 2 arguments");
        return false;
    }
    Value metatable = vm->peek(0);
    Value tableValue = vm->peek(1);

    if (!tableValue.isTable()) {
        vm->runtimeError("bad argument #1 to 'setmetatable' (table expected)");
        return false;
    }

    if (!metatable.isNil() && !metatable.isTable()) {
        vm->runtimeError("bad argument #2 to 'setmetatable' (nil or table expected)");
        return false;
    }

    TableObject* table = tableValue.asTableObj();
    
    // Check if current metatable has __metatable field
    Value currentMt = table->getMetatable();
    if (!currentMt.isNil() && currentMt.isTable()) {
        if (!currentMt.asTableObj()->get("__metatable").isNil()) {
            vm->runtimeError("cannot change a protected metatable");
            return false;
        }
    }

    table->setMetatable(metatable);
    
    for(int i=0; i<argCount; i++) vm->pop();
    vm->push(tableValue);
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_getmetatable(VM* vm, int argCount) {
    if (argCount != 1) {
        vm->runtimeError("getmetatable expects 1 argument");
        return false;
    }
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

    if (!mt.isNil() && mt.isTable()) {
        Value protected_mt = mt.asTableObj()->get("__metatable");
        if (!protected_mt.isNil()) {
            vm->push(protected_mt);
            vm->currentCoroutine()->lastResultCount = 1;
            return true;
        }
    }

    vm->push(mt);
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_tostring(VM* vm, int argCount) {
    if (argCount < 1) {
        vm->runtimeError("tostring expects at least 1 argument");
        return false;
    }
    Value val = vm->peek(argCount - 1);
    std::string str;
    if (!vm->toLString(val, str)) return false;
    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(Value::runtimeString(vm->internString(str)));
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_tonumber(VM* vm, int argCount) {
    if (argCount < 1 || argCount > 2) {
        vm->runtimeError("tonumber expects 1 or 2 arguments");
        return false;
    }
    Value val = vm->peek(argCount - 1);
    if (argCount == 1 && val.isNumber()) {
        vm->pop();
        vm->push(val);
        vm->currentCoroutine()->lastResultCount = 1;
        return true;
    }

    if (!val.isString()) {
        for (int i = 0; i < argCount; i++) vm->pop();
        vm->push(Value::nil());
        vm->currentCoroutine()->lastResultCount = 1;
        return true;
    }

    std::string s = vm->getStringValue(val);

    // Reject embedded NUL
    if (std::strlen(s.c_str()) != s.length()) {
        for (int i = 0; i < argCount; i++) vm->pop();
        vm->push(Value::nil());
        vm->currentCoroutine()->lastResultCount = 1;
        return true;
    }

    // Trim leading and trailing whitespace
    auto start = s.find_first_not_of(" \t\n\r\f\v");
    if (start == std::string::npos) {
        for (int i = 0; i < argCount; i++) vm->pop();
        vm->push(Value::nil());
        vm->currentCoroutine()->lastResultCount = 1;
        return true;
    }
    auto end = s.find_last_not_of(" \t\n\r\f\v");
    s = s.substr(start, end - start + 1);

    if (argCount == 1) {
        double num = 0.0;
        int64_t inum = 0;
        bool isInt = false;
        if (VM::stringToNumber(s, num, inum, isInt)) {
            for (int i = 0; i < argCount; i++) vm->pop();
            if (isInt) {
                vm->push(vm->makeInteger(inum));
            } else {
                vm->push(Value::number(num));
            }
        } else {
            for (int i = 0; i < argCount; i++) vm->pop();
            vm->push(Value::nil());
        }
    } else {
        int base = static_cast<int>(vm->peek(argCount - 2).asNumber());
        if (base < 2 || base > 36) {
            vm->runtimeError("bad argument #2 to 'tonumber' (base out of range)");
            return false;
        }
        const char* p = s.c_str();
        bool neg = false;
        if (*p == '+') p++;
        else if (*p == '-') { neg = true; p++; }
        char* endp = nullptr;
        errno = 0;
        unsigned long long uval = std::strtoull(p, &endp, base);
        for (int i = 0; i < argCount; i++) vm->pop();
        if (endp && *endp == '\0' && endp != p && errno == 0) {
            uint64_t finalVal = uval;
            if (neg) finalVal = 0ULL - finalVal;
            vm->push(vm->makeInteger(static_cast<int64_t>(finalVal)));
        } else {
            vm->push(Value::nil());
        }
    }

    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_print(VM* vm, int argCount) {
    for (int i = 0; i < argCount; i++) {
        Value val = vm->peek(argCount - 1 - i);
        
        // Try to call tostring(val)
        vm->push(vm->getGlobal("tostring"));
        vm->push(val);
        
        size_t prevFrames = vm->currentCoroutine()->frames.size();
        if (vm->callValue(1, 2)) {
            if (vm->currentCoroutine()->frames.size() > prevFrames) {
                if (!vm->run(prevFrames)) return false;
            }
            Value res = vm->pop();
            std::cout << res.toString();
        } else {

            std::cout << val.toString(); // Fallback
        }
        
        if (i < argCount - 1) std::cout << "\t";
    }
    std::cout << std::endl;

    for (int i = 0; i < argCount; i++) vm->pop();
    vm->currentCoroutine()->lastResultCount = 0;
    return true;
}

bool native_sleep(VM* vm, int argCount) {
    if (argCount != 1) {
        vm->runtimeError("sleep expects 1 argument");
        return false;
    }
    double seconds = vm->peek(0).asNumber();
    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(seconds * 1000)));
    
    vm->pop();
    vm->currentCoroutine()->lastResultCount = 0;
    return true;
}

static size_t g_next_idx = 0;

bool native_next(VM* vm, int argCount) {
    if (argCount < 1) {
        vm->runtimeError("bad argument #1 to 'next' (value expected)");
        return false;
    }
    Value tableVal = vm->peek(argCount - 1);
    Value key = (argCount >= 2) ? vm->peek(argCount - 2) : Value::nil();

    if (!tableVal.isTable()) {
        vm->runtimeError("bad argument #1 to 'next' (table expected)");
        return false;
    }

    TableObject* table = tableVal.asTableObj();
    bool keyFound = true;
    auto result = table->next(key, keyFound);
    if (!keyFound) {
        vm->runtimeError("invalid key to 'next'");
        return false;
    }

    for (int i = 0; i < argCount; i++) vm->pop();
    
    if (result.first.isNil()) {
        vm->push(Value::nil());
        vm->currentCoroutine()->lastResultCount = 1;
    } else {
        vm->push(result.first);
        vm->push(result.second);
        vm->currentCoroutine()->lastResultCount = 2;
    }
    return true;
}

bool native_pairs(VM* vm, int argCount) {
    if (argCount < 1) {
        vm->runtimeError("bad argument #1 to 'pairs' (value expected)");
        return false;
    }
    Value val = vm->peek(argCount - 1);

    Value mm = vm->getMetamethod(val, "__pairs");
    if (!mm.isNil()) {
        for (int i = 0; i < argCount; i++) vm->pop();
        vm->push(mm);
        vm->push(val);
        size_t prevFrames = vm->currentCoroutine()->frames.size();
        if (vm->callValue(1, 5)) {
            if (vm->currentCoroutine()->frames.size() > prevFrames) {
                if (!vm->run(prevFrames)) return false;
            }
            vm->currentCoroutine()->lastResultCount = 4;
            return true;
        }
        return false;
    }

    if (!val.isTable()) {
        vm->runtimeError("bad argument #1 to 'pairs' (table expected)");
        return false;
    }

    if (g_next_idx == 0) {
        g_next_idx = vm->registerNativeFunction("next", native_next);
    }
    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(Value::nativeFunction(g_next_idx));
    vm->push(val);
    vm->push(Value::nil());
    vm->currentCoroutine()->lastResultCount = 3;
    return true;
}

static size_t g_ipairs_iter_idx = 0;

static Value get_table_item(VM* vm, const Value& tableVal, const Value& key) {
    if (tableVal.isTable()) {
        TableObject* table = tableVal.asTableObj();
        Value v = table->get(key);
        if (!v.isNil() || table->getMetatable().isNil()) {
            return v;
        }
    }
    Value indexMethod = vm->getMetamethod(tableVal, "__index");
    if (indexMethod.isNil()) {
        return Value::nil();
    }
    if (indexMethod.isFunction()) {
        vm->push(indexMethod);
        vm->push(tableVal);
        vm->push(key);
        size_t prevFrames = vm->currentCoroutine()->frames.size();
        if (!vm->callValue(2, 2)) return Value::nil();
        if (vm->currentCoroutine()->frames.size() > prevFrames) {
            if (!vm->run(prevFrames)) return Value::nil();
        }
        return vm->pop();
    } else if (indexMethod.isTable()) {
        return get_table_item(vm, indexMethod, key);
    }
    return Value::nil();
}

bool native_ipairs_iter(VM* vm, int argCount) {
    if (argCount < 2) return false;
    Value tableVal = vm->peek(argCount - 1);
    Value indexVal = vm->peek(argCount - 2);

    int64_t nextIndex = static_cast<int64_t>(static_cast<uint64_t>(indexVal.asInteger()) + 1ULL);
    Value key = vm->makeInteger(nextIndex);
    Value val = get_table_item(vm, tableVal, key);

    for (int i = 0; i < argCount; i++) vm->pop();
    if (val.isNil()) {
        vm->push(Value::nil());
        vm->currentCoroutine()->lastResultCount = 1;
    } else {
        vm->push(key);
        vm->push(val);
        vm->currentCoroutine()->lastResultCount = 2;
    }
    return true;
}

bool native_ipairs(VM* vm, int argCount) {
    if (argCount < 1) {
        vm->runtimeError("bad argument #1 to 'ipairs' (value expected)");
        return false;
    }
    Value table = vm->peek(argCount - 1);
    if (!table.isTable()) {
        vm->runtimeError("bad argument #1 to 'ipairs' (table expected)");
        return false;
    }

    if (g_ipairs_iter_idx == 0) {
        g_ipairs_iter_idx = vm->registerNativeFunction("__ipairs_iter", native_ipairs_iter);
    }
    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(Value::nativeFunction(g_ipairs_iter_idx));
    vm->push(table);
    vm->push(vm->makeInteger(0));
    vm->currentCoroutine()->lastResultCount = 3;
    return true;
}

bool native_error(VM* vm, int argCount) {
    if (argCount >= 1) {
        Value val = vm->peek(argCount - 1);
        int level = 1;
        if (argCount >= 2 && vm->peek(argCount - 2).isNumber()) {
            level = static_cast<int>(vm->peek(argCount - 2).asNumber());
        }
        vm->runtimeError(val, level);
    } else {
        vm->runtimeError(Value::nil(), 1);
    }
    return false;
}

bool native_assert(VM* vm, int argCount) {
    if (argCount < 1) {
        vm->runtimeError("bad argument #1 to 'assert' (value expected)", 1);
        return false;
    }
    Value cond = vm->peek(argCount - 1);
    if (cond.isFalsey()) {
        std::string msg = (argCount >= 2) ? vm->peek(argCount - 2).toString() : "assertion failed!";
        vm->runtimeError(msg, 1);
        return false;
    }
    // Success: return all arguments
    vm->currentCoroutine()->lastResultCount = argCount;
    return true;
}

bool native_rawget(VM* vm, int argCount) {
    if (argCount < 2) {
        vm->runtimeError("bad argument #2 to 'rawget' (value expected)");
        return false;
    }
    Value table = vm->peek(argCount - 1);
    Value key = vm->peek(argCount - 2);
    if (!table.isTable()) {
        vm->runtimeError("bad argument #1 to 'rawget' (table expected)");
        return false;
    }
    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(table.asTableObj()->get(key));
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_rawset(VM* vm, int argCount) {
    if (argCount < 3) {
        vm->runtimeError("bad argument #3 to 'rawset' (value expected)");
        return false;
    }
    Value table = vm->peek(argCount - 1);
    Value key = vm->peek(argCount - 2);
    Value val = vm->peek(argCount - 3);
    if (!table.isTable()) {
        vm->runtimeError("bad argument #1 to 'rawset' (table expected)");
        return false;
    }
    if (key.isNil()) {
        vm->runtimeError("table index is nil");
        return false;
    }
    if (key.isFloat() && std::isnan(key.asNumber())) {
        vm->runtimeError("table index is NaN");
        return false;
    }
    table.asTableObj()->set(key, val);
    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(table);
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_rawequal(VM* vm, int argCount) {
    if (argCount < 2) {
        vm->runtimeError("bad argument #2 to 'rawequal' (value expected)");
        return false;
    }
    Value a = vm->peek(argCount - 1);
    Value b = vm->peek(argCount - 2);
    for(int i=0; i<argCount; i++) vm->pop();
    vm->push(Value::boolean(a == b));
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_rawlen(VM* vm, int argCount) {
    if (argCount < 1) {
        vm->runtimeError("bad argument #1 to 'rawlen' (value expected)");
        return false;
    }
    Value a = vm->peek(argCount - 1);
    for(int i=0; i<argCount; i++) vm->pop();
    if (a.isString()) {
        vm->push(Value::integer(static_cast<int64_t>(vm->getStringValue(a).length())));
    } else if (a.isTable()) {
        vm->push(Value::integer(static_cast<int64_t>(a.asTableObj()->length())));
    } else {
        vm->runtimeError("rawlen expects string or table");
        return false;
    }
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_vm_jit(VM* vm, int argCount) {
    if (argCount >= 1) {
        Value val = vm->peek(argCount - 1);
        if (val.isBool()) {
            vm->setJitEnabled(val.asBool());
        } else if (val.isString()) {
            std::string opt = vm->getStringValue(val);
            if (opt == "off") vm->setJitEnabled(false);
            else if (opt == "on") vm->setJitEnabled(true);
        }
    }
    for(int i=0; i<argCount; i++) vm->pop();
    vm->push(Value::boolean(vm->isJitEnabled()));
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_warn(VM* vm, int argCount) {
    if (argCount == 0) {
        vm->runtimeError("bad argument #1 to 'warn' (string expected, got no value)");
        return false;
    }

    // Check all arguments are strings
    for (int i = 0; i < argCount; i++) {
        Value val = vm->peek(argCount - 1 - i);
        if (!val.isString()) {
            std::string typeName = "nil";
            if (val.isNumber()) typeName = "number";
            else if (val.isBool()) typeName = "boolean";
            else if (val.isTable()) typeName = "table";
            else if (val.isFunction() || val.isNativeFunction() || val.isCFunction()) typeName = "function";
            else if (val.isThread()) typeName = "thread";
            else if (val.isUserdata()) typeName = "userdata";
            vm->runtimeError("bad argument #" + std::to_string(i + 1) + " to 'warn' (string expected, got " + typeName + ")");
            return false;
        }
    }

    // Check for control messages (only valid when argCount == 1)
    if (argCount == 1) {
        std::string s = vm->getStringValue(vm->peek(0));
        if (!s.empty() && s[0] == '@') {
            if (s == "@off") {
                vm->setWarnEnabled(false);
            } else if (s == "@on") {
                vm->setWarnEnabled(true);
            }
            // Control messages are not printed
            vm->pop();
            vm->currentCoroutine()->lastResultCount = 0;
            return true;
        }
    }

    if (vm->warnEnabled()) {
        std::cerr << "Lua warning: ";
        for (int i = 0; i < argCount; i++) {
            std::cerr << vm->getStringValue(vm->peek(argCount - 1 - i));
        }
        std::cerr << std::endl;
    }

    for (int i = 0; i < argCount; i++) vm->pop();
    vm->currentCoroutine()->lastResultCount = 0;
    return true;
}

bool native_loadfile(VM* vm, int argCount) {
    std::string path;
    bool hasPath = false;
    if (argCount >= 1) {
        Value pathVal = vm->peek(argCount - 1);
        if (!pathVal.isNil()) {
            if (!pathVal.isString()) {
                vm->runtimeError("bad argument #1 to 'loadfile' (string expected, got " + pathVal.typeToString() + ")");
                return false;
            }
            path = vm->getStringValue(pathVal);
            hasPath = true;
        }
    }
    
    Value env = Value::nil();
    bool hasEnv = false;
    if (argCount >= 3) {
        env = vm->peek(argCount - 3);
        hasEnv = true;
    }

    std::string mode = "bt";
    if (argCount >= 2) {
        Value modeVal = vm->peek(argCount - 2);
        if (!modeVal.isNil()) {
            if (!modeVal.isString()) {
                vm->runtimeError("bad argument #2 to 'loadfile' (string expected, got " + modeVal.typeToString() + ")");
                return false;
            }
            mode = vm->getStringValue(modeVal);
            if (mode.find('B') != std::string::npos) {
                vm->runtimeError("bad argument #2 to 'loadfile' (invalid mode)");
                return false;
            }
        }
    }

    std::string sourceName = hasPath ? ("@" + path) : "=stdin";
    std::string source;
    if (hasPath) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            for (int i = 0; i < argCount; i++) vm->pop();
            vm->push(Value::nil());
            StringObject* errStr = vm->internString("cannot open " + path + ": " + std::strerror(errno));
            vm->push(Value::runtimeString(errStr));
            vm->currentCoroutine()->lastResultCount = 2;
            return true;
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        source = buffer.str();
    } else {
        std::stringstream buffer;
        buffer << std::cin.rdbuf();
        source = buffer.str();
    }

    if (source.size() >= 3 && (unsigned char)source[0] == 0xEF && (unsigned char)source[1] == 0xBB && (unsigned char)source[2] == 0xBF) {
        source = source.substr(3);
    }
    if (!source.empty() && source[0] == '#') {
        size_t nl = source.find('\n');
        if (nl != std::string::npos) {
            size_t nextPos = nl + 1;
            if (nextPos < source.size() && source[nextPos] == '\x1b') {
                source = source.substr(nextPos);
            } else {
                source = source.substr(nl);
            }
        } else {
            source.clear();
        }
    }

    bool isBinary = (!source.empty() && source[0] == '\x1b');

    if (isBinary) {
        if (mode.find('b') == std::string::npos) {
            for (int i = 0; i < argCount; i++) vm->pop();
            vm->push(Value::nil());
            vm->push(Value::runtimeString(vm->internString("attempt to load a binary chunk (mode is '" + mode + "')")));
            vm->currentCoroutine()->lastResultCount = 2;
            return true;
        }
        try {
            std::istringstream is(source, std::ios::binary);
            auto function = FunctionObject::deserialize(is);
            if (!function) {
                for (int i = 0; i < argCount; i++) vm->pop();
                vm->push(Value::nil());
                vm->push(Value::runtimeString(vm->internString("bad binary format (truncated chunk)")));
                vm->currentCoroutine()->lastResultCount = 2;
                return true;
            }
            FunctionObject* funcPtr = function.get();
            vm->registerFunction(function.release());
            vm->setSourceName(sourceName);
            vm->internConstants(*funcPtr);
            ClosureObject* closure = vm->createClosure(funcPtr);
            vm->setupRootUpvalues(closure, env, hasEnv);
            for (int i = 0; i < argCount; i++) vm->pop();
            vm->push(Value::closure(closure));
            vm->currentCoroutine()->lastResultCount = 1;
            return true;
        } catch (const std::exception& e) {
            for (int i = 0; i < argCount; i++) vm->pop();
            vm->push(Value::nil());
            vm->push(Value::runtimeString(vm->internString(e.what())));
            vm->currentCoroutine()->lastResultCount = 2;
            return true;
        }
    }

    // Text chunk
    if (mode.find('t') == std::string::npos) {
        for (int i = 0; i < argCount; i++) vm->pop();
        vm->push(Value::nil());
        vm->push(Value::runtimeString(vm->internString("attempt to load a text chunk (mode is '" + mode + "')")));
        vm->currentCoroutine()->lastResultCount = 2;
        return true;
    }

    try {
        Lexer lexer(source);
        lexer.setSourceName(sourceName);
        Parser parser(lexer);
        auto program = parser.parse();
        if (!program) {
            for (int i = 0; i < argCount; i++) vm->pop();
            vm->push(Value::nil());
            StringObject* errStr = vm->internString("Parse error in " + (hasPath ? path : "stdin"));
            vm->push(Value::runtimeString(errStr));
            vm->currentCoroutine()->lastResultCount = 2;
            return true;
        }

        CodeGenerator codegen;
        auto function = codegen.generate(program.get(), sourceName);
        if (!function) {
            for (int i = 0; i < argCount; i++) vm->pop();
            vm->push(Value::nil());
            StringObject* errStr = vm->internString("Code generation error in " + (hasPath ? path : "stdin"));
            vm->push(Value::runtimeString(errStr));
            vm->currentCoroutine()->lastResultCount = 2;
            return true;
        }

        FunctionObject* funcPtr = function.get();
        vm->registerFunction(function.release());
        vm->setSourceName(sourceName);
        vm->internConstants(*funcPtr);
        ClosureObject* closure = vm->createClosure(funcPtr);
        vm->setupRootUpvalues(closure, env, hasEnv);
        for (int i = 0; i < argCount; i++) vm->pop();
        vm->push(Value::closure(closure));
        vm->currentCoroutine()->lastResultCount = 1;
        return true;

    } catch (const CompileError& e) {
        for (int i = 0; i < argCount; i++) vm->pop();
        vm->push(Value::nil());
        StringObject* errStr = vm->internString(e.what());
        vm->push(Value::runtimeString(errStr));
        vm->currentCoroutine()->lastResultCount = 2;
        return true;
    } catch (const std::exception& e) {
        for (int i = 0; i < argCount; i++) vm->pop();
        vm->push(Value::nil());
        StringObject* errStr = vm->internString(e.what());
        vm->push(Value::runtimeString(errStr));
        vm->currentCoroutine()->lastResultCount = 2;
        return true;
    }
}

bool native_dofile(VM* vm, int argCount) {
    if (!native_loadfile(vm, argCount)) return false;
    
    // If native_loadfile returned nil, error
    if (vm->currentCoroutine()->lastResultCount == 2) {
        Value err = vm->peek(0);
        std::string errMsg = err.isString() ? vm->getStringValue(err) : "error";
        vm->pop();
        vm->pop();
        vm->runtimeError(errMsg);
        return false;
    }
    
    // Call the closure
    size_t prevFrames = vm->currentCoroutine()->frames.size();
    if (vm->callValue(0, 0)) { // 0 args, multiple returns
        if (vm->currentCoroutine()->frames.size() > prevFrames) {
            if (!vm->run(prevFrames)) return false;
        }
        return true;
    }
    
    return false;
}

bool native_load(VM* vm, int argCount) {
    if (argCount < 1) {
        vm->runtimeError("load expects at least 1 argument");
        return false;
    }
    Value sourceVal = vm->peek(argCount - 1);
    
    std::string sourceName = "[string \"load\"]";
    if (argCount >= 2) {
        Value nameVal = vm->peek(argCount - 2);
        if (!nameVal.isNil()) sourceName = vm->getStringValue(nameVal);
    }

    Value env = Value::nil();
    bool hasEnv = false;
    if (argCount >= 4) {
        env = vm->peek(argCount - 4);
        hasEnv = true;
    }

    std::string mode = "bt";
    if (argCount >= 3) {
        Value modeVal = vm->peek(argCount - 3);
        if (!modeVal.isNil()) {
            if (!modeVal.isString()) {
                vm->runtimeError("bad argument #3 to 'load' (string expected, got " + modeVal.typeToString() + ")");
                return false;
            }
            mode = vm->getStringValue(modeVal);
            if (mode.find('B') != std::string::npos) {
                vm->runtimeError("bad argument #3 to 'load' (invalid mode)");
                return false;
            }
        }
    }

    std::string source;
    if (sourceVal.isString()) {
        source = vm->getStringValue(sourceVal);
        if (argCount < 2) {
            sourceName = source;
        }
    } else if (sourceVal.isClosure() || sourceVal.isNativeFunction() || sourceVal.isCFunction() ||
               !vm->getMetamethod(sourceVal, "__call").isNil()) {
        if (argCount < 2) {
            sourceName = "=(load)";
        }
        while (true) {
            vm->push(sourceVal);
            if (!vm->pcall(1)) {
                std::string err = vm->lastErrorMessage();
                for (int i = 0; i < argCount; i++) vm->pop();
                vm->push(Value::nil());
                vm->push(Value::runtimeString(vm->internString(err)));
                vm->currentCoroutine()->lastResultCount = 2;
                return true;
            }
            size_t pcallResCount = vm->currentCoroutine()->lastResultCount;
            std::vector<Value> pcallResults;
            pcallResults.reserve(pcallResCount);
            for (size_t i = 0; i < pcallResCount; i++) {
                pcallResults.push_back(vm->pop());
            }
            std::reverse(pcallResults.begin(), pcallResults.end());
            if (pcallResults.empty() || !pcallResults[0].isTruthy()) {
                Value errVal = (pcallResults.size() > 1) ? pcallResults[1] : Value::nil();
                std::string err = errVal.isString() ? vm->getStringValue(errVal) : errVal.toString();
                for (int i = 0; i < argCount; i++) vm->pop();
                vm->push(Value::nil());
                vm->push(Value::runtimeString(vm->internString(err)));
                vm->currentCoroutine()->lastResultCount = 2;
                return true;
            }
            Value piece = (pcallResults.size() > 1) ? pcallResults[1] : Value::nil();
            if (piece.isNil()) {
                break;
            }
            if (!piece.isString()) {
                for (int i = 0; i < argCount; i++) vm->pop();
                vm->push(Value::nil());
                vm->push(Value::runtimeString(vm->internString("reader function must return a string")));
                vm->currentCoroutine()->lastResultCount = 2;
                return true;
            }
            std::string s = vm->getStringValue(piece);
            if (s.empty()) {
                break;
            }
            source += s;
        }
    } else {
        std::string err = "bad argument #1 to 'load' (string or function expected, got " + sourceVal.typeToString() + ")";
        for (int i = 0; i < argCount; i++) vm->pop();
        vm->push(Value::nil());
        vm->push(Value::runtimeString(vm->internString(err)));
        vm->currentCoroutine()->lastResultCount = 2;
        return true;
    }

    if (source.size() >= 3 && (unsigned char)source[0] == 0xEF && (unsigned char)source[1] == 0xBB && (unsigned char)source[2] == 0xBF) {
        source = source.substr(3);
    }
    if (!source.empty() && source[0] == '#') {
        size_t nl = source.find('\n');
        if (nl != std::string::npos) {
            size_t nextPos = nl + 1;
            if (nextPos < source.size() && source[nextPos] == '\x1b') {
                source = source.substr(nextPos);
            }
        }
    }
    bool isBinary = (!source.empty() && source[0] == '\x1b');

    if (isBinary) {
        if (mode.find('b') == std::string::npos) {
            for(int i=0; i<argCount; i++) vm->pop();
            vm->push(Value::nil());
            vm->push(Value::runtimeString(vm->internString("attempt to load a binary chunk (mode is '" + mode + "')")));
            vm->currentCoroutine()->lastResultCount = 2;
            return true;
        }
        try {
            std::istringstream is(source, std::ios::binary);
            auto function = FunctionObject::deserialize(is);
            if (!function) {
                for(int i=0; i<argCount; i++) vm->pop();
                vm->push(Value::nil());
                vm->push(Value::runtimeString(vm->internString("bad binary format (truncated chunk)")));
                vm->currentCoroutine()->lastResultCount = 2;
                return true;
            }
            FunctionObject* funcPtr = function.get();
            vm->registerFunction(function.release());
            vm->setSourceName(sourceName);
            vm->internConstants(*funcPtr);
            ClosureObject* closure = vm->createClosure(funcPtr);
            vm->setupRootUpvalues(closure, env, hasEnv);
            for(int i=0; i<argCount; i++) vm->pop();
            vm->push(Value::closure(closure));
            vm->currentCoroutine()->lastResultCount = 1;
            return true;
        } catch (const std::exception& e) {
            for(int i=0; i<argCount; i++) vm->pop();
            vm->push(Value::nil());
            vm->push(Value::runtimeString(vm->internString(e.what())));
            vm->currentCoroutine()->lastResultCount = 2;
            return true;
        }
    }

    // Text chunk
    if (mode.find('t') == std::string::npos) {
        for(int i=0; i<argCount; i++) vm->pop();
        vm->push(Value::nil());
        vm->push(Value::runtimeString(vm->internString("attempt to load a text chunk (mode is '" + mode + "')")));
        vm->currentCoroutine()->lastResultCount = 2;
        return true;
    }

    try {
        Lexer lexer(source);
        lexer.setSourceName(sourceName);
        Parser parser(lexer);
        auto program = parser.parse();
        if (!program) {
            for(int i=0; i<argCount; i++) vm->pop();
            vm->push(Value::nil());
            vm->push(Value::runtimeString(vm->internString("parse error")));
            vm->currentCoroutine()->lastResultCount = 2;
            return true;
        }

        CodeGenerator codegen;
        auto function = codegen.generate(program.get(), sourceName);
        if (!function) {
            for(int i=0; i<argCount; i++) vm->pop();
            vm->push(Value::nil());
            vm->push(Value::runtimeString(vm->internString("code generation error")));
            vm->currentCoroutine()->lastResultCount = 2;
            return true;
        }

        FunctionObject* funcPtr = function.get();
        vm->registerFunction(function.release());
        vm->setSourceName(sourceName);
        vm->internConstants(*funcPtr);
        ClosureObject* closure = vm->createClosure(funcPtr);
        vm->setupRootUpvalues(closure, env, hasEnv && !env.isNil());
        for(int i=0; i<argCount; i++) vm->pop();
        vm->push(Value::closure(closure));
        vm->currentCoroutine()->lastResultCount = 1;
        return true;

    } catch (const CompileError& e) {
        for(int i=0; i<argCount; i++) vm->pop();
        vm->push(Value::nil());
        vm->push(Value::runtimeString(vm->internString(e.what())));
        vm->currentCoroutine()->lastResultCount = 2;
        return true;
    } catch (const std::exception& e) {
        for(int i=0; i<argCount; i++) vm->pop();
        vm->push(Value::nil());
        vm->push(Value::runtimeString(vm->internString(e.what())));
        vm->currentCoroutine()->lastResultCount = 2;
        return true;
    }
}

bool native_pcall(VM* vm, int argCount) {
    if (argCount < 1) {
        vm->runtimeError("pcall expects at least 1 argument");
        return false;
    }
    return vm->pcall(argCount);
}

bool native_xpcall(VM* vm, int argCount) {
    if (argCount < 2) {
        vm->runtimeError("xpcall expects at least 2 arguments");
        return false;
    }
    return vm->xpcall(argCount);
}

bool native_select(VM* vm, int argCount) {
    if (argCount < 1) {
        vm->runtimeError("select expects at least 1 argument");
        return false;
    }
    Value selector = vm->peek(argCount - 1);
    if (selector.isString() && vm->getStringValue(selector) == "#") {
        for(int i=0; i<argCount; i++) vm->pop();
        vm->push(Value::number(argCount - 1));
        vm->currentCoroutine()->lastResultCount = 1;
        return true;
    }
    int index;
    if (selector.isNumber()) {
        index = static_cast<int>(selector.asNumber());
    } else {
        vm->runtimeError("bad argument #1 to 'select' (number expected)");
        return false;
    }

    int numArgs = argCount - 1;
    if (index < 0) {
        index = numArgs + index + 1;
    }

    if (index < 1) {
        vm->runtimeError("bad argument #1 to 'select' (index out of range)");
        return false;
    }
std::vector<Value> results;
if (index <= numArgs) {
    for (int i = index; i <= numArgs; i++) {
        results.push_back(vm->peek(numArgs - i));
    }
}

for(int i=0; i<argCount; i++) vm->pop();
for (const auto& res : results) {
    vm->push(res);
}
    vm->currentCoroutine()->lastResultCount = results.size();
    return true;
}

bool native_test_userdata(VM* vm, int argCount) {
    for (int i = 0; i < argCount; i++) vm->pop();
    void* ptr = reinterpret_cast<void*>(0xDEADBEEF);
    class UserdataObject* ud = vm->createUserdata(ptr);
    vm->push(Value::userdata(ud));
    return true;
}

bool native_package_searchpath(VM* vm, int argCount) {
    if (argCount < 2 || argCount > 4) {
        vm->runtimeError("package.searchpath expects 2 to 4 arguments");
        return false;
    }
    Value v_name = vm->peek(argCount - 1);
    Value v_path = vm->peek(argCount - 2);
    
    std::string name = vm->getStringValue(v_name);
    std::string path = vm->getStringValue(v_path);
    std::string sep = (argCount >= 3) ? vm->getStringValue(vm->peek(argCount - 3)) : ".";
    std::string rep = (argCount >= 4) ? vm->getStringValue(vm->peek(argCount - 4)) : "/"; // System separator

    // Replace sep with rep in name
    if (!sep.empty()) {
        size_t pos = 0;
        while ((pos = name.find(sep, pos)) != std::string::npos) {
            name.replace(pos, sep.length(), rep);
            pos += rep.length();
        }
    }

    std::string errors = "";
    size_t start = 0;
    while (true) {
        size_t end = path.find(';', start);
        std::string template_str = path.substr(start, (end == std::string::npos) ? std::string::npos : end - start);
        
        // Replace all ? with name
        std::string filename = template_str;
        size_t q_pos = 0;
        while ((q_pos = filename.find('?', q_pos)) != std::string::npos) {
            filename.replace(q_pos, 1, name);
            q_pos += name.length();
        }

        // Check if file exists and is readable
        std::ifstream f(filename);
        if (f.good()) {
            for (int i = 0; i < argCount; i++) vm->pop();
            vm->push(Value::runtimeString(vm->internString(filename)));
            return true;
        }

        errors += "\n\tno file '" + filename + "'";
        
        if (end == std::string::npos) break;
        start = end + 1;
    }

    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(Value::nil());
    vm->push(Value::runtimeString(vm->internString(errors)));
    vm->currentCoroutine()->lastResultCount = 2;
    return true;
}

bool native_package_loadlib(VM* vm, int argCount) {
    if (argCount != 2) {
        vm->runtimeError("loadlib expects 2 arguments");
        return false;
    }
    Value funcname_val = vm->peek(0);
    Value libname_val = vm->peek(1);
    
    if (!libname_val.isString() || !funcname_val.isString()) {
        vm->runtimeError("loadlib expects string arguments");
        return false;
    }

    std::string libname = vm->getStringValue(libname_val);
    std::string funcname = vm->getStringValue(funcname_val);
    
    vm->pop();
    vm->pop();

    void* handle = nullptr;
    void* func = nullptr;
    std::string errorMsg;
    std::string errorType;

#ifdef _WIN32
    handle = LoadLibraryA(libname.c_str());
    if (!handle) {
        errorMsg = "cannot open " + libname + ": LoadLibrary failed";
        errorType = "open";
    } else if (funcname == "*") {
        vm->push(Value::boolean(true));
        vm->currentCoroutine()->lastResultCount = 1;
        return true;
    } else {
        func = (void*)GetProcAddress((HMODULE)handle, funcname.c_str());
        if (!func) {
            errorMsg = "no field " + funcname + " in " + libname;
            errorType = "init";
        }
    }
#else
    handle = dlopen(libname.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        const char* err = dlerror();
        errorMsg = err ? err : "unknown error";
        errorType = "open";
    } else if (funcname == "*") {
        vm->push(Value::boolean(true));
        vm->currentCoroutine()->lastResultCount = 1;
        return true;
    } else {
        func = dlsym(handle, funcname.c_str());
        if (!func) {
            const char* err = dlerror();
            errorMsg = err ? err : "unknown error";
            errorType = "init";
        }
    }
#endif

    if (!func) {
        vm->push(Value::nil());
        vm->push(Value::runtimeString(vm->internString(errorMsg)));
        vm->push(Value::runtimeString(vm->internString(errorType)));
        vm->currentCoroutine()->lastResultCount = 3;
        return true;
    }

    // Push the C function as a C_FUNCTION value
    vm->push(Value::cFunction(func));
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

} // anonymous namespace

void registerBaseLibrary(VM* vm) {
    size_t gcIdx = vm->registerNativeFunction("collectgarbage", native_collectgarbage);
    vm->setGlobal("collectgarbage", Value::nativeFunction(gcIdx));

    size_t testUdIdx = vm->registerNativeFunction("__test_userdata", native_test_userdata);
    vm->setGlobal("__test_userdata", Value::nativeFunction(testUdIdx));

    size_t printIdx = vm->registerNativeFunction("print", native_print);
    vm->setGlobal("print", Value::nativeFunction(printIdx));

    size_t sleepIdx = vm->registerNativeFunction("sleep", native_sleep);
    vm->setGlobal("sleep", Value::nativeFunction(sleepIdx));

    size_t setmtIdx = vm->registerNativeFunction("setmetatable", native_setmetatable);
    vm->setGlobal("setmetatable", Value::nativeFunction(setmtIdx));

    size_t getmtIdx = vm->registerNativeFunction("getmetatable", native_getmetatable);
    vm->setGlobal("getmetatable", Value::nativeFunction(getmtIdx));
    
    size_t tostringIdx = vm->registerNativeFunction("tostring", native_tostring);
    vm->setGlobal("tostring", Value::nativeFunction(tostringIdx));
    
    size_t typeIdx = vm->registerNativeFunction("type", [](VM* vm, int argCount) -> bool {
        if (argCount < 1) {
            vm->runtimeError("bad argument #1 to 'type' (value expected)");
            return false;
        }
        Value val = vm->peek(argCount - 1);
        std::string typeName = val.typeToString();
        for (int i = 0; i < argCount; i++) vm->pop();
        StringObject* str = vm->internString(typeName);
        vm->push(Value::runtimeString(str));
        vm->currentCoroutine()->lastResultCount = 1;
        return true;
    });
    vm->setGlobal("type", Value::nativeFunction(typeIdx));
    
    size_t nextIdx = vm->registerNativeFunction("next", native_next);
    g_next_idx = nextIdx;
    vm->setGlobal("next", Value::nativeFunction(nextIdx));
    
    size_t pairsIdx = vm->registerNativeFunction("pairs", native_pairs);
    vm->setGlobal("pairs", Value::nativeFunction(pairsIdx));
    
    size_t ipairsIterIdx = vm->registerNativeFunction("__ipairs_iter", native_ipairs_iter);
    g_ipairs_iter_idx = ipairsIterIdx;
    vm->setGlobal("__ipairs_iter", Value::nativeFunction(ipairsIterIdx));
    
    size_t ipairsIdx = vm->registerNativeFunction("ipairs", native_ipairs);
    vm->setGlobal("ipairs", Value::nativeFunction(ipairsIdx));

    size_t errorIdx = vm->registerNativeFunction("error", native_error);
    vm->setGlobal("error", Value::nativeFunction(errorIdx));

    size_t assertIdx = vm->registerNativeFunction("assert", native_assert);
    vm->setGlobal("assert", Value::nativeFunction(assertIdx));

    size_t loadfileIdx = vm->registerNativeFunction("loadfile", native_loadfile);
    vm->setGlobal("loadfile", Value::nativeFunction(loadfileIdx));

    size_t dofileIdx = vm->registerNativeFunction("dofile", native_dofile);
    vm->setGlobal("dofile", Value::nativeFunction(dofileIdx));

    size_t loadIdx = vm->registerNativeFunction("load", native_load);
    vm->setGlobal("load", Value::nativeFunction(loadIdx));

    size_t pcallIdx = vm->registerNativeFunction("pcall", native_pcall);
    vm->setGlobal("pcall", Value::nativeFunction(pcallIdx));

    size_t xpcallIdx = vm->registerNativeFunction("xpcall", native_xpcall);
    vm->setGlobal("xpcall", Value::nativeFunction(xpcallIdx));

    size_t selectIdx = vm->registerNativeFunction("select", native_select);
    vm->setGlobal("select", Value::nativeFunction(selectIdx));

    size_t tonumberIdx = vm->registerNativeFunction("tonumber", native_tonumber);
    vm->setGlobal("tonumber", Value::nativeFunction(tonumberIdx));

    size_t rawgetIdx = vm->registerNativeFunction("rawget", native_rawget);
    vm->setGlobal("rawget", Value::nativeFunction(rawgetIdx));

    size_t rawsetIdx = vm->registerNativeFunction("rawset", native_rawset);
    vm->setGlobal("rawset", Value::nativeFunction(rawsetIdx));

    size_t rawequalIdx = vm->registerNativeFunction("rawequal", native_rawequal);
    vm->setGlobal("rawequal", Value::nativeFunction(rawequalIdx));

    size_t rawlenIdx = vm->registerNativeFunction("rawlen", native_rawlen);
    vm->setGlobal("rawlen", Value::nativeFunction(rawlenIdx));

    size_t vmjitIdx = vm->registerNativeFunction("vm_jit", native_vm_jit);
    vm->setGlobal("vm_jit", Value::nativeFunction(vmjitIdx));

    size_t warnIdx = vm->registerNativeFunction("warn", native_warn);
    vm->setGlobal("warn", Value::nativeFunction(warnIdx));

    StringObject* verStr = vm->internString("Lua 5.5");
    vm->setGlobal("_VERSION", Value::runtimeString(verStr));

    TableObject* gTable = vm->createTable();
    vm->setGlobal("_G", Value::table(gTable));
    
    // Populate _G with already defined globals
    for (auto const& [name, value] : vm->globals()) {
        gTable->set(name, value);
    }

    TableObject* package = vm->createTable();
    vm->setGlobal("package", Value::table(package));
    
    vm->addNativeToTable(package, "loadlib", native_package_loadlib);
    vm->addNativeToTable(package, "searchpath", native_package_searchpath);

    // package.config
    // 1: directory separator
    // 2: path separator
    // 3: substitution point (?)
    // 4: execution-substitution point (!)
    // 5: ignore-marker (-)
    const char* config = "/\n;\n?\n!\n-";
    package->set("config", Value::runtimeString(vm->internString(config)));

    TableObject* loaded = vm->createTable();
    package->set("loaded", Value::table(loaded));
    loaded->set("package", Value::table(package));
    loaded->set("_G", Value::table(gTable));

    // Check if -E flag was used
    bool ignoreEnv = vm->getGlobal("__IGNORE_ENV__").asBool();

    auto expandPath = [](const char* envPath, const std::string& defaultPath) -> std::string {
        if (!envPath) return defaultPath;
        std::string pathStr = envPath;
        size_t pos = pathStr.find(";;");
        if (pos != std::string::npos) {
            std::string expanded = pathStr.substr(0, pos) + ";" + defaultPath + ";" + pathStr.substr(pos + 2);
            // Clean up leading/trailing semicolons if present
            if (!expanded.empty() && expanded.front() == ';') expanded.erase(0, 1);
            if (!expanded.empty() && expanded.back() == ';') expanded.pop_back();
            return expanded;
        }
        return pathStr;
    };

    const char* envPath = ignoreEnv ? nullptr : getenv("LUA_PATH_5_5");
    if (!envPath && !ignoreEnv) envPath = getenv("LUA_PATH");
    std::string path = expandPath(envPath, "./?.lua;./?/init.lua");
    package->set("path", Value::runtimeString(vm->internString(path)));

    const char* envCpath = ignoreEnv ? nullptr : getenv("LUA_CPATH_5_5");
    if (!envCpath && !ignoreEnv) envCpath = getenv("LUA_CPATH");
    std::string cpath = expandPath(envCpath, "./?.so;./?.dll;./lua/?.so");
    package->set("cpath", Value::runtimeString(vm->internString(cpath)));

    TableObject* preload = vm->createTable();
    package->set("preload", Value::table(preload));

    TableObject* searchers = vm->createTable();
    package->set("searchers", Value::table(searchers));

    const char* requireScript = 
        "local _PACKAGE = package\n"
        "_PACKAGE.searchers[1] = function(modname)\n"
        "    if _PACKAGE.preload[modname] ~= nil then return _PACKAGE.preload[modname], ':preload:' end\n"
        "    return \"\\n\\tno field package.preload['\" .. modname .. \"']\"\n"
        "end\n"
        "_PACKAGE.searchers[2] = function(modname)\n"
        "    if type(_PACKAGE.path) ~= 'string' then error(\"'package.path' must be a string\") end\n"
        "    local filename, err = _PACKAGE.searchpath(modname, _PACKAGE.path)\n"
        "    if not filename then return err end\n"
        "    local f, e = loadfile(filename)\n"
        "    if not f then error(\"error loading module '\" .. modname .. \"' from file '\" .. filename .. \"':\\n\\t\" .. e, 0) end\n"
        "    return f, filename\n"
        "end\n"
        "_PACKAGE.searchers[3] = function(modname)\n"
        "    if type(_PACKAGE.cpath) ~= 'string' then error(\"'package.cpath' must be a string\") end\n"
        "    local filename, err = _PACKAGE.searchpath(modname, _PACKAGE.cpath)\n"
        "    if not filename then return err end\n"
        "    local modprefix = string.match(modname, '^([^-]+)') or modname\n"
        "    local openname = 'luaopen_' .. string.gsub(modprefix, '%.', '_')\n"
        "    local f, e = _PACKAGE.loadlib(filename, openname)\n"
        "    if not f then error(\"error loading module '\" .. modname .. \"' from file '\" .. filename .. \"':\\n\\t\" .. e, 0) end\n"
        "    return f, filename\n"
        "end\n"
        "_PACKAGE.searchers[4] = function(modname)\n"
        "    if type(_PACKAGE.cpath) ~= 'string' then error(\"'package.cpath' must be a string\") end\n"
        "    local root = string.match(modname, '^([^.]+)%.')\n"
        "    if not root then return nil end\n"
        "    local filename, err = _PACKAGE.searchpath(root, _PACKAGE.cpath)\n"
        "    if not filename then return err end\n"
        "    local openname = 'luaopen_' .. string.gsub(modname, '%.', '_')\n"
        "    local f, e = _PACKAGE.loadlib(filename, openname)\n"
        "    if not f then error(\"error loading module '\" .. modname .. \"' from file '\" .. filename .. \"':\\n\\t\" .. e, 0) end\n"
        "    return f, filename\n"
        "end\n"
        "function require(modname)\n"
        "    if type(_PACKAGE.searchers) ~= 'table' then error(\"'package.searchers' must be a table\") end\n"
        "    if _PACKAGE.loaded[modname] then return _PACKAGE.loaded[modname] end\n"
        "    local errors = ''\n"
        "    for i=1, #_PACKAGE.searchers do\n"
        "        local searcher = _PACKAGE.searchers[i]\n"
        "        local loader, data = searcher(modname)\n"
        "        if type(loader) == 'function' then\n"
        "            local res = loader(modname, data)\n"
        "            if res ~= nil then\n"
        "                _PACKAGE.loaded[modname] = res\n"
        "            elseif not _PACKAGE.loaded[modname] then\n"
        "                _PACKAGE.loaded[modname] = true\n"
        "            end\n"
        "            return _PACKAGE.loaded[modname], data\n"
        "        elseif type(loader) == 'string' then\n"
        "            errors = errors .. loader\n"
        "        end\n"
        "    end\n"
        "    error(\"module '\" .. modname .. \"' not found:\" .. errors, 0)\n"
        "end\n";

    vm->runSource(requireScript, "require_init");
}
