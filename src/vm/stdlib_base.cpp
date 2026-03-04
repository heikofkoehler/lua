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
        return true;
    } else if (opt == "isrunning") {
        for(int i=0; i<argCount; i++) vm->pop();
        vm->push(Value::boolean(true));
        return true;
    } else if (opt == "incremental") {
        VM::GCMode old = vm->gcMode();
        vm->setGCMode(VM::GCMode::INCREMENTAL);
        for(int i=0; i<argCount; i++) vm->pop();
        vm->push(Value::runtimeString(vm->internString(old == VM::GCMode::INCREMENTAL ? "incremental" : "generational")));
        return true;
    } else if (opt == "generational") {
        VM::GCMode old = vm->gcMode();
        vm->setGCMode(VM::GCMode::GENERATIONAL);
        for(int i=0; i<argCount; i++) vm->pop();
        vm->push(Value::runtimeString(vm->internString(old == VM::GCMode::INCREMENTAL ? "incremental" : "generational")));
        return true;
    } else if (opt == "step") {
        vm->collectGarbage();
        for(int i=0; i<argCount; i++) vm->pop();
        vm->push(Value::boolean(true));
        return true;
    } else if (opt == "stop" || opt == "restart") {
        // Stubs for these commands
        for(int i=0; i<argCount; i++) vm->pop();
        vm->push(Value::boolean(true));
        return true;
    }
 else if (opt == "param") {
        // collectgarbage("param", name, [newvalue])
        if (argCount >= 2) {
            std::string param = vm->getStringValue(vm->peek(argCount - 2));
            if (argCount >= 3) {
                // Setting a value (stub)
                double val = vm->peek(0).asNumber();
                for(int i=0; i<argCount; i++) vm->pop();
                vm->push(Value::number(val)); // Return new value
                return true;
            } else {
                // Getting a value (stub)
                for(int i=0; i<argCount; i++) vm->pop();
                vm->push(Value::number(100)); // Return a default value
                return true;
            }
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
        return true;
    }

    // Default: full collect
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

    if (!metatable.isTable() && !metatable.isNil()) {
        vm->runtimeError("bad argument #2 to 'setmetatable' (table or nil expected)");
        return false;
    }

    table.asTableObj()->setMetatable(metatable);

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
        mt = obj.asTableObj()->getMetatable();
    } else {
        mt = vm->getTypeMetatable(obj.type());
    }

    vm->pop();
    vm->push(mt);
    return true;
}

bool native_tostring(VM* vm, int argCount) {
    if (argCount != 1) {
        vm->runtimeError("tostring expects 1 argument");
        return false;
    }
    Value val = vm->peek(0);
    
    Value mm = vm->getMetamethod(val, "__tostring");
    if (!mm.isNil()) {
        // Pop the original argument first so it doesn't get counted as a result
        vm->pop();
        
        vm->push(mm);
        vm->push(val);
        
        size_t prevFrames = vm->currentCoroutine()->frames.size();
        if (vm->callValue(1, 2)) {
            if (vm->currentCoroutine()->frames.size() > prevFrames) {
                if (!vm->run(prevFrames)) return false;
            }
            // Result is now on top of the stack
            vm->currentCoroutine()->lastResultCount = 1;
            return true;
        }
        return false;
    }

    std::string str = vm->getStringValue(val);
    vm->pop();
    vm->push(Value::runtimeString(vm->internString(str)));
    return true;
}

bool native_tonumber(VM* vm, int argCount) {
    if (argCount < 1 || argCount > 2) {
        vm->runtimeError("tonumber expects 1 or 2 arguments");
        return false;
    }
    Value val = vm->peek(argCount - 1);

    std::string s = vm->getStringValue(val);

    try {
        size_t pos;
        double num = std::stod(s, &pos);
        for (int i = 0; i < argCount; i++) vm->pop();
        if (pos != s.length()) {
            vm->push(Value::nil());
        } else {
            vm->push(Value::number(num));
        }
    } catch (...) {
        for (int i = 0; i < argCount; i++) vm->pop();
        vm->push(Value::nil());
    }

    return true;
}

