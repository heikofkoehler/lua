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
        Value val = vm->peek(argCount - 1 - i);
        std::cout << vm->getStringValue(val);
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
    Value val = vm->peek(0);
    if (!val.isNumber()) {
        vm->runtimeError("sleep expects number argument (seconds)");
        return false;
    }
    double seconds = val.asNumber();
    
    // Pop args
    for(int i=0; i<argCount; i++) vm->pop();

    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(seconds * 1000)));
    vm->push(Value::nil());
    return true;
}

bool native_collectgarbage(VM* vm, int argCount) {
    bool isCount = false;
    if (argCount > 0) {
        Value arg = vm->peek(argCount - 1);
        if (arg.isString()) {
            std::string s = vm->getStringValue(arg);
            if (s == "count") {
                isCount = true;
            }
        }
    }
    
    if (isCount) {
        double count = static_cast<double>(vm->bytesAllocated()) / 1024.0;
        for(int i=0; i<argCount; i++) vm->pop();
        vm->push(Value::number(count));
        return true;
    }
    
    vm->collectGarbage();
    for(int i=0; i<argCount; i++) vm->pop();
    vm->push(Value::nil());
    return true;
}

bool native_setmetatable(VM* vm, int argCount) {
    if (argCount != 2) {
        vm->runtimeError("setmetatable expects 2 arguments");
        return false;
    }
    Value metatable = vm->peek(0);
    Value table = vm->peek(1);

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
    
    for(int i=0; i<argCount; i++) vm->pop();
    vm->push(table);
    return true;
}

bool native_getmetatable(VM* vm, int argCount) {
    if (argCount != 1) {
        vm->runtimeError("getmetatable expects 1 argument");
        return false;
    }
    Value obj = vm->peek(0);
    Value mt = Value::nil();

    if (obj.isTable()) {
        TableObject* t = vm->getTable(obj.asTableIndex());
        mt = t->getMetatable();
    }
    
    for(int i=0; i<argCount; i++) vm->pop();
    vm->push(mt);
    return true;
}

bool native_tostring(VM* vm, int argCount) {
    if (argCount != 1) {
        vm->runtimeError("tostring expects 1 argument");
        return false;
    }
    Value val = vm->peek(0);
    
    // Check for __tostring metamethod
    Value meta = vm->getMetamethod(val, "__tostring");
    if (!meta.isNil()) {
        // metamethod call will handle its own popping
        vm->push(meta);
        vm->push(val);
        // Wait, we need to pop our original tostring arg first?
        // standard Lua: tostring(val) calls val:__tostring()
        // If we callValue here, it will push results.
        // But native_tostring itself is a native function.
        
        // Let's just pop and tail-call?
        vm->pop(); // pop val
        vm->push(meta);
        vm->push(val);
        return vm->callValue(1, 2); // Expect 1 result (1+1=2)
    }
    
    // Default conversion
    std::string str = vm->getStringValue(val);
    vm->pop();
    size_t strIdx = vm->internString(str);
    vm->push(Value::runtimeString(strIdx));
    return true;
}

bool native_tonumber(VM* vm, int argCount) {
    if (argCount < 1 || argCount > 2) {
        vm->runtimeError("tonumber expects 1 or 2 arguments");
        return false;
    }
    
    Value baseVal = (argCount == 2) ? vm->peek(0) : Value::number(10);
    Value val = vm->peek(argCount - 1);
    
    Value result = Value::nil();
    bool error = false;

    if (val.isNumber() && baseVal.asNumber() == 10) {
        result = val;
    } else if (!val.isString()) {
        result = Value::nil();
    } else {
        std::string s = vm->getStringValue(val);
        int base = static_cast<int>(baseVal.asNumber());
        
        if (base < 2 || base > 36) {
            vm->runtimeError("base out of range");
            error = true;
        } else {
            try {
                size_t pos;
                if (base == 10) {
                    double num = std::stod(s, &pos);
                    if (pos == s.length()) result = Value::number(num);
                } else {
                    long num = std::stol(s, &pos, base);
                    if (pos == s.length()) result = Value::number(static_cast<double>(num));
                }
            } catch (...) {}
        }
    }
    
    if (error) return false;
    
    for(int i=0; i<argCount; i++) vm->pop();
    vm->push(result);
    return true;
}