bool native_print(VM* vm, int argCount) {
    for (int i = 0; i < argCount; i++) {
        Value val = vm->peek(argCount - 1 - i);
        std::cout << val.toString();
        if (i < argCount - 1) std::cout << "\t";
    }
    std::cout << std::endl;

    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(Value::nil());
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
    vm->push(Value::nil());
    return true;
}

bool native_next(VM* vm, int argCount) {
    if (argCount < 1 || argCount > 2) {
        vm->runtimeError("next expects 1 or 2 arguments");
        return false;
    }
    Value key = (argCount == 2) ? vm->peek(0) : Value::nil();
    Value tableVal = vm->peek(argCount - 1);

    if (!tableVal.isTable()) {
        vm->runtimeError("bad argument #1 to 'next' (table expected)");
        return false;
    }

    TableObject* table = tableVal.asTableObj();
    auto result = table->next(key);

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
    if (argCount != 1) {
        vm->runtimeError("pairs expects 1 argument");
        return false;
    }
    Value table = vm->peek(0);
    if (!table.isTable()) {
        vm->runtimeError("bad argument #1 to 'pairs' (table expected)");
        return false;
    }

    size_t nextIdx = vm->registerNativeFunction("next", native_next);
    vm->pop();
    vm->push(Value::nativeFunction(nextIdx));
    vm->push(table);
    vm->push(Value::nil());
    vm->currentCoroutine()->lastResultCount = 3;
    return true;
}

bool native_ipairs_iter(VM* vm, int argCount) {
    if (argCount != 2) return false;
    Value indexVal = vm->peek(0);
    Value tableVal = vm->peek(1);

    if (!tableVal.isTable()) return false;
    TableObject* table = tableVal.asTableObj();
    double nextIndex = indexVal.asNumber() + 1;
    Value val = table->get(Value::number(nextIndex));

    vm->pop(); vm->pop();
    if (val.isNil()) {
        vm->push(Value::nil());
        vm->currentCoroutine()->lastResultCount = 1;
    } else {
        vm->push(Value::number(nextIndex));
        vm->push(val);
        vm->currentCoroutine()->lastResultCount = 2;
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

    size_t iterIdx = vm->registerNativeFunction("__ipairs_iter", native_ipairs_iter);
    vm->pop();
    vm->push(Value::nativeFunction(iterIdx));
    vm->push(table);
    vm->push(Value::number(0));
    vm->currentCoroutine()->lastResultCount = 3;
    return true;
}

bool native_error(VM* vm, int argCount) {
    std::string msg = "nil";
    int level = 1;
    if (argCount >= 1) {
        msg = vm->peek(argCount - 1).toString();
        if (argCount >= 2) {
            level = static_cast<int>(vm->peek(argCount - 2).asNumber());
        }
    }
    vm->runtimeError(msg, level);
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
    vm->pop(); vm->pop();
    vm->push(table.asTableObj()->get(key));
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
    table.asTableObj()->set(key, val);
    vm->pop(); vm->pop(); vm->pop();
    vm->push(table);
    return true;
}

bool native_warn(VM* vm, int argCount) {
    std::cerr << "Lua Warning: ";
    for (int i = 0; i < argCount; i++) {
        std::cerr << vm->peek(argCount - 1 - i).toString();
    }
    std::cerr << std::endl;
    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(Value::nil());
    return true;
}

bool native_loadfile(VM* vm, int argCount) {
    if (argCount < 1) {
        vm->runtimeError("loadfile expects at least 1 argument");
        return false;
    }
    std::string path = vm->getStringValue(vm->peek(argCount - 1));
    for(int i=0; i<argCount; i++) vm->pop();
    std::string sourceName = "@" + path;

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        vm->push(Value::nil());
        StringObject* errStr = vm->internString("Could not open file: " + path);
        vm->push(Value::runtimeString(errStr));
        return true;
    }

    // Check for signature
    char sig[4];
    file.read(sig, 4);
    if (file.gcount() == 4 && std::memcmp(sig, "\x1bLua", 4) == 0) {
        auto function = FunctionObject::deserialize(file);
        if (!function) {
            vm->push(Value::nil());
            vm->push(Value::runtimeString(vm->internString("Could not deserialize bytecode in " + path)));
            return true;
        }
        FunctionObject* funcPtr = function.get();
        vm->registerFunction(function.release());
        vm->setSourceName(sourceName);
        ClosureObject* closure = vm->createClosure(funcPtr);
        vm->setupRootUpvalues(closure);
        vm->push(Value::closure(closure));
        return true;
    }

    // Not bytecode, read as source
    file.clear();
    file.seekg(0);
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();

    try {
        Lexer lexer(source);
        lexer.setSourceName(sourceName);
        Parser parser(lexer);
        auto program = parser.parse();
        if (!program) {
            vm->push(Value::nil());
            StringObject* errStr = vm->internString("Parse error in " + path);
            vm->push(Value::runtimeString(errStr));
            return true;
        }

        CodeGenerator codegen;
        auto function = codegen.generate(program.get(), "@" + path);
        if (!function) {
            vm->push(Value::nil());
            StringObject* errStr = vm->internString("Code generation error in " + path);
            vm->push(Value::runtimeString(errStr));
            return true;
        }

        FunctionObject* funcPtr = function.get();
        vm->registerFunction(function.release());
        vm->setSourceName(sourceName);
        ClosureObject* closure = vm->createClosure(funcPtr);
        vm->setupRootUpvalues(closure);
        vm->push(Value::closure(closure));
        return true;

    } catch (const CompileError& e) {
        vm->push(Value::nil());
        StringObject* errStr = vm->internString(e.what());
        vm->push(Value::runtimeString(errStr));
        return true;
    } catch (const std::exception& e) {
        vm->push(Value::nil());
        StringObject* errStr = vm->internString(e.what());
        vm->push(Value::runtimeString(errStr));
        return true;
    }
}

bool native_load(VM* vm, int argCount) {
    if (argCount < 1) {
        vm->runtimeError("load expects at least 1 argument");
        return false;
    }
    std::string source = vm->getStringValue(vm->peek(argCount - 1));
    for(int i=0; i<argCount; i++) vm->pop();
    std::string sourceName = "[string \"load\"]";

    if (source.length() >= 4 && std::memcmp(source.data(), "\x1bLua", 4) == 0) {
        std::istringstream is(source.substr(4), std::ios::binary);
        auto function = FunctionObject::deserialize(is);
        if (!function) {
            vm->push(Value::nil());
            vm->push(Value::runtimeString(vm->internString("Could not deserialize bytecode")));
            return true;
        }
        FunctionObject* funcPtr = function.get();
        vm->registerFunction(function.release());
        vm->setSourceName(sourceName);
        ClosureObject* closure = vm->createClosure(funcPtr);
        vm->setupRootUpvalues(closure);
        vm->push(Value::closure(closure));
        return true;
    }

    try {
        Lexer lexer(source);
        lexer.setSourceName(sourceName);
        Parser parser(lexer);
        auto program = parser.parse();
        if (!program) {
            vm->push(Value::nil());
            vm->push(Value::runtimeString(vm->internString("parse error")));
            return true;
        }

        CodeGenerator codegen;
        auto function = codegen.generate(program.get(), "[string \"load\"]");
        if (!function) {
            vm->push(Value::nil());
            vm->push(Value::runtimeString(vm->internString("code generation error")));
            return true;
        }

        FunctionObject* funcPtr = function.get();
        vm->registerFunction(function.release());
        vm->setSourceName(sourceName);
        ClosureObject* closure = vm->createClosure(funcPtr);
        vm->setupRootUpvalues(closure);
        vm->push(Value::closure(closure));
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
    return true;
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
    } else {
        func = (void*)GetProcAddress((HMODULE)handle, funcname.c_str());
        if (!func) {
            errorMsg = "no field " + funcname + " in " + libname;
            errorType = "init";
        }
    }
#else
    handle = dlopen(libname.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        const char* err = dlerror();
        errorMsg = err ? err : "unknown error";
        errorType = "open";
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

    // Register the C function
    NativeFunction nativeFunc = reinterpret_cast<NativeFunction>(func);
    size_t funcIndex = vm->registerNativeFunction(funcname, nativeFunc);
    vm->push(Value::nativeFunction(funcIndex));
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
        if (argCount != 1) {
            vm->runtimeError("type expects 1 argument");
            return false;
        }
        Value val = vm->peek(0);
        std::string typeName = val.typeToString();
        vm->pop();
        StringObject* str = vm->internString(typeName);
        vm->push(Value::runtimeString(str));
        return true;
    });
    vm->setGlobal("type", Value::nativeFunction(typeIdx));
    
    size_t nextIdx = vm->registerNativeFunction("next", native_next);
    vm->setGlobal("next", Value::nativeFunction(nextIdx));
    
    size_t pairsIdx = vm->registerNativeFunction("pairs", native_pairs);
    vm->setGlobal("pairs", Value::nativeFunction(pairsIdx));
    
    size_t ipairsIterIdx = vm->registerNativeFunction("__ipairs_iter", native_ipairs_iter);
    vm->setGlobal("__ipairs_iter", Value::nativeFunction(ipairsIterIdx));
    
    size_t ipairsIdx = vm->registerNativeFunction("ipairs", native_ipairs);
    vm->setGlobal("ipairs", Value::nativeFunction(ipairsIdx));

    size_t errorIdx = vm->registerNativeFunction("error", native_error);
    vm->setGlobal("error", Value::nativeFunction(errorIdx));

    size_t assertIdx = vm->registerNativeFunction("assert", native_assert);
    vm->setGlobal("assert", Value::nativeFunction(assertIdx));

    size_t loadfileIdx = vm->registerNativeFunction("loadfile", native_loadfile);
    vm->setGlobal("loadfile", Value::nativeFunction(loadfileIdx));

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

    size_t warnIdx = vm->registerNativeFunction("warn", native_warn);
    vm->setGlobal("warn", Value::nativeFunction(warnIdx));

    StringObject* verStr = vm->internString("Lua 5.5");
    vm->setGlobal("_VERSION", Value::runtimeString(verStr));

    TableObject* package = vm->createTable();
    vm->setGlobal("package", Value::table(package));
    
    vm->addNativeToTable(package, "loadlib", native_package_loadlib);

    TableObject* loaded = vm->createTable();
    package->set("loaded", Value::table(loaded));

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

    const char* requireScript = 
        "function require(modname)\n"
        "    if package.loaded[modname] then return package.loaded[modname] end\n"
        "    local errors = \"\"\n"
        "    \n"
        "    -- Search in package.path\n"
        "    local path = package.path .. \";\"\n"
        "    local start = 1\n"
        "    while true do\n"
        "        local sep = string.find(path, \";\", start)\n"
        "        if not sep then break end\n"
        "        local template = string.sub(path, start, sep - 1)\n"
        "        local filename = string.gsub(template, \"?\", modname)\n"
        "        local f, err = loadfile(filename)\n"
        "        if f then\n"
        "            local res = f(modname)\n"
        "            if res == nil then res = true end\n"
        "            package.loaded[modname] = res\n"
        "            return res\n"
        "        end\n"
        "        errors = errors .. \"\\n\\tno file '\" .. filename .. \"'\"\n"
        "        start = sep + 1\n"
        "    end\n"
        "    \n"
        "    -- Search in package.cpath\n"
        "    local cpath = package.cpath .. \";\"\n"
        "    start = 1\n"
        "    while true do\n"
        "        local sep = string.find(cpath, \";\", start)\n"
        "        if not sep then break end\n"
        "        local template = string.sub(cpath, start, sep - 1)\n"
        "        local filename = string.gsub(template, \"?\", modname)\n"
        "        -- For C modules, we'd use package.loadlib\n"
        "        -- The function name is usually luaopen_ + modname (replacing . with _)\n"
        "        local openname = \"luaopen_\" .. string.gsub(modname, \"%%.\", \"_\")\n"
        "        local f, err = package.loadlib(filename, openname)\n"
        "        if f then\n"
        "            local res = f(modname)\n"
        "            if res == nil then res = true end\n"
        "            package.loaded[modname] = res\n"
        "            return res\n"
        "        end\n"
        "        errors = errors .. \"\\n\\tno file '\" .. filename .. \"' (C module)\"\n"
        "        start = sep + 1\n"
        "    end\n"
        "    \n"
        "    error(\"module '\" .. modname .. \"' not found:\" .. errors)\n"
        "end\n";

    vm->runSource(requireScript, "require_init");
}