bool native_rawget(VM* vm, int argCount) {
    if (argCount != 2) {
        vm->runtimeError("rawget expects 2 arguments");
        return false;
    }
    Value key = vm->peek(0);
    Value table = vm->peek(1);
    
    if (!table.isTable()) {
        vm->runtimeError("bad argument #1 to 'rawget' (table expected)");
        return false;
    }
    
    TableObject* t = vm->getTable(table.asTableIndex());
    Value res = t->get(key);
    
    for(int i=0; i<argCount; i++) vm->pop();
    vm->push(res);
    return true;
}

bool native_rawset(VM* vm, int argCount) {
    if (argCount != 3) {
        vm->runtimeError("rawset expects 3 arguments");
        return false;
    }
    Value val = vm->peek(0);
    Value key = vm->peek(1);
    Value table = vm->peek(2);
    
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
    
    for(int i=0; i<argCount; i++) vm->pop();
    vm->push(table);
    return true;
}

bool native_warn(VM* vm, int argCount) {
    for (int i = 0; i < argCount; i++) {
        Value val = vm->peek(argCount - 1 - i);
        std::cerr << "Lua warning: " << vm->getStringValue(val) << std::endl;
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

    TableObject* t = vm->getTable(table.asTableIndex());
    std::pair<Value, Value> nextPair = t->next(key);

    // Pop args BEFORE pushing
    for(int i=0; i<argCount; i++) vm->pop();

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
    
    auto it = vm->globals().find("next");
    if (it == vm->globals().end()) {
        vm->runtimeError("global 'next' not found");
        return false;
    }
    Value nextFunc = it->second;
    
    // Pop argument
    vm->pop();
    
    vm->push(nextFunc); // next
    vm->push(table);      // table
    vm->push(Value::nil()); // initial key
    return true;
}

bool native_ipairs_iter(VM* vm, int argCount) {
    if (argCount != 2) {
        vm->runtimeError("ipairs iterator expects 2 arguments");
        return false;
    }
    Value index = vm->peek(0);
    Value table = vm->peek(1);
    
    if (!index.isNumber()) {
        vm->runtimeError("ipairs iterator expects number index");
        return false;
    }
    
    double i = index.asNumber();
    i += 1;
    
    Value res1 = Value::nil();
    Value res2 = Value::nil();
    bool found = false;

    if (table.isTable()) {
        TableObject* t = vm->getTable(table.asTableIndex());
        Value val = t->get(Value::number(i));
        
        if (!val.isNil()) {
            res1 = Value::number(i);
            res2 = val;
            found = true;
        }
    }
    
    for(int i=0; i<argCount; i++) vm->pop();

    if (found) {
        vm->push(res1);
        vm->push(res2);
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
    
    auto it = vm->globals().find("__ipairs_iter");
    if (it == vm->globals().end()) {
        vm->runtimeError("internal error: __ipairs_iter not found");
        return false;
    }
    Value iterFunc = it->second;
    
    // Pop argument
    vm->pop();
    
    vm->push(iterFunc); // iter
    vm->push(table);      // table
    vm->push(Value::number(0)); // initial index
    return true;
}

bool native_error(VM* vm, int argCount) {
    if (argCount < 1) {
        vm->runtimeError("error expects at least 1 argument");
        return false;
    }
    Value msg = vm->peek(argCount - 1);
    vm->runtimeError(vm->getStringValue(msg));
    return false;
}

bool native_pcall(VM* vm, int argCount) {
    if (argCount < 1) {
        vm->runtimeError("pcall expects at least 1 argument");
        return false;
    }
    return vm->pcall(argCount - 1);
}

bool native_xpcall(VM* vm, int argCount) {
    if (argCount < 2) {
        vm->runtimeError("xpcall expects at least 2 arguments (f, msgh)");
        return false;
    }
    return vm->xpcall(argCount);
}

bool native_load(VM* vm, int argCount) {
    if (argCount < 1) {
        vm->runtimeError("load expects at least 1 argument");
        return false;
    }
    Value chunkVal = vm->peek(argCount - 1);
    if (!chunkVal.isString() && !chunkVal.isRuntimeString()) {
        vm->runtimeError("load expects string argument");
        return false;
    }
    
    std::string source = vm->getStringValue(chunkVal);
    
    for(int i=0; i<argCount; i++) vm->pop();

    try {
        Lexer lexer(source);
        Parser parser(lexer);
        auto program = parser.parse();
        if (!program) {
            vm->push(Value::nil());
            vm->push(Value::runtimeString(vm->internString("parse error")));
            return true;
        }

        CodeGenerator codegen;
        auto chunk = codegen.generate(program.get());
        if (!chunk) {
            vm->push(Value::nil());
            vm->push(Value::runtimeString(vm->internString("code generation error")));
            return true;
        }

        FunctionObject* function = new FunctionObject("load", 0, std::move(chunk));
        vm->registerFunction(function);
        size_t closureIndex = vm->createClosure(function);
        vm->push(Value::closure(closureIndex));
        return true;

    } catch (const CompileError& e) {
        vm->push(Value::nil());
        vm->push(Value::runtimeString(vm->internString(e.what())));
        return true;
    } catch (const std::exception& e) {
        vm->push(Value::nil());
        vm->push(Value::runtimeString(vm->internString(e.what())));
        return true;
    }
}

bool native_select(VM* vm, int argCount) {
    if (argCount < 1) {
        vm->runtimeError("bad argument #1 to 'select' (number expected, got no value)");
        return false;
    }

    Value indexVal = vm->peek(argCount - 1);
    if (indexVal.isString()) {
        std::string s = vm->getStringValue(indexVal);
        if (s == "#") {
            double count = static_cast<double>(argCount - 1);
            for (int i = 0; i < argCount; i++) vm->pop();
            vm->push(Value::number(count));
            return true;
        }
    }

    if (!indexVal.isNumber()) {
        vm->runtimeError("bad argument #1 to 'select' (number expected)");
        return false;
    }

    int n = static_cast<int>(indexVal.asNumber());
    int total = argCount - 1;

    std::vector<Value> results;
    bool outOfRange = false;

    if (n > 0) {
        if (n <= total) {
            for (int i = n - 1; i < total; i++) {
                results.push_back(vm->peek(total - 1 - i));
            }
        }
    } else if (n < 0) {
        n = total + n + 1;
        if (n >= 1) {
            for (int i = n - 1; i < total; i++) {
                results.push_back(vm->peek(total - 1 - i));
            }
        } else {
            outOfRange = true;
        }
    } else {
        outOfRange = true;
    }

    if (outOfRange) {
        vm->runtimeError("bad argument #1 to 'select' (index out of range)");
        return false;
    }
    
    for (int i = 0; i < argCount; i++) vm->pop();
    for (const auto& res : results) vm->push(res);
    vm->currentCoroutine()->lastResultCount = results.size();
    return true;
}

bool native_assert(VM* vm, int argCount) {
    if (argCount < 1) {
        vm->runtimeError("bad argument #1 to 'assert' (value expected)");
        return false;
    }

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
    // Standard Lua assert returns all its arguments.
    // They are already on the stack, but we should set lastResultCount
    vm->currentCoroutine()->lastResultCount = argCount;
    return true;
}

bool native_loadfile(VM* vm, int argCount) {
    if (argCount != 1) {
        vm->runtimeError("loadfile expects 1 argument");
        return false;
    }
    Value pathVal = vm->peek(0);
    if (!pathVal.isString() && !pathVal.isRuntimeString()) {
        vm->runtimeError("loadfile expects string argument");
        return false;
    }

    std::string path = vm->getStringValue(pathVal);
    
    for(int i=0; i<argCount; i++) vm->pop();

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
        Value val = vm->peek(0);
        std::string typeName = val.typeToString();
        
        vm->pop();
        
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

    size_t xpcallIdx = vm->registerNativeFunction("xpcall", native_xpcall);
    vm->globals()["xpcall"] = Value::nativeFunction(xpcallIdx);

    size_t selectIdx = vm->registerNativeFunction("select", native_select);
    vm->globals()["select"] = Value::nativeFunction(selectIdx);

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
