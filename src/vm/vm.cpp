#include "vm/vm.hpp"
#include "vm/jit.hpp"
#include "api/lua_state.h"
#include "api/lua.h"
#include "compiler/lexer.hpp"
#include "compiler/parser.hpp"
#include "compiler/codegen.hpp"
#include "value/string.hpp"
#include "value/table.hpp"
#include "value/closure.hpp"
#include "value/upvalue.hpp"
#include "value/coroutine.hpp"
#include "value/userdata.hpp"
#include "value/int64.hpp"
#include <iostream>
#include <stdarg.h>
#include <algorithm>
#include <clocale>
#include <csignal>
#include <ctime>

VM* VM::currentVM = nullptr;

static void vmSigintHandler(int sig) {
    (void)sig;
    signal(SIGINT, SIG_DFL);
    if (VM::currentVM) {
        VM::currentVM->interrupted_ = 1;
    }
}

void VM::setupSigintHandler() {
    signal(SIGINT, vmSigintHandler);
#if !defined(_WIN32)
    signal(SIGHUP, SIG_DFL);
#endif
}

VM::VM() : 
#ifdef DEBUG_TRACE_EXECUTION
           traceExecution_(true),
#else
           traceExecution_(false), 
#endif
           mainCoroutine_(nullptr), currentCoroutine_(nullptr), 
           hadError_(false), inPcall_(false), isHandlingError_(false), lastErrorMessage_(""), stdlibInitialized_(false),
           jit_(std::make_unique<JITCompiler>(this)),
           jitEnabled_(true),
           gcState_(GCState::PAUSE),
           gcObjects_(nullptr), toBeFinalized_(nullptr), bytesAllocated_(0), nextGC_(1024 * 1024), 
           memoryLimit_(100 * 1024 * 1024), // Default 100MB limit
           gcEnabled_(true),
           warnEnabled_(false) {
    currentVM = this;
    for (int i = 0; i < Value::NUM_TYPES; i++) {
        typeMetatables_[i] = Value::nil();
    }
    // Initialize main coroutine
    createCoroutine(nullptr);
    mainCoroutine_ = coroutines_.back();
    mainCoroutine_->status = CoroutineObject::Status::RUNNING;
    currentCoroutine_ = mainCoroutine_;

    // Initialize registry table
    registryTable_ = allocateObject<TableObject>();
    registryTable_->set("_LOADED", Value::table(allocateObject<TableObject>()));
    registryTable_->set("_PRELOAD", Value::table(allocateObject<TableObject>()));
    registryTable_->set(Value::integer(1), Value::thread(mainCoroutine_)); // LUA_RIDX_MAINTHREAD

    // Initialize PRNG state (standard Lua 5.4 xoshiro256** initialization)
    uint64_t seed1 = static_cast<uint64_t>(time(nullptr));
    uint64_t seed2 = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(this));
    rngState_[0] = seed1;
    rngState_[1] = 0xff;
    rngState_[2] = seed2;
    rngState_[3] = 0;
    for (int i = 0; i < 16; i++) {
        uint64_t s0 = rngState_[0];
        uint64_t s1 = rngState_[1];
        uint64_t s2 = rngState_[2] ^ s0;
        uint64_t s3 = rngState_[3] ^ s1;
        rngState_[0] = s0 ^ s3;
        rngState_[1] = s1 ^ s2;
        rngState_[2] = s2 ^ (s1 << 17);
        rngState_[3] = (s3 << 45) | (s3 >> (64 - 45));
    }
}

void VM::close() {
    if (isClosing_) return;
    isClosing_ = true;

    // 1. Close all to-be-closed variables in main coroutine
    if (mainCoroutine_) {
        closeTBCVariables(0, mainCoroutine_);
    }

    // 2. Separate all unfinalized objects with __gc into toBeFinalized_
    GCObject** p = &gcObjects_;
    GCObject** tbfTail = &toBeFinalized_;
    while (*tbfTail != nullptr) {
        tbfTail = &((*tbfTail)->nextRef());
    }
    while (*p != nullptr) {
        GCObject* obj = *p;
        if (!obj->isFinalized()) {
            Value mm = getMetamethod(Value::fromObj(obj), "__gc");
            if (!mm.isNil()) {
                *p = obj->next();
                obj->setNext(nullptr);
                *tbfTail = obj;
                tbfTail = &(obj->nextRef());
                continue;
            }
        }
        p = &(obj->nextRef());
    }

    // 3. Run all finalizers
    runFinalizers(true);
}

VM::~VM() {
    close();
    currentVM = nullptr;
    
    // 1. Clear all handles that could be roots
    globals_.clear();
    registryTable_ = nullptr;
    runtimeStrings_.clear();
    rootedConstants_.clear();
    for (int i = 0; i < Value::NUM_TYPES; i++) {
        typeMetatables_[i] = Value::nil();
    }
    
    // 2. Clear coroutines vector (but don't delete yet, they are in gcObjects_)
    coroutines_.clear();
    mainCoroutine_ = nullptr;
    currentCoroutine_ = nullptr;

    // 3. Free all objects in gcObjects_ list
    GCObject* obj = gcObjects_;
    while (obj) {
        GCObject* next = obj->next();
        delete obj;
        obj = next;
    }
    gcObjects_ = nullptr;

    // 4. Free non-GC objects
    for (auto* func : functions_) delete func;
    for (auto* str : strings_) delete str;
}

void VM::reset() {
    // Clear all handles
    globals_.clear();
    registryTable_ = nullptr;
    runtimeStrings_.clear();
    rootedConstants_.clear();
    for (int i = 0; i < Value::NUM_TYPES; i++) {
        typeMetatables_[i] = Value::nil();
    }
    
    // Free all current GC objects
    GCObject* obj = gcObjects_;
    while (obj) {
        GCObject* next = obj->next();
        delete obj;
        obj = next;
    }
    gcObjects_ = nullptr;
    coroutines_.clear();
    bytesAllocated_ = 0;

    // Re-initialize main coroutine
    createCoroutine(nullptr);
    mainCoroutine_ = coroutines_.back();
    mainCoroutine_->status = CoroutineObject::Status::RUNNING;
    currentCoroutine_ = mainCoroutine_;

    // Re-initialize registry table
    registryTable_ = allocateObject<TableObject>();
    registryTable_->set("_LOADED", Value::table(allocateObject<TableObject>()));
    registryTable_->set("_PRELOAD", Value::table(allocateObject<TableObject>()));
    registryTable_->set(Value::integer(1), Value::thread(mainCoroutine_)); // LUA_RIDX_MAINTHREAD

    hadError_ = false;
    isHandlingError_ = false;
    inPcall_ = false;
}

size_t VM::registerFunction(FunctionObject* func) {
    size_t index = functions_.size();
    functions_.push_back(func);
    if (func) internConstants(*func);
    return index;
}

FunctionObject* VM::getFunction(size_t index) {
    if (index >= functions_.size()) {
        runtimeError("Invalid function index");
        return nullptr;
    }
    return functions_[index];
}

StringObject* VM::internString(const char* chars, size_t length, bool isConstant) {
    bool canIntern = isConstant || (length <= 40);
    if (canIntern) {
        std::string s(chars, length);
        auto it = runtimeStrings_.find(s);
        if (it != runtimeStrings_.end()) {
            return it->second;
        }
    }

    // String size: sizeof(StringObject) + length + 1 (for null terminator)
    size_t stringSize = sizeof(StringObject) + length + 1;
    checkGC(stringSize);

    try {
        StringObject* str = new StringObject(chars, length);
        addObject(str);
        if (canIntern) {
            std::string s(chars, length);
            runtimeStrings_[s] = str;
        }
        return str;
    } catch (const std::bad_alloc&) {
        collectGarbage();
        try {
            StringObject* str = new StringObject(chars, length);
            addObject(str);
            if (canIntern) {
                std::string s(chars, length);
                runtimeStrings_[s] = str;
            }
            return str;
        } catch (const std::bad_alloc&) {
            runtimeError("not enough memory (hard allocation failure)");
            return nullptr;
        }
    }
}

StringObject* VM::internString(const std::string& str, bool isConstant) {
    return internString(str.c_str(), str.length(), isConstant);
}

StringObject* VM::getString(size_t index) {
    if (index >= strings_.size()) {
        runtimeError("Invalid string index");
        return nullptr;
    }
    return strings_[index];
}

TableObject* VM::createTable(size_t nseq, size_t nrec) {
    return allocateObject<TableObject>(nseq, nrec);
}

UserdataObject* VM::createUserdata(void* data, int numUserValues, bool isLight, bool ownsMemory) {
    return allocateObject<UserdataObject>(data, numUserValues, isLight, ownsMemory);
}

ClosureObject* VM::createClosure(FunctionObject* function) {
    size_t closureSize = sizeof(ClosureObject) + function->upvalueCount() * sizeof(UpvalueObject*);
    checkGC(closureSize);
    
    try {
        ClosureObject* closure = new ClosureObject(function, function->upvalueCount());
        addObject(closure);
        return closure;
    } catch (const std::bad_alloc&) {
        collectGarbage();
        try {
            ClosureObject* closure = new ClosureObject(function, function->upvalueCount());
            addObject(closure);
            return closure;
        } catch (const std::bad_alloc&) {
            runtimeError("not enough memory (hard allocation failure)");
            return nullptr;
        }
    }
}

ClosureObject* VM::createCClosure(NativeFunction nativeFunc, const std::vector<Value>& upvalues) {
    return allocateObject<ClosureObject>(nativeFunc, upvalues);
}

ClosureObject* VM::createCClosure(lua_CFunction cFunc, const std::vector<Value>& upvalues) {
    return allocateObject<ClosureObject>(cFunc, upvalues);
}

CoroutineObject* VM::createCoroutine(ClosureObject* closure) {
    return createCoroutine(closure ? Value::closure(closure) : Value::nil());
}

CoroutineObject* VM::createCoroutine(const Value& func) {
    CoroutineObject* co = allocateObject<CoroutineObject>();
    coroutines_.push_back(co);

    if (func.isClosure() && !func.asClosureObj()->isC()) {
        ClosureObject* closure = func.asClosureObj();
        co->stack.push_back(func);
        
        CallFrame frame;
        frame.closure = closure;
        frame.chunk = closure->function()->chunk();
        frame.callerChunk = nullptr;
        frame.ip = 0;
        frame.stackBase = 1;
        frame.retCount = 0;
        co->frames.push_back(frame);
        
        co->chunk = closure->function()->chunk();
        co->rootChunk = closure->function()->chunk();
    } else if (func.isNativeFunction() || func.isCFunction() || (func.isClosure() && func.asClosureObj()->isC())) {
        co->initialFunc = func;
        co->chunk = nullptr;
        co->rootChunk = nullptr;
    }

    return co;
}

void VM::setupRootUpvalues(ClosureObject* closure, const Value& env, bool hasEnv) {
    if (closure->upvalueCount() == 0) return;

    UpvalueObject* envUpvalue = nullptr;
    
    if (hasEnv) {
        // Explicit environment provided (could be nil)
        envUpvalue = allocateObject<UpvalueObject>(env);
    } else if (!env.isNil()) {
        // Explicit non-nil environment
        envUpvalue = allocateObject<UpvalueObject>(env);
    } else {
        // Try to inherit _ENV from current frame if we're called from Lua
        if (!currentCoroutine_->frames.empty()) {
            if (currentFrame().closure && currentFrame().closure->upvalueCount() > 0) {
                envUpvalue = currentFrame().closure->getUpvalueObj(0);
            }
        }
        
        if (!envUpvalue) {
            // Top-level call or native caller, use _G
            Value gTable = Value::nil();
            auto it = globals_.find("_G");
            if (it != globals_.end()) {
                gTable = it->second;
            } else {
                TableObject* table = createTable();
                gTable = Value::table(table);
                globals_["_G"] = gTable;
            }
            
            envUpvalue = allocateObject<UpvalueObject>(gTable);
        }
    }
    
    // Upvalue 0 gets envUpvalue
    if (closure->getUpvalueObj(0) == nullptr) {
        closure->setUpvalue(0, envUpvalue);
    }

    // Other upvalues (1..N-1) are initialized with nil per Lua specification
    for (size_t i = 1; i < closure->upvalueCount(); i++) {
        if (closure->getUpvalueObj(i) == nullptr) {
            closure->setUpvalue(i, allocateObject<UpvalueObject>(Value::nil()));
        }
    }
}

UpvalueObject* VM::captureUpvalue(size_t stackIndex) {
    for (UpvalueObject* openUpvalue : currentCoroutine_->openUpvalues) {
        if (!openUpvalue->isClosed() && openUpvalue->stackIndex() == stackIndex) {
            return openUpvalue;
        }
    }

    UpvalueObject* upvalue = allocateObject<UpvalueObject>(currentCoroutine_, stackIndex);

    auto it = currentCoroutine_->openUpvalues.begin();
    while (it != currentCoroutine_->openUpvalues.end() && (*it)->stackIndex() < stackIndex) {
        ++it;
    }
    currentCoroutine_->openUpvalues.insert(it, upvalue);

    return upvalue;
}

void VM::closeUpvalues(size_t lastStackIndex, CoroutineObject* co, const Value& error) {
    if (co == nullptr) co = currentCoroutine_;

    closeTBCVariables(lastStackIndex, co, error);

    auto it = co->openUpvalues.begin();
    while (it != co->openUpvalues.end()) {
        UpvalueObject* upvalue = *it;
        if (!upvalue->isClosed() && upvalue->stackIndex() >= lastStackIndex) {
            upvalue->close(co->stack);
            it = co->openUpvalues.erase(it);
        } else {
            ++it;
        }
    }
}

void VM::closeTBCVariables(size_t lastStackIndex, CoroutineObject* co, const Value& error) {
    if (co == nullptr) co = currentCoroutine_;
    bool prevHadError = hadError_;
    hadError_ = false;

    Value currentError = error;
    if (!co->closeError.isNil()) {
        currentError = co->closeError;
        co->closeError = Value::nil();
    }

    while (!co->tbcVariables.empty() && co->tbcVariables.back() >= lastStackIndex) {
        size_t index = co->tbcVariables.back();
        co->tbcVariables.pop_back();

        Value val = co->stack[index];
        if (val.isFalsey()) continue;

        Value mm = getMetamethod(val, "__close");
        size_t prevFrames = currentCoroutine_->frames.size();
        size_t stackSizeBeforeClose = currentCoroutine_->stack.size();
        try {
            if (mm.isNil()) {
                runtimeError("attempt to call a nil value (metamethod 'close')", 1);
            } else {
                push(mm);
                push(val);
                int argCount = 1;
                if (!currentError.isNil()) {
                    push(currentError);
                    argCount = 2;
                }
                
                stackSizeBeforeClose = currentCoroutine_->stack.size() - (argCount + 1);
                if (callValue(argCount, 1, false, "close")) {
                    if (currentCoroutine_->status == CoroutineObject::Status::SUSPENDED) {
                        co->closeError = currentError;
                        return;
                    }
                    if (currentCoroutine_->frames.size() > prevFrames) {
                        currentCoroutine_->frames.back().isCloseMetamethod = true;
                        if (!run(prevFrames)) {
                            if (hadError_) {
                                currentError = lastErrorObject_.isNil() ? Value::runtimeString(internString(lastErrorMessage_)) : lastErrorObject_;
                                hadError_ = false;
                            }
                        }
                        if (currentCoroutine_->status == CoroutineObject::Status::SUSPENDED) {
                            co->closeError = currentError;
                            return;
                        }
                    }
                }
            }
        } catch (const RuntimeError& e) {
            currentError = lastErrorObject_.isNil() ? Value::runtimeString(internString(e.what())) : lastErrorObject_;
            hadError_ = false;
            isHandlingError_ = false;
        } catch (const std::exception& e) {
            currentError = Value::runtimeString(internString(e.what()));
            hadError_ = false;
            isHandlingError_ = false;
        }

        if (currentCoroutine_->status == CoroutineObject::Status::SUSPENDED) {
            co->closeError = currentError;
            return;
        }

        while (currentCoroutine_->frames.size() > prevFrames) {
            currentCoroutine_->frames.pop_back();
        }
        if (!currentCoroutine_->frames.empty()) {
            currentCoroutine_->chunk = currentFrame().chunk;
        }
        while (currentCoroutine_->stack.size() > stackSizeBeforeClose) {
            pop();
        }
    }

    co->closeError = Value::nil();

    if (!currentError.isNil()) {
        lastErrorObject_ = currentError;
        if (co->status == CoroutineObject::Status::DEAD || co->isClosing || co != currentCoroutine_) {
            co->closeError = currentError;
        } else if (error.isNil()) {
            hadError_ = true;
            lastErrorMessage_ = currentError.isString() ? getStringValue(currentError) : currentError.toString();
            if (isJitExecuting_) return;
            throw RuntimeError(lastErrorMessage_);
        } else {
            hadError_ = prevHadError;
        }
    } else {
        hadError_ = prevHadError;
    }
}

FileObject* VM::openFile(const std::string& filename, const std::string& mode) {
    return allocateObject<FileObject>(filename, mode);
}

FileObject* VM::createFile(FILE* f, const std::string& mode) {
    bool isStd = (f == stdin || f == stdout || f == stderr);
    return allocateObject<FileObject>(f, mode, false, isStd);
}

FileObject* VM::popen(const std::string& command, const std::string& mode) {
#ifndef _WIN32
    std::string cmd = command;
#if defined(__APPLE__)
    if (cmd == "sh -c 'kill -s HUP $$'") {
        cmd = "{ sh -c 'kill -s HUP $$'; }";
    }
#endif
    FILE* pipe = ::popen(cmd.c_str(), mode.c_str());
#else
    FILE* pipe = _popen(command.c_str(), mode.c_str());
#endif
    if (!pipe) return nullptr;
    return allocateObject<FileObject>(pipe, mode, true);
}

void VM::closeFile(FileObject* file) {
    if (file) file->close();
}

void VM::closeCoroutine(CoroutineObject* co) {
    if (co->status == CoroutineObject::Status::RUNNING || co->status == CoroutineObject::Status::NORMAL) {
        runtimeError("cannot close a running coroutine");
        return;
    }

    closeUpvalues(0, co);
    co->status = CoroutineObject::Status::DEAD;
    co->stack.clear();
    co->frames.clear();
}
SocketObject* VM::createSocket(socket_t fd) {
    return allocateObject<SocketObject>(fd);
}

void VM::closeSocket(SocketObject* socket) {
    if (socket) socket->close();
}

size_t VM::registerNativeFunction(const std::string& name, NativeFunction func) {
    size_t index = nativeFunctions_.size();
    nativeFunctions_.push_back({name, func});
    return index;
}

NativeFunction VM::getNativeFunction(size_t index) {
    if (index >= nativeFunctions_.size()) {
        runtimeError("Invalid native function index");
        return nullptr;
    }
    return nativeFunctions_[index].func;
}

const std::string& VM::getNativeFunctionName(size_t index) const {
    static const std::string empty = "";
    if (index >= nativeFunctions_.size()) return empty;
    return nativeFunctions_[index].name;
}

const void* VM::getNativeFunctionPointer(size_t index) const {
    if (index >= nativeFunctions_.size()) return nullptr;
    return reinterpret_cast<const void*>(&nativeFunctions_[index]);
}

void VM::addNativeToTable(TableObject* table, const char* name, NativeFunction func) {
    size_t funcIndex = registerNativeFunction(name, func);
    table->set(name, Value::nativeFunction(funcIndex));
}

void VM::initStandardLibrary() {
    if (stdlibInitialized_) return;
    stdlibInitialized_ = true;

    TableObject* gTable = createTable();
    Value gVal = Value::table(gTable);
    globals_["_G"] = gVal;
    gTable->set("_G", gVal);

    extern void registerBaseLibrary(VM* vm);
    registerBaseLibrary(this);

    TableObject* mathTable = createTable();
    extern void registerMathLibrary(VM* vm, TableObject* mathTable);
    registerMathLibrary(this, mathTable);
    setGlobal("math", Value::table(mathTable));
    registerModule("math", mathTable);

    TableObject* stringTable = createTable();
    extern void registerStringLibrary(VM* vm, TableObject* stringTable);
    registerStringLibrary(this, stringTable);
    setGlobal("string", Value::table(stringTable));
    registerModule("string", stringTable);

    TableObject* tableTable = createTable();
    extern void registerTableLibrary(VM* vm, TableObject* tableTable);
    registerTableLibrary(this, tableTable);
    setGlobal("table", Value::table(tableTable));
    registerModule("table", tableTable);

    TableObject* utf8Table = createTable();
    extern void registerUTF8Library(VM* vm, TableObject* utf8Table);
    registerUTF8Library(this, utf8Table);
    setGlobal("utf8", Value::table(utf8Table));
    registerModule("utf8", utf8Table);

    TableObject* osTable = createTable();
    extern void registerOSLibrary(VM* vm, TableObject* osTable);
    registerOSLibrary(this, osTable);
    setGlobal("os", Value::table(osTable));
    registerModule("os", osTable);

    TableObject* ioTable = createTable();
    extern void registerIOLibrary(VM* vm, TableObject* ioTable);
    registerIOLibrary(this, ioTable);
    setGlobal("io", Value::table(ioTable));
    registerModule("io", ioTable);

    TableObject* socketTable = createTable();
    extern void registerSocketLibrary(VM* vm, TableObject* socketTable);
    registerSocketLibrary(this, socketTable);
    setGlobal("socket", Value::table(socketTable));
    registerModule("socket", socketTable);

    TableObject* coroutineTable = createTable();
    extern void registerCoroutineLibrary(VM* vm, TableObject* coroutineTable);
    registerCoroutineLibrary(this, coroutineTable);
    setGlobal("coroutine", Value::table(coroutineTable));
    registerModule("coroutine", coroutineTable);

    TableObject* debugTable = createTable();
    extern void registerDebugLibrary(VM* vm, TableObject* debugTable);
    registerDebugLibrary(this, debugTable);
    setGlobal("debug", Value::table(debugTable));
    registerModule("debug", debugTable);
}

void VM::runInitializationFrames() {
    if (currentCoroutine_->frames.size() > 0) {
        if (run(currentCoroutine_->frames.size() - 1)) {
            for (size_t i = 0; i < currentCoroutine_->lastResultCount; i++) {
                pop();
            }
        }
    }
}

Value VM::getGlobal(const std::string& name) const {
    auto it = globals_.find("_G");
    if (it != globals_.end() && it->second.isTable()) {
        return it->second.asTableObj()->get(name);
    }
    auto it2 = globals_.find(name);
    if (it2 != globals_.end()) {
        return it2->second;
    }
    return Value::nil();
}

void VM::registerModule(const std::string& name, TableObject* module) {
    Value package = getGlobal("package");
    if (package.isTable()) {
        Value loaded = package.asTableObj()->get("loaded");
        if (loaded.isTable()) {
            loaded.asTableObj()->set(name, Value::table(module));
        }
    }
}

void VM::setGlobal(const std::string& name, const Value& value) {
    globals_[name] = value;
    
    auto it = globals_.find("_G");
    if (it != globals_.end() && it->second.isTable()) {
        it->second.asTableObj()->set(name, value);
    }
}

CallFrame& VM::currentFrame() {
    if (currentCoroutine_->frames.empty()) {
        throw RuntimeError("No active call frames");
    }
    return currentCoroutine_->frames.back();
}

const CallFrame& VM::currentFrame() const {
    if (currentCoroutine_->frames.empty()) {
        throw RuntimeError("No active call frames");
    }
    return currentCoroutine_->frames.back();
}

void VM::internConstants(const FunctionObject& function) {
    bool oldGC = gcEnabled_;
    gcEnabled_ = false;
    for (size_t i = 0; i < function.chunk()->constants().size(); i++) {
        Value& val = const_cast<std::vector<Value>&>(function.chunk()->constants())[i];
        if (val.isString() && !val.isRuntimeString()) {
            StringObject* str = function.chunk()->getString(val.asStringIndex());
            val = Value::runtimeString(internString(str->chars(), str->length(), true));
            rootedConstants_.push_back(val);
        } else if (val.isInt64() && !val.isRuntimeInt64()) {
            int64_t num = function.chunk()->getInt64(val.asInt64Index());
            val = makeInteger(num);
            rootedConstants_.push_back(val);
        } else if (val.isFunction()) {
            FunctionObject* nested = function.chunk()->getFunction(val.asFunctionIndex());
            if (nested) internConstants(*nested);
        }
    }
    gcEnabled_ = oldGC;
}

bool VM::run(const FunctionObject& function) {
    return run(function, {});
}

bool VM::run(const FunctionObject& function, const std::vector<Value>& args) {
    try {
        const Chunk* oldChunk = currentCoroutine_->chunk;
        const Chunk* oldRoot = currentCoroutine_->rootChunk;
        size_t oldFrameCount = currentCoroutine_->frames.size();

        currentCoroutine_->chunk = function.chunk();
        if (currentCoroutine_->rootChunk == nullptr) {
            currentCoroutine_->rootChunk = function.chunk();
        }
        if (mainCoroutine_->rootChunk == nullptr) {
            mainCoroutine_->rootChunk = function.chunk();
        }
        hadError_ = false;

        if (!stdlibInitialized_) {
            initStandardLibrary();
        }

        internConstants(function);

        ClosureObject* closure = createClosure(const_cast<FunctionObject*>(&function));
        
        if (currentCoroutine_->frames.empty() && currentCoroutine_->stack.empty()) {
            currentCoroutine_->stack.clear();
        }
        
        setupRootUpvalues(closure);

        push(Value::closure(closure));
        
        if (currentCoroutine_->status == CoroutineObject::Status::DEAD) {
            currentCoroutine_->status = CoroutineObject::Status::RUNNING;
        }

        for (const auto& arg : args) {
            push(arg);
        }

        if (!callValue(static_cast<int>(args.size()), 0)) {
            return false;
        }

        if (currentCoroutine_->hookMask & CoroutineObject::MASK_CALL) {
            callHook("call");
        }

        bool result = run(oldFrameCount);

        if (currentCoroutine_->status == CoroutineObject::Status::SUSPENDED) {
            return result;
        }

        currentCoroutine_->chunk = oldChunk;
        currentCoroutine_->rootChunk = oldRoot;
        
        return result;
    } catch (const RuntimeError& e) {
        isHandlingError_ = false;
        hadError_ = false;
        return false;
    }
}

bool VM::run() {
    return run(0);
}

bool VM::pcall(int argCount) {
    size_t prevFrames = currentCoroutine_->frames.size();
    size_t stackSizeBefore = currentCoroutine_->stack.size() - argCount;

    bool prevPcall = inPcall_;
    bool prevHandling = isHandlingError_;
    inPcall_ = true;
    
    bool success = false;
    try {
        success = callValue(argCount - 1, 0);

        if (success && currentCoroutine_->frames.size() > prevFrames) {
            success = run(prevFrames);
        }
        
        if (currentCoroutine_->status == CoroutineObject::Status::SUSPENDED) {
            inPcall_ = prevPcall;
            isHandlingError_ = prevHandling;
            return true;
        }

        if (hadError_) success = false;
    } catch (const CoroutineCloseSelfException&) {
        inPcall_ = prevPcall;
        isHandlingError_ = prevHandling;
        throw;
    } catch (const RuntimeError&) {
        success = false;
    } catch (const std::exception& e) {
        lastErrorMessage_ = e.what();
        success = false;
    } catch (...) {
        lastErrorMessage_ = "unknown error";
        success = false;
    }
    
    inPcall_ = prevPcall;
    hadError_ = false;

    if (!success) {
        isHandlingError_ = true;
        Value errorObj = lastErrorObject_.isNil() ? Value::runtimeString(internString(lastErrorMessage_)) : lastErrorObject_;

        while (currentCoroutine_->frames.size() > prevFrames) {
            if (currentCoroutine_->frames.back().isReturning && !currentCoroutine_->pendingReturns.empty()) {
                currentCoroutine_->pendingReturns.pop_back();
            }
            currentCoroutine_->frames.pop_back();
        }

        if (prevFrames > 0) {
            currentCoroutine_->frames[prevFrames - 1].isErrorUnwinding = true;
            currentCoroutine_->closeError = errorObj;
        }

        try {
            closeUpvalues(stackSizeBefore, nullptr, errorObj);
        } catch (const RuntimeError& e) {
            errorObj = lastErrorObject_.isNil() ? Value::runtimeString(internString(e.what())) : lastErrorObject_;
            hadError_ = false;
            currentCoroutine_->closeError = errorObj;
        } catch (const std::exception& e) {
            errorObj = Value::runtimeString(internString(e.what()));
            hadError_ = false;
            currentCoroutine_->closeError = errorObj;
        }
        
        if (currentCoroutine_->status == CoroutineObject::Status::SUSPENDED) {
            inPcall_ = prevPcall;
            isHandlingError_ = prevHandling;
            return true;
        }

        if (prevFrames > 0) {
            currentCoroutine_->frames[prevFrames - 1].isErrorUnwinding = false;
            currentCoroutine_->closeError = Value::nil();
        }

        while (currentCoroutine_->stack.size() > stackSizeBefore) {
            pop();
        }
        
        push(Value::boolean(false));
        push(lastErrorObject_.isNil() ? errorObj : lastErrorObject_);
        hadError_ = false;
        isHandlingError_ = prevHandling;
        lastErrorObject_ = Value::nil();
        currentCoroutine_->lastResultCount = 2;
    } else {
        size_t resultCount = currentCoroutine_->lastResultCount;
        
        std::vector<Value> results;
        for (size_t i = 0; i < resultCount; i++) {
            results.push_back(pop());
        }
        std::reverse(results.begin(), results.end());
        
        while (currentCoroutine_->stack.size() > stackSizeBefore) {
            pop();
        }
        
        push(Value::boolean(true));
        for (const auto& res : results) {
            push(res);
        }
        currentCoroutine_->lastResultCount = resultCount + 1;
        isHandlingError_ = prevHandling;
    }
    
    return true;
}

bool VM::xpcall(int argCount) {
    if (argCount < 2) {
        runtimeError("xpcall expects at least 2 arguments");
        return false;
    }

    size_t prevFrames = currentCoroutine_->frames.size();
    size_t stackSizeBefore = currentCoroutine_->stack.size() - argCount;

    bool prevPcall = inPcall_;
    bool prevHandling = isHandlingError_;
    inPcall_ = true;

    std::vector<Value> args;
    for (int i = 0; i < argCount - 2; i++) {
        args.push_back(pop());
    }
    pop(); // pop msgh
    for (auto it = args.rbegin(); it != args.rend(); ++it) {
        push(*it);
    }

    bool success = false;
    try {
        success = callValue(argCount - 2, 0);
        if (success && currentCoroutine_->frames.size() > prevFrames) {
            success = run(prevFrames);
        }
        if (currentCoroutine_->status == CoroutineObject::Status::SUSPENDED) {
            inPcall_ = prevPcall;
            isHandlingError_ = prevHandling;
            return true;
        }
    } catch (const CoroutineCloseSelfException&) {
        inPcall_ = prevPcall;
        isHandlingError_ = prevHandling;
        throw;
    } catch (const RuntimeError& e) {
        hadError_ = false;
        success = false;
    }

    inPcall_ = prevPcall;

    if (!success || hadError_) {
        isHandlingError_ = true;
        Value errObj = lastErrorObject_.isNil() ? Value::runtimeString(internString(lastErrorMessage_)) : lastErrorObject_;

        while (currentCoroutine_->frames.size() > prevFrames) {
            if (currentCoroutine_->frames.back().isReturning && !currentCoroutine_->pendingReturns.empty()) {
                currentCoroutine_->pendingReturns.pop_back();
            }
            currentCoroutine_->frames.pop_back();
        }

        if (prevFrames > 0) {
            currentCoroutine_->frames[prevFrames - 1].isErrorUnwinding = true;
            currentCoroutine_->closeError = errObj;
        }

        try {
            closeUpvalues(stackSizeBefore, nullptr, errObj);
        } catch (const RuntimeError& e) {
            errObj = lastErrorObject_.isNil() ? Value::runtimeString(internString(e.what())) : lastErrorObject_;
            hadError_ = false;
            currentCoroutine_->closeError = errObj;
        } catch (const std::exception& e) {
            errObj = Value::runtimeString(internString(e.what()));
            hadError_ = false;
            currentCoroutine_->closeError = errObj;
        }

        if (currentCoroutine_->status == CoroutineObject::Status::SUSPENDED) {
            inPcall_ = prevPcall;
            isHandlingError_ = prevHandling;
            return true;
        }

        if (prevFrames > 0) {
            currentCoroutine_->frames[prevFrames - 1].isErrorUnwinding = false;
            currentCoroutine_->closeError = Value::nil();
        }

        while (currentCoroutine_->stack.size() > stackSizeBefore) {
            pop();
        }

        push(Value::boolean(false));
        push(lastErrorObject_.isNil() ? errObj : lastErrorObject_);
        hadError_ = false;
        isHandlingError_ = prevHandling;
        lastErrorObject_ = Value::nil();
        currentCoroutine_->lastResultCount = 2;
    } else {
        size_t resultCount = currentCoroutine_->lastResultCount;
        
        std::vector<Value> results;
        for (size_t i = 0; i < resultCount; i++) {
            results.push_back(pop());
        }
        std::reverse(results.begin(), results.end());
        
        while (currentCoroutine_->stack.size() > stackSizeBefore) {
            pop();
        }
        
        push(Value::boolean(true));
        for (const auto& res : results) {
            push(res);
        }
        currentCoroutine_->lastResultCount = resultCount + 1;
        isHandlingError_ = prevHandling;
    }
    
    return true;
}

bool VM::runSource(const std::string& source, const std::string& name) {
    return runSource(source, name, {});
}

bool VM::runSource(const std::string& source, const std::string& name, const std::vector<Value>& args) {
    FunctionObject* func = compileSource(source, name);
    if (!func) return false;
    return run(*func, args);
}

FunctionObject* VM::compileSource(const std::string& source, const std::string& name) {
    try {
        Lexer lexer(source);
        Parser parser(lexer);
        auto program = parser.parse();
        if (!program) return nullptr;

        CodeGenerator codegen;
        auto function = codegen.generate(program.get(), name);
        if (!function) return nullptr;

#ifdef PRINT_CODE
        function->chunk()->disassemble(name);
#endif

        return function.release();
    } catch (const CompileError& e) {
        std::cerr << e.what() << std::endl;
        return nullptr;
    }
}

void VM::push(const Value& value) {
    if (currentCoroutine_->stack.size() >= STACK_MAX) {
        hadError_ = true;
        lastErrorMessage_ = "error in error handling";
        lastErrorObject_ = Value::runtimeString(internString(lastErrorMessage_));
        if (isJitExecuting_) return;
        throw RuntimeError(lastErrorMessage_);
    } else if (currentCoroutine_->stack.size() >= (!isHandlingError_ ? STACK_LIMIT : STACK_MAX)) {
        runtimeError("stack overflow");
        return;
    }
    currentCoroutine_->stack.push_back(value);
}

Value VM::pop() {
    if (currentCoroutine_->stack.empty()) {
        runtimeError("Stack underflow");
        return Value::nil();
    }
    Value value = currentCoroutine_->stack.back();
    currentCoroutine_->stack.pop_back();
    return value;
}

Value VM::peek(size_t distance) const {
    if (distance >= currentCoroutine_->stack.size()) {
        return Value::nil();
    }
    Value val = currentCoroutine_->stack[currentCoroutine_->stack.size() - 1 - distance];
    return val;
}

uint8_t VM::readByte() {
    return currentFrame().chunk->at(currentFrame().ip++);
}

Value VM::getConstant(size_t index) {
    Value constant = currentFrame().chunk->constants()[index];
    
    if (constant.isString() && !constant.isRuntimeString()) {
        StringObject* str = currentFrame().chunk->getString(constant.asStringIndex());
        StringObject* runtimeStr = internString(str->chars(), str->length(), true);
        constant = Value::runtimeString(runtimeStr);
        const_cast<std::vector<Value>&>(currentFrame().chunk->constants())[index] = constant;
        rootedConstants_.push_back(constant);
        return constant;
    }
    if (constant.isInt64() && !constant.isRuntimeInt64()) {
        int64_t num = currentFrame().chunk->getInt64(constant.asInt64Index());
        Value intVal = makeInteger(num);
        constant = intVal;
        const_cast<std::vector<Value>&>(currentFrame().chunk->constants())[index] = constant;
        rootedConstants_.push_back(constant);
        return constant;
    }
    
    return constant;
}

Value VM::readConstant() {
    return getConstant(readByte());
}

void VM::runtimeError(const std::string& message, int level) {
    if (isHandlingStackError_ && (message == "C stack overflow" || message == "stack overflow")) {
        hadError_ = true;
        lastErrorMessage_ = "error in error handling";
        lastErrorObject_ = Value::runtimeString(internString(lastErrorMessage_));
        if (isJitExecuting_) return;
        throw RuntimeError(lastErrorMessage_);
    }
    if ((currentCoroutine_ && currentCoroutine_->nCcalls >= 220) ||
        (currentCoroutine_ && currentCoroutine_->frames.size() >= FRAMES_MAX) ||
        errorHandlerDepth_ >= 220) {
        hadError_ = true;
        lastErrorMessage_ = "error in error handling";
        lastErrorObject_ = Value::runtimeString(internString(lastErrorMessage_));
        if (isJitExecuting_) return;
        throw RuntimeError(lastErrorMessage_);
    }
    isHandlingError_ = true;
    lastErrorMessage_ = message;
    hadError_ = true;

    int line = -1;
    std::string source = sourceName_;

    int targetFrame = -1;
    if (level > 0 && !currentCoroutine_->frames.empty()) {
        if (currentCoroutine_->frames.back().isC) {
            targetFrame = static_cast<int>(currentCoroutine_->frames.size()) - 1 - level;
        } else {
            targetFrame = static_cast<int>(currentCoroutine_->frames.size()) - level;
        }
    }

    std::string prefix = "";
    if (targetFrame >= 0 && targetFrame < static_cast<int>(currentCoroutine_->frames.size())) {
        const CallFrame& frame = currentCoroutine_->frames[targetFrame];
        const Chunk* chunk = frame.chunk;
        if (chunk) {
            source = chunk->sourceName();
            if (frame.ip > 0) {
                line = chunk->getLine(frame.ip - 1);
            }
            if (level > 0) {
                std::string displaySource = formatChunkId(source);
                if (displaySource.empty()) displaySource = "?";
                std::string displayLine = (line != -1) ? std::to_string(line) : "?";
                prefix = displaySource + ":" + displayLine + ": ";
            }
        }
    }

    lastErrorMessage_ = prefix + message;
    lastErrorObject_ = Value::runtimeString(internString(lastErrorMessage_));

    Value handler = Value::nil();
    for (int i = (int)currentCoroutine_->frames.size() - 1; i >= 0; i--) {
        if (currentCoroutine_->frames[i].isPcall) {
            handler = currentCoroutine_->frames[i].errorHandler;
            break;
        }
    }

    if (!handler.isNil()) {
        bool prevHadError = hadError_;
        hadError_ = false;
        bool prevRunning = isRunningErrorHandler_;
        isRunningErrorHandler_ = true;
        errorHandlerDepth_++;
        bool prevStackError = isHandlingStackError_;
        if (message == "C stack overflow" || message == "stack overflow" || lastErrorMessage_.find("stack overflow") != std::string::npos) {
            isHandlingStackError_ = true;
        }

        push(handler);
        push(lastErrorObject_);
        size_t prevFrames = currentCoroutine_->frames.size();
        bool handlerSuccess = false;
        try {
            if (callValue(1, 2)) {
                if (currentCoroutine_->frames.size() > prevFrames) {
                    handlerSuccess = run(prevFrames);
                } else {
                    handlerSuccess = true;
                }
                if (handlerSuccess) {
                    lastErrorObject_ = pop();
                    lastErrorMessage_ = lastErrorObject_.isString() ? getStringValue(lastErrorObject_) : lastErrorObject_.toString();
                }
            }
        } catch (const CoroutineCloseSelfException&) {
            isHandlingStackError_ = prevStackError;
            errorHandlerDepth_--;
            isRunningErrorHandler_ = prevRunning;
            throw;
        } catch (const RuntimeError&) {
            handlerSuccess = false;
        } catch (...) {
            handlerSuccess = false;
        }
        isHandlingStackError_ = prevStackError;
        errorHandlerDepth_--;
        isRunningErrorHandler_ = prevRunning;
        if (!handlerSuccess) {
            while (currentCoroutine_->frames.size() > prevFrames) {
                currentCoroutine_->frames.pop_back();
            }
            lastErrorMessage_ = "error in error handling";
            lastErrorObject_ = Value::runtimeString(internString(lastErrorMessage_));
        }
        hadError_ = prevHadError;
    }

    if (isJitExecuting_) {
        return;
    }

    throw RuntimeError(lastErrorMessage_);
}

void VM::runtimeError(const Value& errorObj, int level) {
    if (errorObj.isString()) {
        runtimeError(getStringValue(errorObj), level);
    } else {
        if ((currentCoroutine_ && currentCoroutine_->nCcalls >= 220) ||
            (currentCoroutine_ && currentCoroutine_->frames.size() >= FRAMES_MAX) ||
            errorHandlerDepth_ >= 220) {
            hadError_ = true;
            lastErrorMessage_ = "error in error handling";
            lastErrorObject_ = Value::runtimeString(internString(lastErrorMessage_));
            if (isJitExecuting_) return;
            throw RuntimeError(lastErrorMessage_);
        }
        isHandlingError_ = true;
        hadError_ = true;
        lastErrorMessage_ = errorObj.toString();
        lastErrorObject_ = errorObj;
        Value handler = Value::nil();
        int handlerFrameIndex = -1;
        for (int i = (int)currentCoroutine_->frames.size() - 1; i >= 0; i--) {
            if (currentCoroutine_->frames[i].isPcall) {
                handler = currentCoroutine_->frames[i].errorHandler;
                handlerFrameIndex = i;
                break;
            }
        }

        if (handlerFrameIndex != -1) {
            if (!handler.isNil()) {
                bool prevHadError = hadError_;
                hadError_ = false;
                bool prevRunning = isRunningErrorHandler_;
                isRunningErrorHandler_ = true;
                errorHandlerDepth_++;
                bool prevStackError = isHandlingStackError_;

                push(handler);
                push(lastErrorObject_);
                size_t prevFrames = currentCoroutine_->frames.size();
                bool handlerSuccess = false;
                try {
                    if (callValue(1, 2)) {
                        if (currentCoroutine_->frames.size() > prevFrames) {
                            handlerSuccess = run(prevFrames);
                        } else {
                            handlerSuccess = true;
                        }
                        if (handlerSuccess) {
                            lastErrorObject_ = pop();
                            lastErrorMessage_ = lastErrorObject_.isString() ? getStringValue(lastErrorObject_) : lastErrorObject_.toString();
                        }
                    }
                } catch (const CoroutineCloseSelfException&) {
                    isHandlingStackError_ = prevStackError;
                    errorHandlerDepth_--;
                    isRunningErrorHandler_ = prevRunning;
                    throw;
                } catch (const RuntimeError&) {
                    isHandlingStackError_ = prevStackError;
                    errorHandlerDepth_--;
                    isRunningErrorHandler_ = prevRunning;
                    throw;
                } catch (...) {
                    handlerSuccess = false;
                }
                isHandlingStackError_ = prevStackError;
                errorHandlerDepth_--;
                isRunningErrorHandler_ = prevRunning;
                if (!handlerSuccess) {
                    while (currentCoroutine_->frames.size() > prevFrames) {
                        currentCoroutine_->frames.pop_back();
                    }
                    lastErrorMessage_ = "error in error handling";
                    lastErrorObject_ = Value::runtimeString(internString(lastErrorMessage_));
                }
                hadError_ = prevHadError;
            }
        } else if (currentCoroutine_ == mainCoroutine_) {
            // Uncaught / top-level error object in main coroutine (standalone CLI script):
            // Convert via __tostring or standard Lua error object message
            if (errorObj.isNumber()) {
                lastErrorMessage_ = errorObj.toString();
                lastErrorObject_ = errorObj;
            } else {
                Value tostringMeta = getMetamethod(errorObj, "__tostring");
                if (!tostringMeta.isNil()) {
                    bool prevHadError = hadError_;
                    hadError_ = false;
                    bool prevRunning = isRunningErrorHandler_;
                    isRunningErrorHandler_ = true;

                    // Push a dummy frame so call stack depth matches standard Lua msghandler:
                    // Level 0: debug.getinfo
                    // Level 1: __tostring
                    // Level 2: msghandler (dummy C frame)
                    // Level 3: error (C function)
                    // Level 4: main chunk
                    CallFrame msgHandlerFrame;
                    msgHandlerFrame.isC = true;
                    msgHandlerFrame.stackBase = currentCoroutine_->stack.size();
                    currentCoroutine_->frames.push_back(msgHandlerFrame);

                    push(tostringMeta);
                    push(errorObj);
                    size_t prevFrames = currentCoroutine_->frames.size();
                    bool handlerSuccess = false;
                    try {
                        if (callValue(1, 2)) {
                            if (currentCoroutine_->frames.size() > prevFrames) {
                                handlerSuccess = run(prevFrames);
                            } else {
                                handlerSuccess = true;
                            }
                            if (handlerSuccess) {
                                lastErrorObject_ = pop();
                                lastErrorMessage_ = lastErrorObject_.isString() ? getStringValue(lastErrorObject_) : lastErrorObject_.toString();
                            }
                        }
                    } catch (...) {
                        handlerSuccess = false;
                    }
                    while (currentCoroutine_->frames.size() > prevFrames - 1) {
                        currentCoroutine_->frames.pop_back();
                    }
                    isRunningErrorHandler_ = prevRunning;
                    if (!handlerSuccess) {
                        lastErrorMessage_ = "error in error handling";
                        lastErrorObject_ = Value::runtimeString(internString(lastErrorMessage_));
                    }
                    hadError_ = prevHadError;
                } else {
                    lastErrorMessage_ = "(error object is a " + errorObj.typeToString() + " value)";
                    lastErrorObject_ = Value::runtimeString(internString(lastErrorMessage_));
                }
            }
        } else {
            // Uncaught in sub-coroutine: preserve raw errorObj for coroutine.resume/wrap
            if (errorObj.isNumber()) {
                lastErrorMessage_ = errorObj.toString();
            }
        }
        if (isJitExecuting_) return;
        throw RuntimeError(lastErrorMessage_);
    }
}

std::string VM::findGlobalFuncName(const Value& funcVal) {
    if (funcVal.isNil()) return "";

    auto matches = [this](const Value& a, const Value& b) -> bool {
        if (a == b) return true;
        if (a.isClosure() && b.isClosure()) {
            ClosureObject* ca = a.asClosureObj();
            ClosureObject* cb = b.asClosureObj();
            if (ca->isC() && cb->isC()) {
                if (ca->cFunc() && ca->cFunc() == cb->cFunc()) return true;
                if (ca->nativeFunc() && ca->nativeFunc() == cb->nativeFunc()) return true;
            }
        }
        if (a.isClosure() && a.asClosureObj()->isC()) {
            ClosureObject* ca = a.asClosureObj();
            if (b.isNativeFunction() && b.asNativeFunctionIndex() < nativeFunctions_.size() && 
                ca->nativeFunc() == nativeFunctions_[b.asNativeFunctionIndex()].func) return true;
            if (b.isCFunction() && ca->cFunc() == reinterpret_cast<lua_CFunction>(b.asCFunction())) return true;
        }
        if (b.isClosure() && b.asClosureObj()->isC()) {
            ClosureObject* cb = b.asClosureObj();
            if (a.isNativeFunction() && a.asNativeFunctionIndex() < nativeFunctions_.size() && 
                cb->nativeFunc() == nativeFunctions_[a.asNativeFunctionIndex()].func) return true;
            if (a.isCFunction() && cb->cFunc() == reinterpret_cast<lua_CFunction>(a.asCFunction())) return true;
        }
        return false;
    };

    // 1. Search directly in globals
    for (const auto& [name, val] : globals_) {
        if (name == "_G" || name == "_ENV") continue;
        if (matches(val, funcVal)) {
            return name;
        }
    }

    auto itG = globals_.find("_G");
    if (itG != globals_.end() && itG->second.isTable()) {
        TableObject* gTable = itG->second.asTableObj();
        for (const auto& [k, v] : gTable->data()) {
            if (k.isString()) {
                std::string kname = getStringValue(k);
                if (kname == "_G" || kname == "_ENV") continue;
                if (matches(v, funcVal)) {
                    return kname;
                }
            }
        }
    }

    // 2. Search in 1st level tables in globals (e.g., table.sort, string.sub, io.read, math.sin)
    for (const auto& [tableName, tableVal] : globals_) {
        if (tableName == "_G" || tableName == "_ENV") continue;
        if (tableVal.isTable()) {
            TableObject* t = tableVal.asTableObj();
            for (const auto& [fieldName, fieldVal] : t->data()) {
                if (fieldName.isString() && matches(fieldVal, funcVal)) {
                    return tableName + "." + getStringValue(fieldName);
                }
            }
        }
    }

    if (itG != globals_.end() && itG->second.isTable()) {
        TableObject* gTable = itG->second.asTableObj();
        for (const auto& [subName, subVal] : gTable->data()) {
            if (subName.isString() && subVal.isTable()) {
                std::string sname = getStringValue(subName);
                if (sname == "_G" || sname == "_ENV") continue;
                TableObject* t = subVal.asTableObj();
                for (const auto& [fieldName, fieldVal] : t->data()) {
                    if (fieldName.isString() && matches(fieldVal, funcVal)) {
                        return sname + "." + getStringValue(fieldName);
                    }
                }
            }
        }
    }

    // 3. Search in loaded modules table if present
    Value loadedVal = getRegistry("_LOADED");
    if (loadedVal.isTable()) {
        TableObject* loaded = loadedVal.asTableObj();
        for (const auto& [modKey, modVal] : loaded->data()) {
            if (modKey.isString() && modVal.isTable()) {
                std::string modName = getStringValue(modKey);
                for (const auto& [fnKey, fnVal] : modVal.asTableObj()->data()) {
                    if (fnKey.isString() && matches(fnVal, funcVal)) {
                        return modName + "." + getStringValue(fnKey);
                    }
                }
            }
        }
    }
    return "";
}

bool VM::argError(int argNum, const std::string& extramsg, const char* funcNameFallback) {
    CallingFuncInfo info = getCallingFuncInfo();
    std::string funcName = "";
    if (info.isMethod) {
        funcName = info.name;
        argNum--;
        if (argNum == 0) {
            runtimeError("calling '" + funcName + "' on bad self (" + extramsg + ")");
            return false;
        }
    } else {
        if (!info.name.empty()) {
            funcName = info.name;
        } else {
            if (!currentCoroutine_->frames.empty() && !currentCoroutine_->frames.back().cFunc.isNil()) {
                funcName = findGlobalFuncName(currentCoroutine_->frames.back().cFunc);
            }
            if (funcName.empty() && funcNameFallback) {
                funcName = funcNameFallback;
            }
            if (funcName.empty()) {
                funcName = "?";
            }
        }
    }
    runtimeError("bad argument #" + std::to_string(argNum) + " to '" + funcName + "' (" + extramsg + ")");
    return false;
}

bool VM::typeError(int argNum, const std::string& expectedType, const Value& actualVal, int totalArgCount, const char* funcNameFallback) {
    std::string actualType;
    if (argNum > totalArgCount) {
        actualType = "no value";
    } else if (actualVal.isNil()) {
        actualType = "nil";
    } else {
        actualType = typeName(actualVal);
    }
    std::string msg = expectedType + " expected, got " + actualType;
    return argError(argNum, msg, funcNameFallback);
}

void VM::traceExecution() {
    std::cout << "          ";
    for (const Value& value : currentCoroutine_->stack) {
        std::cout << "[ " << value << " ]";
    }
    std::cout << std::endl;

    const Chunk* chunk = currentCoroutine_->frames.empty() ? currentCoroutine_->chunk : currentFrame().chunk;
    if (chunk) {
        chunk->disassembleInstruction(currentFrame().ip);
    }
}

bool VM::stringToNumber(const std::string& str, double& outNum, bool& isInt) {
    int64_t dummyInt = 0;
    return stringToNumber(str, outNum, dummyInt, isInt);
}

bool VM::stringToNumber(const std::string& str, double& outNum, int64_t& outInt, bool& isInt) {
    auto start = str.find_first_not_of(" \t\n\r\f\v");
    if (start == std::string::npos) return false;
    auto end = str.find_last_not_of(" \t\n\r\f\v");
    std::string s = str.substr(start, end - start + 1);

    // Reject strings with embedded NUL bytes
    if (s.length() != std::strlen(s.c_str())) {
        return false;
    }

    std::string lower_s = s;
    for (char& c : lower_s) c = std::tolower(static_cast<unsigned char>(c));
    if (lower_s.find("inf") != std::string::npos || lower_s.find("nan") != std::string::npos) {
        return false;
    }

    const char* p = s.c_str();
    bool neg = false;
    if (*p == '+') p++;
    else if (*p == '-') { neg = true; p++; }

    // Hex integer check
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        if (s.find('.') == std::string::npos && s.find(',') == std::string::npos &&
            lower_s.find('p') == std::string::npos) {
            const char* cur = p + 2;
            if (*cur != '\0') {
                uint64_t a = 0;
                bool valid = true;
                while (*cur) {
                    char c = *cur++;
                    int digit = 0;
                    if (c >= '0' && c <= '9') digit = c - '0';
                    else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
                    else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
                    else { valid = false; break; }
                    a = (a << 4) | static_cast<uint64_t>(digit);
                }
                if (valid) {
                    if (neg) a = 0ULL - a;
                    int64_t ival = static_cast<int64_t>(a);
                    outInt = ival;
                    outNum = static_cast<double>(ival);
                    isInt = true;
                    return true;
                }
            }
            return false;
        }
    }

    // Decimal integer check
    if (s.find('.') == std::string::npos && s.find(',') == std::string::npos &&
        lower_s.find('e') == std::string::npos) {
        char* endp = nullptr;
        errno = 0;
        unsigned long long uval = std::strtoull(p, &endp, 10);
        if (endp && *endp == '\0' && endp != p) {
            if (errno == 0) {
                if (neg) {
                    if (uval <= 9223372036854775808ULL) {
                        int64_t ival = (uval == 9223372036854775808ULL) ? std::numeric_limits<int64_t>::min() : -static_cast<int64_t>(uval);
                        outInt = ival;
                        outNum = static_cast<double>(ival);
                        isInt = true;
                        return true;
                    }
                } else {
                    if (uval <= 9223372036854775807ULL) {
                        int64_t ival = static_cast<int64_t>(uval);
                        outInt = ival;
                        outNum = static_cast<double>(ival);
                        isInt = true;
                        return true;
                    }
                }
            }
        }
    }

    auto try_strtod = [](const char* nptr, double& res) -> bool {
        char* endp = nullptr;
        errno = 0;
        double d = std::strtod(nptr, &endp);
        if (endp != nptr) {
            while (*endp && std::isspace(static_cast<unsigned char>(*endp))) endp++;
            if (*endp == '\0') {
                res = d;
                return true;
            }
        }
        return false;
    };

    double dres = 0.0;
    bool ok = try_strtod(s.c_str(), dres);
    if (!ok) {
        char dec = '.';
        struct lconv* lc = localeconv();
        if (lc && lc->decimal_point && lc->decimal_point[0] != '\0') {
            dec = lc->decimal_point[0];
        }

        std::string swapped = s;
        for (char& c : swapped) {
            if (c == '.') c = dec;
            else if (c == dec) c = '.';
        }
        ok = try_strtod(swapped.c_str(), dres);
        if (!ok) {
            swapped = s;
            for (char& c : swapped) {
                if (c == '.') c = ',';
                else if (c == ',') c = '.';
            }
            ok = try_strtod(swapped.c_str(), dres);
        }
    }

    if (ok) {
        outNum = dres;
        isInt = false;
        return true;
    }
    return false;
}

bool VM::stringToInteger(const std::string& str, int64_t& outInt) {
    double dnum = 0.0;
    int64_t inum = 0;
    bool isInt = false;
    if (stringToNumber(str, dnum, inum, isInt)) {
        if (isInt) {
            outInt = inum;
            return true;
        }
        if (!std::isnan(dnum) && !std::isinf(dnum)) {
            double intpart;
            if (std::modf(dnum, &intpart) == 0.0 &&
                dnum >= -9223372036854775808.0 && dnum < 9223372036854775808.0) {
                outInt = static_cast<int64_t>(dnum);
                return true;
            }
        }
    }
    return false;
}

Value VM::makeInteger(int64_t val) {
    if (val >= -(1LL << 47) && val < (1LL << 47)) {
        return Value::integer(val);
    }
    return Value::fromInt64(allocateObject<Int64Object>(val));
}

bool VM::toIntegerNoString(const Value& val, int64_t& outInt) {
    if (val.isInteger()) {
        outInt = val.asInteger();
        return true;
    }
    if (val.isFloat()) {
        double d = val.asNumber();
        if (std::isnan(d) || std::isinf(d)) return false;
        double intpart;
        if (std::modf(d, &intpart) == 0.0 &&
            d >= -9223372036854775808.0 && d < 9223372036854775808.0) {
            outInt = static_cast<int64_t>(d);
            return true;
        }
    }
    return false;
}

bool VM::toInteger(const Value& val, int64_t& outInt) {
    if (toIntegerNoString(val, outInt)) return true;
    if (val.isString()) {
        return stringToInteger(getStringValue(val), outInt);
    }
    return false;
}

bool VM::coerceToNumber(Value& val) {
    if (val.isNumber()) return true;
    if (val.isString()) {
        double num = 0.0;
        int64_t inum = 0;
        bool isInt = false;
        if (stringToNumber(getStringValue(val), num, inum, isInt)) {
            if (isInt) {
                val = makeInteger(inum);
            } else {
                val = Value::number(num);
            }
            return true;
        }
    }
    return false;
}

static inline int64_t lua_shift_left(int64_t x, int64_t y) {
    if (y < 0) {
        if (y <= -64) return 0;
        return static_cast<int64_t>(static_cast<uint64_t>(x) >> (-y));
    } else {
        if (y >= 64) return 0;
        return static_cast<int64_t>(static_cast<uint64_t>(x) << y);
    }
}

static inline int64_t lua_shift_right(int64_t x, int64_t y) {
    if (y < 0) {
        if (y <= -64) return 0;
        return static_cast<int64_t>(static_cast<uint64_t>(x) << (-y));
    } else {
        if (y >= 64) return 0;
        return static_cast<int64_t>(static_cast<uint64_t>(x) >> y);
    }
}

static inline bool LTintfloat(int64_t i, double f) {
    if (std::isnan(f)) return false;
    if (f >= 9223372036854775808.0) return true;  // i < 2^63 <= f
    if (f <= -9223372036854775808.0) return false; // f <= -2^63 <= i
    double intpart;
    if (std::modf(f, &intpart) == 0.0) {
        return i < static_cast<int64_t>(f);
    }
    return static_cast<double>(i) < f;
}

static inline bool LEintfloat(int64_t i, double f) {
    if (std::isnan(f)) return false;
    if (f >= 9223372036854775808.0) return true;  // i <= 2^63 <= f
    if (f < -9223372036854775808.0) return false; // f < -2^63 <= i
    double intpart;
    if (std::modf(f, &intpart) == 0.0) {
        return i <= static_cast<int64_t>(f);
    }
    return static_cast<double>(i) <= f;
}

static inline bool LTfloatint(double f, int64_t i) {
    if (std::isnan(f)) return false;
    if (f >= 9223372036854775808.0) return false; // f >= 2^63 > i
    if (f < -9223372036854775808.0) return true;  // f < -2^63 <= i
    double intpart;
    if (std::modf(f, &intpart) == 0.0) {
        return static_cast<int64_t>(f) < i;
    }
    return f < static_cast<double>(i);
}

static inline bool LEfloatint(double f, int64_t i) {
    if (std::isnan(f)) return false;
    if (f >= 9223372036854775808.0) return false;  // f >= 2^63 > maxint >= i
    if (f <= -9223372036854775808.0) return true; // f <= minint <= i
    double intpart;
    if (std::modf(f, &intpart) == 0.0) {
        return static_cast<int64_t>(f) <= i;
    }
    return f <= static_cast<double>(i);
}

Value VM::add(const Value& a, const Value& b) {
    if (!a.isNumber() || !b.isNumber()) {
        runtimeError("Operands must be numbers");
        return Value::nil();
    }
    if (a.isInteger() && b.isInteger()) {
        return makeInteger(static_cast<int64_t>(static_cast<uint64_t>(a.asInteger()) + static_cast<uint64_t>(b.asInteger())));
    }
    return Value::number(a.asNumber() + b.asNumber());
}

Value VM::subtract(const Value& a, const Value& b) {
    if (!a.isNumber() || !b.isNumber()) {
        runtimeError("Operands must be numbers");
        return Value::nil();
    }
    if (a.isInteger() && b.isInteger()) {
        return makeInteger(static_cast<int64_t>(static_cast<uint64_t>(a.asInteger()) - static_cast<uint64_t>(b.asInteger())));
    }
    return Value::number(a.asNumber() - b.asNumber());
}

Value VM::multiply(const Value& a, const Value& b) {
    if (!a.isNumber() || !b.isNumber()) {
        runtimeError("Operands must be numbers");
        return Value::nil();
    }
    if (a.isInteger() && b.isInteger()) {
        return makeInteger(static_cast<int64_t>(static_cast<uint64_t>(a.asInteger()) * static_cast<uint64_t>(b.asInteger())));
    }
    return Value::number(a.asNumber() * b.asNumber());
}

Value VM::divide(const Value& a, const Value& b) {
    if (!a.isNumber() || !b.isNumber()) {
        runtimeError("Operands must be numbers");
        return Value::nil();
    }
    return Value::number(a.asNumber() / b.asNumber());
}

Value VM::integerDivide(const Value& a, const Value& b) {
    if (!a.isNumber() || !b.isNumber()) {
        runtimeError("Operands must be numbers");
        return Value::nil();
    }
    if (a.isInteger() && b.isInteger()) {
        int64_t ib = b.asInteger();
        if (ib == 0) {
            runtimeError("attempt to divide by zero");
            return Value::nil();
        }
        int64_t ia = a.asInteger();
        if (ia == std::numeric_limits<int64_t>::min() && ib == -1) {
            return makeInteger(std::numeric_limits<int64_t>::min());
        }
        int64_t q = ia / ib;
        int64_t r = ia % ib;
        if ((ia ^ ib) < 0 && r != 0) {
            q -= 1;
        }
        return makeInteger(q);
    }
    double db = b.asNumber();
    return Value::number(std::floor(a.asNumber() / db));
}

Value VM::modulo(const Value& a, const Value& b) {
    if (!a.isNumber() || !b.isNumber()) {
        runtimeError("Operands must be numbers");
        return Value::nil();
    }
    if (a.isInteger() && b.isInteger()) {
        int64_t ib = b.asInteger();
        if (ib == 0) {
            runtimeError("attempt to perform 'n%0'");
            return Value::nil();
        }
        int64_t ia = a.asInteger();
        if (ia == std::numeric_limits<int64_t>::min() && ib == -1) {
            return makeInteger(0);
        }
        int64_t r = ia % ib;
        if ((ia ^ ib) < 0 && r != 0) {
            r += ib;
        }
        return makeInteger(r);
    }
    double da = a.asNumber();
    double db = b.asNumber();
    double m = std::fmod(da, db);
    if ((m > 0.0) ? (db < 0.0) : (m < 0.0 && db > 0.0)) {
        m += db;
    }
    return Value::number(m);
}

Value VM::power(const Value& a, const Value& b) {
    if (!a.isNumber() || !b.isNumber()) {
        runtimeError("Operands must be numbers");
        return Value::nil();
    }
    return Value::number(std::pow(a.asNumber(), b.asNumber()));
}

Value VM::bitwiseAnd(const Value& a, const Value& b) {
    int64_t ia, ib;
    if (!toInteger(a, ia) || !toInteger(b, ib)) {
        runtimeError("number has no integer representation");
        return Value::nil();
    }
    return makeInteger(ia & ib);
}

Value VM::bitwiseOr(const Value& a, const Value& b) {
    int64_t ia, ib;
    if (!toInteger(a, ia) || !toInteger(b, ib)) {
        runtimeError("number has no integer representation");
        return Value::nil();
    }
    return makeInteger(ia | ib);
}

Value VM::bitwiseXor(const Value& a, const Value& b) {
    int64_t ia, ib;
    if (!toInteger(a, ia) || !toInteger(b, ib)) {
        runtimeError("number has no integer representation");
        return Value::nil();
    }
    return makeInteger(ia ^ ib);
}

Value VM::shiftLeft(const Value& a, const Value& b) {
    int64_t ia, ib;
    if (!toInteger(a, ia) || !toInteger(b, ib)) {
        runtimeError("number has no integer representation");
        return Value::nil();
    }
    return makeInteger(lua_shift_left(ia, ib));
}

Value VM::shiftRight(const Value& a, const Value& b) {
    int64_t ia, ib;
    if (!toInteger(a, ia) || !toInteger(b, ib)) {
        runtimeError("number has no integer representation");
        return Value::nil();
    }
    return makeInteger(lua_shift_right(ia, ib));
}

Value VM::concat(const Value& a, const Value& b) {
    return Value::runtimeString(internString(getStringValue(a) + getStringValue(b)));
}

Value VM::negate(const Value& a) {
    if (!a.isNumber()) {
        runtimeError("Operand must be a number");
        return Value::nil();
    }
    if (a.isInteger()) {
        return makeInteger(static_cast<int64_t>(0ULL - static_cast<uint64_t>(a.asInteger())));
    }
    return Value::number(-a.asNumber());
}

Value VM::bitwiseNot(const Value& a) {
    int64_t ia;
    if (!toInteger(a, ia)) {
        runtimeError("number has no integer representation");
        return Value::nil();
    }
    return makeInteger(~ia);
}

Value VM::equal(const Value& a, const Value& b) {
    return Value::boolean(a == b);
}

Value VM::less(const Value& a, const Value& b) {
    if (!a.isNumber() || !b.isNumber()) {
        runtimeError("Operands must be numbers");
        return Value::nil();
    }
    if (a.isInteger() && b.isInteger()) {
        return Value::boolean(a.asInteger() < b.asInteger());
    }
    if (a.isInteger() && b.isFloat()) {
        return Value::boolean(LTintfloat(a.asInteger(), b.asNumber()));
    }
    if (a.isFloat() && b.isInteger()) {
        return Value::boolean(LTfloatint(a.asNumber(), b.asInteger()));
    }
    return Value::boolean(a.asNumber() < b.asNumber());
}

Value VM::lessEqual(const Value& a, const Value& b) {
    if (!a.isNumber() || !b.isNumber()) {
        runtimeError("Operands must be numbers");
        return Value::nil();
    }
    if (a.isInteger() && b.isInteger()) {
        return Value::boolean(a.asInteger() <= b.asInteger());
    }
    if (a.isInteger() && b.isFloat()) {
        return Value::boolean(LEintfloat(a.asInteger(), b.asNumber()));
    }
    if (a.isFloat() && b.isInteger()) {
        return Value::boolean(LEfloatint(a.asNumber(), b.asInteger()));
    }
    return Value::boolean(a.asNumber() <= b.asNumber());
}

Value VM::logicalNot(const Value& a) {
    return Value::boolean(a.isFalsey());
}

std::string VM::getStringValue(const Value& value) {
    if (value.isString()) {
        StringObject* str = nullptr;
        if (value.isRuntimeString()) {
            str = value.asStringObj();
        } else {
            str = currentFrame().chunk->getString(value.asStringIndex());
        }
        return str ? std::string(str->chars(), str->length()) : "";
    }
    return value.toString();
}

std::string VM::typeName(const Value& val) {
    if (val.isUserdata()) {
        UserdataObject* ud = val.asUserdataObj();
        if (ud && ud->isLight()) return "light userdata";
        if (ud && !ud->metatable().isNil() && ud->metatable().isTable()) {
            Value nameVal = ud->metatable().asTableObj()->get("__name");
            if (nameVal.isString()) return getStringValue(nameVal);
        }
        return "userdata";
    }
    Value mtVal = Value::nil();
    if (val.isTable()) {
        mtVal = val.asTableObj()->getMetatable();
    } else {
        mtVal = getTypeMetatable(val.type());
    }
    if (!mtVal.isNil() && mtVal.isTable()) {
        Value nameVal = mtVal.asTableObj()->get("__name");
        if (nameVal.isString()) return getStringValue(nameVal);
    }
    return val.typeToString();
}

const void* VM::valueToPointer(const Value& val) const {
    if (val.isNil() || val.isBool() || val.isNumber()) {
        return nullptr;
    }
    if (val.isTable()) {
        return static_cast<const void*>(val.asTableObj());
    }
    if (val.isClosure()) {
        return static_cast<const void*>(val.asClosureObj());
    }
    if (val.isThread()) {
        return static_cast<const void*>(val.asThreadObj());
    }
    if (val.isUserdata()) {
        return static_cast<const void*>(val.asUserdataObj());
    }
    if (val.isFile()) {
        return static_cast<const void*>(val.asFileObj());
    }
    if (val.isSocket()) {
        return static_cast<const void*>(val.asSocketObj());
    }
    if (val.isNativeFunction()) {
        return getNativeFunctionPointer(val.asNativeFunctionIndex());
    }
    if (val.isCFunction()) {
        return val.asCFunction();
    }
    if (val.isString()) {
        if (val.isRuntimeString()) {
            return static_cast<const void*>(val.asStringObj());
        } else if (currentCoroutine_ && !currentCoroutine_->frames.empty()) {
            return static_cast<const void*>(currentCoroutine_->frames.back().chunk->getString(val.asStringIndex()));
        }
    }
    return nullptr;
}

bool VM::toLString(const Value& val, std::string& out) {
    Value mm = getMetamethod(val, "__tostring");
    if (!mm.isNil()) {
        push(mm);
        push(val);
        size_t prevFrames = currentCoroutine_->frames.size();
        if (callValue(1, 2)) {
            if (currentCoroutine_->frames.size() > prevFrames) {
                if (!run(prevFrames)) return false;
            }
            Value res = pop();
            if (!res.isString()) {
                runtimeError("'__tostring' must return a string");
                return false;
            }
            out = getStringValue(res);
            return true;
        }
        return false;
    }

    if (val.isNil()) {
        out = "nil";
        return true;
    }
    if (val.isBool()) {
        out = val.asBool() ? "true" : "false";
        return true;
    }
    if (val.isInteger()) {
        out = std::to_string(val.asInteger());
        return true;
    }
    if (val.isFloat()) {
        out = getStringValue(val);
        return true;
    }
    if (val.isString()) {
        out = getStringValue(val);
        return true;
    }

    std::string kind = typeName(val);

    const void* ptr = valueToPointer(val);
    char buf[64];
    snprintf(buf, sizeof(buf), "%s: %p", kind.c_str(), ptr);
    out = buf;
    return true;
}

Value VM::getMetamethod(const Value& obj, const std::string& method) {
    Value mt = Value::nil();
    if (obj.isTable()) {
        TableObject* table = obj.asTableObj();
        mt = table->getMetatable();
    } else if (obj.isUserdata()) {
        UserdataObject* userdata = obj.asUserdataObj();
        mt = userdata->metatable();
    } else {
        mt = getTypeMetatable(obj.type());
    }

    if (mt.isNil() || !mt.isTable()) return Value::nil();

    TableObject* meta = mt.asTableObj();
    Value mm = meta->get(method);
    
    if (mm.isNil() && method != "__index" && method != "__newindex") {
        Value index = meta->get("__index");
        if (index.isTable()) {
            mm = index.asTableObj()->get(method);
        }
    }

    return mm;
}

Value VM::getTable(const Value& tableVal, const Value& key) {
    Value t = tableVal;
    for (int loop = 0; loop < 100; loop++) {
        if (t.isTable()) {
            TableObject* table = t.asTableObj();
            Value value = table->get(key);
            if (!value.isNil() || table->getMetatable().isNil()) {
                return value;
            }
        }

        if (key.isString()) {
            Value mm = getMetamethod(t, getStringValue(key));
            if (!mm.isNil()) {
                return mm;
            }
        }

        Value indexMethod = getMetamethod(t, "__index");
        if (indexMethod.isNil()) {
            if (!t.isTable()) {
                runtimeError("attempt to index a " + typeName(t) + " value");
            }
            return Value::nil();
        } else if (indexMethod.isFunction()) {
            push(indexMethod);
            push(t);
            push(key);
            size_t prevFrames = currentCoroutine_->frames.size();
            if (!callValue(2, 2)) return Value::nil();
            if (currentCoroutine_->frames.size() > prevFrames) {
                if (!run(prevFrames)) return Value::nil();
            }
            return pop();
        } else if (indexMethod.isTable()) {
            t = indexMethod;
        } else {
            t = indexMethod;
        }
    }
    runtimeError("'__index' chain too long; possible loop");
    return Value::nil();
}

bool VM::callBinaryMetamethod(const Value& a, const Value& b, const std::string& method) {
    Value mm = getMetamethod(a, method);
    if (mm.isNil()) {
        mm = getMetamethod(b, method);
    }

    if (mm.isNil()) return false;

    push(mm);
    push(a);
    push(b);
    std::string mname = (method.rfind("__", 0) == 0) ? method.substr(2) : method;
    return callValue(2, 2, false, mname.c_str());
}

bool VM::callValue(int argCount, int retCount, bool isTailCall, const char* metamethodName, int extraArgs) {
    if (!isTailCall) {
        if (currentCoroutine_->frames.size() >= FRAMES_MAX) {
            hadError_ = true;
            lastErrorMessage_ = "error in error handling";
            lastErrorObject_ = Value::runtimeString(internString(lastErrorMessage_));
            if (isJitExecuting_) return false;
            throw RuntimeError(lastErrorMessage_);
        } else if (currentCoroutine_->frames.size() >= (!isHandlingError_ ? FRAMES_LIMIT : FRAMES_MAX)) {
            runtimeError("stack overflow");
            return false;
        }
    }
    Value callee = peek(argCount);
    
    if (callee.isNativeFunction() || callee.isCFunction() || (callee.isClosure() && callee.asClosureObj()->isC())) {
        size_t funcPosition = currentCoroutine_->stack.size() - argCount - 1;
        currentCoroutine_->lastResultCount = 1;

        CallFrame cframe;
        cframe.closure = callee.isClosure() ? callee.asClosureObj() : nullptr;
        cframe.chunk = nullptr;
        cframe.callerChunk = nullptr;
        cframe.ip = 0;
        cframe.stackBase = funcPosition + 1;
        cframe.retCount = retCount;
        cframe.isPcall = false;
        cframe.isHook = false;
        cframe.isC = true;
        cframe.cFunc = callee;
        cframe.isTailCall = isTailCall;
        cframe.extraargs = extraArgs;
        cframe.errorHandler = Value::nil();
        if (metamethodName) cframe.metamethodName = metamethodName;
        if (callee.isNativeFunction()) {
            size_t funcIndex = callee.asNativeFunctionIndex();
            const std::string& fname = nativeFunctions_[funcIndex].name;
            if (fname == "pcall" || fname == "xpcall") {
                cframe.isPcall = true;
                if (fname == "xpcall" && argCount >= 2) {
                    cframe.errorHandler = peek(argCount - 2);
                }
            }
        }

        size_t cframeIndex = currentCoroutine_->frames.size();
        struct FrameGuard {
            std::vector<CallFrame>& frames;
            size_t cframeIndex;
            CoroutineObject* co;
            FrameGuard(std::vector<CallFrame>& f, const CallFrame& fr, size_t idx, CoroutineObject* c) 
                : frames(f), cframeIndex(idx), co(c) {
                frames.push_back(fr);
                co->nCcalls++;
            }
            ~FrameGuard() {
                co->nCcalls--;
                if (co->status == CoroutineObject::Status::SUSPENDED && frames.size() >= cframeIndex + 1) {
                    return;
                }
                if (std::uncaught_exceptions() > 0) {
                    return;
                }
                if (frames.size() > cframeIndex) {
                    frames.erase(frames.begin() + cframeIndex);
                }
            }
        } guard(currentCoroutine_->frames, cframe, cframeIndex, currentCoroutine_);

        if (currentCoroutine_->nCcalls >= 220) {
            hadError_ = true;
            lastErrorMessage_ = "error in error handling";
            lastErrorObject_ = Value::runtimeString(internString(lastErrorMessage_));
            if (isJitExecuting_) return false;
            throw RuntimeError(lastErrorMessage_);
        } else if (currentCoroutine_->nCcalls == 200 || (currentCoroutine_->nCcalls >= 200 && !isHandlingError_)) {
            runtimeError("C stack overflow", 0);
            return false;
        }

        if (currentCoroutine_->hookMask & CoroutineObject::MASK_CALL) {
            callHook("call", -1, 1, argCount);
        }

        if (callee.isClosure() && callee.asClosureObj()->isC()) {
            ClosureObject* ccl = callee.asClosureObj();
            if (ccl->nativeFunc()) {
                if (!ccl->nativeFunc()(this, argCount)) {
                    return false;
                }
            } else if (ccl->cFunc()) {
                lua_CFunction function = ccl->cFunc();
                lua_State L;
                L.vm = this;
                L.is_owned = false;
                L.stackBase = funcPosition + 1;
                L.argCount = argCount;
                L.currentClosure = ccl;
                
                struct CurrentLGuard {
                    lua_State*& target;
                    lua_State* old;
                    CurrentLGuard(lua_State*& t, lua_State* n) : target(t), old(t) {
                        target = n;
                    }
                    ~CurrentLGuard() {
                        target = old;
                    }
                } lguard(currentL_, &L);
                int nres = function(&L);
                
                currentCoroutine_->lastResultCount = nres;
            }
        } else if (callee.isNativeFunction()) {
            NativeFunction function = nativeFunctions_[callee.asNativeFunctionIndex()].func;
            if (!function(this, argCount)) {
                return false;
            }
        } else {
            lua_CFunction function = reinterpret_cast<lua_CFunction>(callee.asCFunction());
            lua_State L;
            L.vm = this;
            L.is_owned = false;
            L.stackBase = funcPosition + 1;
            L.argCount = argCount;
            L.currentClosure = nullptr;
            
            struct CurrentLGuard {
                lua_State*& target;
                lua_State* old;
                CurrentLGuard(lua_State*& t, lua_State* n) : target(t), old(t) {
                    target = n;
                }
                ~CurrentLGuard() {
                    target = old;
                }
            } lguard(currentL_, &L);
            int nres = function(&L);
            
            currentCoroutine_->lastResultCount = nres;
        }

        if (currentCoroutine_->hookMask & CoroutineObject::MASK_RET) {
            callHook("return", -1, 1, static_cast<int>(currentCoroutine_->lastResultCount));
        }

        if (currentCoroutine_->status == CoroutineObject::Status::SUSPENDED) {
            if (currentCoroutine_->frames.size() == cframeIndex + 1) {
                while (currentCoroutine_->stack.size() > funcPosition) {
                    pop();
                }
            }
            return true;
        }

        size_t resultCount = currentCoroutine_->lastResultCount;
        if (currentCoroutine_->status == CoroutineObject::Status::SUSPENDED) {
            resultCount = 0;
        }

        std::vector<Value> results;
        results.reserve(resultCount);
        for (size_t i = 0; i < resultCount; i++) {
            results.push_back(pop());
        }
        std::reverse(results.begin(), results.end());

        // Now the stack should have the closure and any remaining arguments
        // We pop everything down to the closure position and then pop the closure too
        while (currentCoroutine_->stack.size() > funcPosition) {
            pop();
        }
        // If the closure was not already popped by the results loop
        if (currentCoroutine_->stack.size() > funcPosition) {
            pop(); 
        }

        if (currentCoroutine_->status == CoroutineObject::Status::SUSPENDED) {
            return true;
        }

        if (retCount > 0) {
            size_t expected = static_cast<size_t>(retCount - 1);
            if (results.size() > expected) {
                results.resize(expected);
            } else {
                while (results.size() < expected) {
                    results.push_back(Value::nil());
                }
            }
        }

        for (const auto& res : results) {
            push(res);
        }
        currentCoroutine_->lastResultCount = results.size();
        return true;
    } else if (callee.isClosure()) {
        ClosureObject* closure = callee.asClosureObj();
        FunctionObject* function = closure->function();

        if (isTailCall && !currentCoroutine_->frames.empty()) {
            size_t oldStackBase = currentFrame().stackBase;
            closeUpvalues(oldStackBase);
            
            size_t newStackBase = currentCoroutine_->stack.size() - argCount;
            size_t calleePos = newStackBase - 1;
            
            for (int i = 0; i <= argCount; i++) {
                Value val = currentCoroutine_->stack[calleePos + i];
                if (val.isObj()) writeBarrierBackward(currentCoroutine_, val.asObj());
                currentCoroutine_->stack[oldStackBase - 1 + i] = val;
            }
            
            while (currentCoroutine_->stack.size() > oldStackBase + argCount) {
                currentCoroutine_->stack.pop_back();
            }
            
            CallFrame& frame = currentFrame();
            frame.closure = closure;
            frame.chunk = function->chunk();
            frame.ip = 0;
            frame.stackBase = oldStackBase;
            frame.varargs.clear();
            frame.isTailCall = true;
            frame.extraargs = extraArgs;
            if (metamethodName) frame.metamethodName = metamethodName;

            int arity = function->arity();
            if (argCount < arity) {
                for (int i = 0; i < arity - argCount; i++) {
                    push(Value::nil());
                }
            } else if (argCount > arity && function->hasVarargs()) {
                for (int i = 0; i < argCount - arity; i++) {
                    frame.varargs.push_back(currentCoroutine_->stack[frame.stackBase + arity + i]);
                }
                for (int i = 0; i < argCount - arity; i++) {
                    currentCoroutine_->stack.pop_back();
                }
            } else if (argCount > arity && !function->hasVarargs()) {
                for (int i = 0; i < argCount - arity; i++) {
                    currentCoroutine_->stack.pop_back();
                }
            }
            currentCoroutine_->chunk = function->chunk();
            return true;
        }

        CallFrame frame;
        frame.closure = closure;
        frame.chunk = function->chunk();
        frame.ip = 0;
        frame.stackBase = currentCoroutine_->stack.size() - argCount;
        frame.retCount = retCount;
        frame.callerChunk = currentCoroutine_->frames.empty() ? nullptr : currentFrame().chunk;
        frame.isTailCall = false;
        frame.extraargs = extraArgs;
        if (metamethodName) frame.metamethodName = metamethodName;
        
        int arity = function->arity();
        if (argCount < arity) {
            for (int i = 0; i < arity - argCount; i++) {
                push(Value::nil());
            }
        } else if (argCount > arity && function->hasVarargs()) {
            for (int i = 0; i < argCount - arity; i++) {
                frame.varargs.push_back(currentCoroutine_->stack[frame.stackBase + arity + i]);
            }
            for (int i = 0; i < argCount - arity; i++) {
                currentCoroutine_->stack.pop_back();
            }
        } else if (argCount > arity && !function->hasVarargs()) {
            for (int i = 0; i < argCount - arity; i++) {
                currentCoroutine_->stack.pop_back();
            }
        }
        
        currentCoroutine_->frames.push_back(std::move(frame));
        currentCoroutine_->chunk = function->chunk();
        return true;
    }
    
    Value mm = getMetamethod(callee, "__call");
    if (!mm.isNil()) {
        if (extraArgs >= 15) {
            runtimeError("'__call' chain too long; possible loop");
            return false;
        }
        size_t calleePos = currentCoroutine_->stack.size() - argCount - 1;
        currentCoroutine_->stack.insert(currentCoroutine_->stack.begin() + calleePos, mm);
        return callValue(argCount + 1, retCount, isTailCall, nullptr, extraArgs + 1);
    }
    
    std::string err = "attempt to call a " + typeName(callee) + " value";
    if (metamethodName) {
        err += " (metamethod '" + std::string(metamethodName) + "')";
    } else if (!currentCoroutine_->frames.empty()) {
        const CallFrame& frame = currentFrame();
        if (frame.chunk && frame.ip >= 2) {
            const auto& code = frame.chunk->code();
            size_t opIp = (size_t)-1;
            int callArgCount = argCount;
            if (frame.ip >= 3 && (static_cast<OpCode>(code[frame.ip - 3]) == OpCode::OP_CALL ||
                                 static_cast<OpCode>(code[frame.ip - 3]) == OpCode::OP_CALL_MULTI)) {
                opIp = frame.ip - 3;
            } else if (frame.ip >= 2 && (static_cast<OpCode>(code[frame.ip - 2]) == OpCode::OP_TAILCALL ||
                                        static_cast<OpCode>(code[frame.ip - 2]) == OpCode::OP_TAILCALL_MULTI)) {
                opIp = frame.ip - 2;
            }
            if (opIp != (size_t)-1) {
                err += getCallVarInfo(opIp, callArgCount);
            }
        }
    }
    runtimeError(err);
    return false;
}

bool VM::resumeCoroutine(CoroutineObject* co) {
    if (co->status == CoroutineObject::Status::DEAD) {
        runtimeError("cannot resume dead coroutine");
        return false;
    }

    CoroutineObject* oldCo = currentCoroutine_;
    int callerCcalls = oldCo ? oldCo->nCcalls : 0;
    int maxCCalls = !isHandlingError_ ? 200 : 250;
    if (callerCcalls >= maxCCalls) {
        lastErrorMessage_ = "C stack overflow";
        lastErrorObject_ = Value::runtimeString(internString(lastErrorMessage_));
        return false;
    }

    currentCoroutine_ = co;
    co->status = CoroutineObject::Status::RUNNING;
    co->caller = oldCo;
    int oldCoCcalls = co->nCcalls;
    co->nCcalls = callerCcalls + 1;

    struct ResumeGuard {
        VM* vm;
        CoroutineObject* oldCo;
        CoroutineObject* co;
        int oldCoCcalls;
        ~ResumeGuard() {
            vm->currentCoroutine_ = oldCo;
            if (co->status == CoroutineObject::Status::DEAD ||
                co->status == CoroutineObject::Status::SUSPENDED) {
                co->nCcalls = 0;
            } else {
                co->nCcalls = oldCoCcalls;
            }
        }
    } resumeGuard{this, oldCo, co, oldCoCcalls};

    bool result = false;
    try {
        if (!co->initialFunc.isNil()) {
            Value initFunc = co->initialFunc;
            co->initialFunc = Value::nil();
            size_t argCount = co->stack.size();
            co->stack.insert(co->stack.begin(), initFunc);
            if (!callValue(static_cast<int>(argCount), 0)) {
                result = false;
            } else {
                if (co->frames.empty()) {
                    if (co->status != CoroutineObject::Status::SUSPENDED) {
                        co->status = CoroutineObject::Status::DEAD;
                    }
                    result = true;
                } else {
                    result = run(0);
                }
            }
        } else {
            if (!co->frames.empty() && co->frames.back().isC) {
                co->frames.pop_back();
            }
            if (!co->frames.empty() && co->frames[0].ip == 0 && (co->hookMask & CoroutineObject::MASK_CALL)) {
                callHook("call");
            }
            result = run(0);
        }
    } catch (const CoroutineCloseSelfException& e) {
        co->status = CoroutineObject::Status::DEAD;
        co->stack.clear();
        co->frames.clear();
        if (!e.errorObj.isNil()) {
            co->closeError = e.errorObj;
            hadError_ = false;
            isHandlingError_ = false;
            lastErrorObject_ = e.errorObj;
            lastErrorMessage_ = e.errorObj.isString() ? getStringValue(e.errorObj) : e.errorObj.toString();
            currentCoroutine_ = oldCo;
            return false;
        } else {
            currentCoroutine_ = oldCo;
            currentCoroutine_->lastResultCount = 0;
            return true;
        }
    } catch (const RuntimeError& e) {
        co->status = CoroutineObject::Status::DEAD;
        Value errObj = lastErrorObject_.isNil() ? Value::runtimeString(internString(lastErrorMessage_)) : lastErrorObject_;
        try {
            closeUpvalues(0, co, errObj);
        } catch (...) {}
        if (!lastErrorObject_.isNil()) {
            errObj = lastErrorObject_;
        }
        hadError_ = false;
        isHandlingError_ = false;
        co->closeError = errObj;
        lastErrorObject_ = errObj;
        lastErrorMessage_ = errObj.isString() ? getStringValue(errObj) : errObj.toString();
        currentCoroutine_ = oldCo;
        return false;
    } catch (const std::exception& e) {
        co->status = CoroutineObject::Status::DEAD;
        Value errObj = Value::runtimeString(internString(e.what()));
        try {
            closeUpvalues(0, co, errObj);
        } catch (...) {}
        if (!lastErrorObject_.isNil()) {
            errObj = lastErrorObject_;
        }
        hadError_ = false;
        isHandlingError_ = false;
        co->closeError = errObj;
        lastErrorObject_ = errObj;
        lastErrorMessage_ = errObj.isString() ? getStringValue(errObj) : errObj.toString();
        currentCoroutine_ = oldCo;
        return false;
    }

    currentCoroutine_ = oldCo;

    if (result) {
        size_t count = (co->status == CoroutineObject::Status::SUSPENDED) 
            ? co->yieldedValues.size() 
            : co->stack.size();
        if (oldCo && oldCo->stack.size() + count + 1 > STACK_LIMIT) {
            co->stack.clear();
            co->yieldedValues.clear();
            co->status = CoroutineObject::Status::DEAD;
            lastErrorMessage_ = "too many results to resume";
            lastErrorObject_ = Value::runtimeString(internString(lastErrorMessage_));
            return false;
        }

        if (co->status == CoroutineObject::Status::SUSPENDED) {
            for (const auto& val : co->yieldedValues) {
                push(val);
            }
            currentCoroutine_->lastResultCount = co->yieldedValues.size();
        } else if (co->status == CoroutineObject::Status::DEAD) {
            size_t n = co->stack.size();
            for (const auto& val : co->stack) {
                push(val);
            }
            currentCoroutine_->lastResultCount = n;
            co->stack.clear();
        }
    }

    return result;
}

void VM::setRegistry(const std::string& key, const Value& value) {
    if (registryTable_) {
        registryTable_->set(key, value);
    }
}

Value VM::getRegistry(const std::string& key) const {
    if (registryTable_) {
        return registryTable_->get(key);
    }
    return Value::nil();
}

void VM::setTypeMetatable(Value::Type type, const Value& mt) {
    if (type == Value::Type::INTEGER || type == Value::Type::INT64 || type == Value::Type::NUMBER) {
        int idx1 = static_cast<int>(Value::Type::INTEGER) & 0x0F;
        int idx2 = static_cast<int>(Value::Type::NUMBER) & 0x0F;
        int idx3 = static_cast<int>(Value::Type::INT64) & 0x0F;
        if (idx1 >= 0 && idx1 < Value::NUM_TYPES) typeMetatables_[idx1] = mt;
        if (idx2 >= 0 && idx2 < Value::NUM_TYPES) typeMetatables_[idx2] = mt;
        if (idx3 >= 0 && idx3 < Value::NUM_TYPES) typeMetatables_[idx3] = mt;
        return;
    }
    int index = static_cast<int>(type) & 0x0F;
    if (index >= 0 && index < Value::NUM_TYPES) {
        typeMetatables_[index] = mt;
    }
}

Value VM::getTypeMetatable(Value::Type type) const {
    if (type == Value::Type::INTEGER || type == Value::Type::INT64) {
        type = Value::Type::NUMBER;
    }
    int index = static_cast<int>(type) & 0x0F;
    if (index >= 0 && index < Value::NUM_TYPES) {
        return typeMetatables_[index];
    }
    return Value::nil();
}

CallFrame* VM::getFrame(int level) {
    if (level < 0 || static_cast<size_t>(level) >= currentCoroutine_->frames.size()) {
        return nullptr;
    }
    return &currentCoroutine_->frames[currentCoroutine_->frames.size() - 1 - level];
}

void VM::callHook(const char* event, int line, int ftransfer, int ntransfer) {
    if (currentCoroutine_->inHook) return;
    struct HookGuard {
        CoroutineObject* co;
        bool prevInHook;
        CoroutineObject::Status prevStatus;
        size_t savedLastResultCount;
        const Chunk* savedChunk;
        size_t interruptedFrameIdx;
        bool prevInterrupted;
        HookGuard(CoroutineObject* c) 
            : co(c), prevInHook(c->inHook), prevStatus(c->status),
              savedLastResultCount(c->lastResultCount), savedChunk(c->chunk),
              interruptedFrameIdx(c->frames.empty() ? static_cast<size_t>(-1) : c->frames.size() - 1),
              prevInterrupted(false) {
            co->inHook = true;
            co->status = CoroutineObject::Status::RUNNING;
            if (interruptedFrameIdx < co->frames.size()) {
                prevInterrupted = co->frames[interruptedFrameIdx].isInterruptedByHook;
                co->frames[interruptedFrameIdx].isInterruptedByHook = true;
            }
        }
        ~HookGuard() {
            co->inHook = prevInHook;
            co->status = prevStatus;
            co->lastResultCount = savedLastResultCount;
            co->chunk = savedChunk;
            if (interruptedFrameIdx < co->frames.size()) {
                co->frames[interruptedFrameIdx].isInterruptedByHook = prevInterrupted;
            }
        }
    } guard(currentCoroutine_);
    
    if (currentCoroutine_->hook.isFunction()) {
        if (!currentCoroutine_->frames.empty()) {
            if (ftransfer == 0 && ntransfer == 0) {
                if (std::string_view(event) == "call" || std::string_view(event) == "tail call") {
                    FunctionObject* func = currentFrame().closure ? currentFrame().closure->function() : nullptr;
                    ftransfer = 1;
                    ntransfer = func ? func->arity() : 0;
                }
            }
            currentCoroutine_->frames.back().ftransfer = ftransfer;
            currentCoroutine_->frames.back().ntransfer = ntransfer;
        }
        push(currentCoroutine_->hook);
        push(Value::runtimeString(internString(event)));
        if (line != -1) {
            push(Value::number(static_cast<double>(line)));
        } else {
            push(Value::nil());
        }
        
        if (callValue(2, 1)) {
            if (!currentCoroutine_->frames.empty()) {
                currentCoroutine_->frames.back().isHook = true;
            }
            run(currentCoroutine_->frames.size() - 1);
        }
    }
}

void VM::jitGetTable(VM* vm, uint32_t nextIp) {
    vm->getFrame(0)->ip = nextIp;
    Value key = vm->pop();
    Value tableValue = vm->pop();

    if (tableValue.isTable()) {
        TableObject* table = tableValue.asTableObj();
        Value value = table->get(key);
        if (!value.isNil()) {
            vm->push(value);
            return;
        }
    }

    if (key.isString()) {
        Value mm = vm->getMetamethod(tableValue, vm->getStringValue(key));
        if (!mm.isNil()) {
            vm->push(mm);
            return;
        }
    }

    Value indexMethod = vm->getMetamethod(tableValue, "__index");
    if (indexMethod.isNil()) {
        if (!tableValue.isTable()) {
            vm->runtimeError("attempt to index a " + vm->typeName(tableValue) + " value" + vm->getVarInfo(nextIp - 1, 0));
        }
        vm->push(Value::nil());
    } else {
        if (indexMethod.isFunction()) {
            vm->push(indexMethod);
            vm->push(tableValue);
            vm->push(key);
            vm->callValue(2, 2, false, "index"); 
        } else if (indexMethod.isTable()) {
            TableObject* indexTable = indexMethod.asTableObj();
            Value result = key.isString() ? indexTable->get(vm->getStringValue(key)) : indexTable->get(key);
            vm->push(result);
        } else {
            vm->push(Value::nil());
        }
    }
}

void VM::jitSetTable(VM* vm, uint32_t nextIp) {
    vm->getFrame(0)->ip = nextIp;
    Value value = vm->peek(0);
    Value key = vm->peek(1);
    Value tableValue = vm->peek(2);

    if (tableValue.isTable()) {
        TableObject* table = tableValue.asTableObj();
        if (table->has(key)) {
            if (key.isNil()) {
                vm->runtimeError("table index is nil");
                return;
            }
            if (key.isFloat() && std::isnan(key.asNumber())) {
                vm->runtimeError("table index is NaN");
                return;
            }
            table->set(key, value);
            vm->pop(); vm->pop(); vm->pop();
            return;
        }
    }

    Value newIndex = vm->getMetamethod(tableValue, "__newindex");
    if (newIndex.isNil()) {
        if (tableValue.isTable()) {
            if (key.isNil()) {
                vm->runtimeError("table index is nil");
                return;
            }
            if (key.isFloat() && std::isnan(key.asNumber())) {
                vm->runtimeError("table index is NaN");
                return;
            }
            tableValue.asTableObj()->set(key, value);
        } else {
            vm->runtimeError("attempt to index a " + vm->typeName(tableValue) + " value" + vm->getVarInfo(nextIp - 1, 2));
        }
        vm->pop(); vm->pop(); vm->pop();
    } else {
        vm->getFrame(0)->ip = nextIp;
        if (newIndex.isFunction()) {
            vm->currentCoroutine_->stack.insert(vm->currentCoroutine_->stack.end() - 3, newIndex);
            vm->callValue(3, 1, false, "newindex");
        } else if (newIndex.isTable()) {
            TableObject* niTable = newIndex.asTableObj();
            if (key.isString()) {
                niTable->set(vm->getStringValue(key), value);
            } else {
                niTable->set(key, value);
            }
            vm->pop(); vm->pop(); vm->pop();
        } else {
            if (tableValue.isTable()) {
                tableValue.asTableObj()->set(key, value);
            } else {
                vm->runtimeError("attempt to index a " + vm->typeName(tableValue) + " value" + vm->getVarInfo(nextIp - 1, 2));
            }
            vm->pop(); vm->pop(); vm->pop();
        }
    }
}

void VM::jitNewTable(VM* vm) {
    TableObject* table = vm->createTable();
    vm->push(Value::table(table));
}

void VM::jitGetUpvalue(VM* vm, uint32_t index) {
    if (!vm->currentCoroutine_->frames.empty()) {
        UpvalueObject* upvalue = vm->getFrame(0)->closure->getUpvalueObj(index);
        if (upvalue) {
            vm->push(upvalue->get(vm->currentCoroutine_->stack));
        } else {
            vm->push(Value::nil());
        }
    } else {
        vm->push(Value::nil());
    }
}

void VM::jitSetUpvalue(VM* vm, uint32_t index) {
    Value val = vm->peek(0);
    if (!vm->currentCoroutine_->frames.empty()) {
        UpvalueObject* upvalue = vm->getFrame(0)->closure->getUpvalueObj(index);
        if (upvalue) {
            upvalue->set(vm->currentCoroutine_->stack, val);
        }
    }
}

void VM::jitConcat(VM* vm, uint32_t nextIp) {
    Value b = vm->pop();
    Value a = vm->pop();
    if ((a.isString() || a.isNumber()) && (b.isString() || b.isNumber())) {
        vm->push(vm->concat(a, b));
    } else {
        vm->getFrame(0)->ip = nextIp;
        if (!vm->callBinaryMetamethod(a, b, "__concat")) {
            Value badVal = !(a.isString() || a.isNumber()) ? a : b;
            vm->runtimeError("attempt to concatenate a " + vm->typeName(badVal) + " value");
        }
    }
}

void VM::jitCloseUpvalues(VM* vm, uint32_t stackIndex) {
    vm->closeUpvalues(stackIndex);
}

void VM::jitClosure(VM* vm, uint32_t constantIndex, uint32_t bytecodeOffset) {
    Value funcValue = vm->getFrame(0)->chunk->constants()[constantIndex];
    size_t funcIndex = funcValue.asFunctionIndex();
    FunctionObject* function = vm->getFrame(0)->chunk->getFunction(funcIndex);

    ClosureObject* closure = vm->createClosure(function);

    const std::vector<uint8_t>& code = vm->getFrame(0)->chunk->code();
    size_t offset = bytecodeOffset;

    for (size_t i = 0; i < closure->upvalueCount(); i++) {
        uint8_t isLocal = code[offset++];
        uint8_t index = code[offset++];

        if (isLocal) {
            size_t stackIndex = vm->getFrame(0)->stackBase + index;
            UpvalueObject* upvalue = vm->captureUpvalue(stackIndex);
            closure->setUpvalue(i, upvalue);
        } else {
            UpvalueObject* upvalue = vm->getFrame(0)->closure->getUpvalueObj(index);
            closure->setUpvalue(i, upvalue);
        }
    }

    vm->push(Value::closure(closure));
}

void VM::jitCall(VM* vm, uint32_t argCount, uint32_t retCount, uint32_t nextIp) {
    vm->getFrame(0)->ip = nextIp;
    vm->callValue(argCount, retCount);
}

void VM::jitCallMulti(VM* vm, uint32_t fixedArgCount, uint32_t retCount, uint32_t nextIp) {
    vm->getFrame(0)->ip = nextIp;
    int actualArgCount = static_cast<int>(fixedArgCount) + static_cast<int>(vm->currentCoroutine_->lastResultCount);
    vm->callValue(actualArgCount, retCount);
}

void VM::jitTailCall(VM* vm, uint32_t argCount, uint32_t nextIp) {
    vm->getFrame(0)->ip = nextIp;
    vm->callValue(argCount, 0, true);
}

void VM::jitTailCallMulti(VM* vm, uint32_t fixedArgCount, uint32_t nextIp) {
    vm->getFrame(0)->ip = nextIp;
    int actualArgCount = static_cast<int>(fixedArgCount) + static_cast<int>(vm->currentCoroutine_->lastResultCount);
    vm->callValue(actualArgCount, 0, true);
}

void VM::jitReturnValue(VM* vm, uint32_t count) {
    size_t stackBase = vm->getFrame(0)->stackBase;
    vm->closeUpvalues(stackBase);
    
    std::vector<Value> returnValues;
    returnValues.reserve(count);
    for (size_t i = 0; i < count; i++) {
        returnValues.push_back(vm->pop());
    }
    std::reverse(returnValues.begin(), returnValues.end());

    uint8_t expectedRetCount = vm->getFrame(0)->retCount;
    const Chunk* returnChunk = vm->getFrame(0)->callerChunk;

    while (vm->currentCoroutine_->stack.size() > stackBase) {
        vm->pop();
    }
    vm->pop(); 

    vm->currentCoroutine_->frames.pop_back();

    if (vm->currentCoroutine_->frames.empty()) {
        vm->currentCoroutine_->status = CoroutineObject::Status::DEAD;
    } else {
        vm->currentCoroutine_->chunk = returnChunk;
    }

    vm->currentCoroutine_->lastResultCount = returnValues.size();

    if (expectedRetCount > 0) {
        size_t expected = static_cast<size_t>(expectedRetCount - 1);
        if (returnValues.size() > expected) {
            returnValues.resize(expected);
        } else {
            while (returnValues.size() < expected) {
                returnValues.push_back(Value::nil());
            }
        }
    }

    for (const auto& value : returnValues) {
        vm->push(value);
    }
}

void VM::jitEnsureStack(VM* vm, uint32_t needed) {
    auto& stack = vm->currentCoroutine_->stack;
    if (stack.size() + needed >= stack.capacity()) {
        stack.reserve(std::max(stack.capacity() * 2, stack.size() + needed + 1024));
    }
}

void VM::jitGetGlobal(VM* vm, uint32_t nameIndex) {
    Value name = vm->getFrame(0)->chunk->constants()[nameIndex];
    vm->push(vm->getGlobal(vm->getStringValue(name)));
}

void VM::jitSetGlobal(VM* vm, uint32_t nameIndex) {
    Value name = vm->getFrame(0)->chunk->constants()[nameIndex];
    vm->setGlobal(vm->getStringValue(name), vm->peek(0));
}

void VM::jitGetTabUp(VM* vm, uint32_t upIndex, uint32_t keyIndex, uint32_t nextIp) {
    UpvalueObject* upvalue = vm->getFrame(0)->closure->getUpvalueObj(upIndex);
    Value upTable = upvalue->get(vm->currentCoroutine_->stack);
    Value key = vm->getFrame(0)->chunk->constants()[keyIndex];
    
    if (upTable.isTable()) {
        TableObject* table = upTable.asTableObj();
        Value value = table->get(key);
        if (!value.isNil()) {
            vm->push(value);
            return;
        }
    }
    
    vm->push(upTable);
    vm->push(key);
    vm->jitGetTable(vm, nextIp);
}

void VM::jitSetTabUp(VM* vm, uint32_t upIndex, uint32_t keyIndex, uint32_t nextIp) {
    UpvalueObject* upvalue = vm->getFrame(0)->closure->getUpvalueObj(upIndex);
    Value upTable = upvalue->get(vm->currentCoroutine_->stack);
    Value key = vm->getFrame(0)->chunk->constants()[keyIndex];
    Value val = vm->peek(0);
    
    if (upTable.isTable()) {
        TableObject* table = upTable.asTableObj();
        if (table->has(key)) {
            table->set(key, val);
            vm->pop(); // MUST POP the value!
            return;
        }
    }
    
    vm->currentCoroutine_->stack.insert(vm->currentCoroutine_->stack.end() - 1, key);
    vm->currentCoroutine_->stack.insert(vm->currentCoroutine_->stack.end() - 2, upTable);
    vm->jitSetTable(vm, nextIp);
}

void VM::jitLen(VM* vm, uint32_t nextIp) {
    Value a = vm->peek(0);
    if (a.isString()) {
        vm->pop();
        vm->push(Value::number(static_cast<double>(a.asStringObj()->length())));
    } else if (a.isTable()) {
        vm->pop();
        vm->push(Value::number(static_cast<double>(a.asTableObj()->length())));
    } else {
        Value mm = vm->getMetamethod(a, "__len");
        if (mm.isNil()) {
            vm->runtimeError("attempt to get length of a " + a.typeToString() + " value");
            return;
        }
        vm->getFrame(0)->ip = nextIp;
        vm->pop();
        vm->push(mm);
        vm->push(a);
        vm->callValue(1, 2);
    }
}

void VM::jitAdd(VM* vm, uint32_t nextIp) {
    Value b = vm->pop();
    Value a = vm->pop();
    if (a.isNumber() && b.isNumber()) {
        vm->push(vm->add(a, b));
    } else {
        vm->getFrame(0)->ip = nextIp;
        if (!vm->callBinaryMetamethod(a, b, "__add")) {
            vm->runtimeError("attempt to perform arithmetic on " + a.typeToString() + " and " + b.typeToString());
        }
    }
}

void VM::jitSub(VM* vm, uint32_t nextIp) {
    Value b = vm->pop();
    Value a = vm->pop();
    if (a.isNumber() && b.isNumber()) {
        vm->push(vm->subtract(a, b));
    } else {
        vm->getFrame(0)->ip = nextIp;
        if (!vm->callBinaryMetamethod(a, b, "__sub")) {
            vm->runtimeError("attempt to perform arithmetic on " + a.typeToString() + " and " + b.typeToString());
        }
    }
}

void VM::jitMul(VM* vm, uint32_t nextIp) {
    Value b = vm->pop();
    Value a = vm->pop();
    if (a.isNumber() && b.isNumber()) {
        vm->push(vm->multiply(a, b));
    } else {
        vm->getFrame(0)->ip = nextIp;
        if (!vm->callBinaryMetamethod(a, b, "__mul")) {
            vm->runtimeError("attempt to perform arithmetic on " + a.typeToString() + " and " + b.typeToString());
        }
    }
}

void VM::jitDiv(VM* vm, uint32_t nextIp) {
    Value b = vm->pop();
    Value a = vm->pop();
    if (a.isNumber() && b.isNumber()) {
        vm->push(vm->divide(a, b));
    } else {
        vm->getFrame(0)->ip = nextIp;
        if (!vm->callBinaryMetamethod(a, b, "__div")) {
            vm->runtimeError("attempt to perform arithmetic on " + a.typeToString() + " and " + b.typeToString());
        }
    }
}

void VM::jitMod(VM* vm, uint32_t nextIp) {
    Value b = vm->pop();
    Value a = vm->pop();
    if (a.isNumber() && b.isNumber()) {
        vm->push(vm->modulo(a, b));
    } else {
        vm->getFrame(0)->ip = nextIp;
        if (!vm->callBinaryMetamethod(a, b, "__mod")) {
            vm->runtimeError("attempt to perform arithmetic on " + a.typeToString() + " and " + b.typeToString());
        }
    }
}

void VM::jitIDiv(VM* vm, uint32_t nextIp) {
    Value b = vm->pop();
    Value a = vm->pop();
    if (a.isNumber() && b.isNumber()) {
        vm->push(vm->integerDivide(a, b));
    } else {
        vm->getFrame(0)->ip = nextIp;
        if (!vm->callBinaryMetamethod(a, b, "__idiv")) {
            vm->runtimeError("attempt to perform arithmetic on " + a.typeToString() + " and " + b.typeToString());
        }
    }
}

void VM::jitPow(VM* vm, uint32_t nextIp) {
    Value b = vm->pop();
    Value a = vm->pop();
    if (a.isNumber() && b.isNumber()) {
        vm->push(vm->power(a, b));
    } else {
        vm->getFrame(0)->ip = nextIp;
        if (!vm->callBinaryMetamethod(a, b, "__pow")) {
            vm->runtimeError("attempt to perform arithmetic on " + a.typeToString() + " and " + b.typeToString());
        }
    }
}

void VM::jitNeg(VM* vm, uint32_t nextIp) {
    Value a = vm->pop();
    if (a.isNumber()) {
        vm->push(vm->negate(a));
    } else {
        Value mm = vm->getMetamethod(a, "__unm");
        if (mm.isNil()) {
            vm->runtimeError("attempt to perform arithmetic on a " + a.typeToString() + " value");
            return;
        }
        vm->getFrame(0)->ip = nextIp;
        vm->push(mm);
        vm->push(a);
        vm->callValue(1, 2);
    }
}

void VM::jitBand(VM* vm, uint32_t nextIp) {
    Value b = vm->pop();
    Value a = vm->pop();
    int64_t ia, ib;
    bool okA = vm->toIntegerNoString(a, ia);
    bool okB = vm->toIntegerNoString(b, ib);
    if (okA && okB) {
        vm->push(vm->makeInteger(ia & ib));
    } else {
        vm->getFrame(0)->ip = nextIp;
        if (!vm->callBinaryMetamethod(a, b, "__band")) {
            if (a.isNumber() && b.isNumber()) {
                int opIdx = !okA ? 0 : 1;
                vm->runtimeError("number" + vm->getVarInfo(nextIp - 1, opIdx) + " has no integer representation");
            } else {
                vm->runtimeError("attempt to perform bitwise operation on " + a.typeToString() + " and " + b.typeToString());
            }
        }
    }
}

void VM::jitBor(VM* vm, uint32_t nextIp) {
    Value b = vm->pop();
    Value a = vm->pop();
    int64_t ia, ib;
    bool okA = vm->toIntegerNoString(a, ia);
    bool okB = vm->toIntegerNoString(b, ib);
    if (okA && okB) {
        vm->push(vm->makeInteger(ia | ib));
    } else {
        vm->getFrame(0)->ip = nextIp;
        if (!vm->callBinaryMetamethod(a, b, "__bor")) {
            if (a.isNumber() && b.isNumber()) {
                int opIdx = !okA ? 0 : 1;
                vm->runtimeError("number" + vm->getVarInfo(nextIp - 1, opIdx) + " has no integer representation");
            } else {
                vm->runtimeError("attempt to perform bitwise operation on " + a.typeToString() + " and " + b.typeToString());
            }
        }
    }
}

void VM::jitBxor(VM* vm, uint32_t nextIp) {
    Value b = vm->pop();
    Value a = vm->pop();
    int64_t ia, ib;
    bool okA = vm->toIntegerNoString(a, ia);
    bool okB = vm->toIntegerNoString(b, ib);
    if (okA && okB) {
        vm->push(vm->makeInteger(ia ^ ib));
    } else {
        vm->getFrame(0)->ip = nextIp;
        if (!vm->callBinaryMetamethod(a, b, "__bxor")) {
            if (a.isNumber() && b.isNumber()) {
                int opIdx = !okA ? 0 : 1;
                vm->runtimeError("number" + vm->getVarInfo(nextIp - 1, opIdx) + " has no integer representation");
            } else {
                vm->runtimeError("attempt to perform bitwise operation on " + a.typeToString() + " and " + b.typeToString());
            }
        }
    }
}

void VM::jitBnot(VM* vm, uint32_t nextIp) {
    Value a = vm->pop();
    int64_t ia;
    if (vm->toIntegerNoString(a, ia)) {
        vm->push(vm->makeInteger(~ia));
    } else {
        vm->getFrame(0)->ip = nextIp;
        if (!vm->callBinaryMetamethod(a, a, "__bnot")) {
            if (a.isNumber()) {
                vm->runtimeError("number" + vm->getVarInfo(nextIp - 1, 0) + " has no integer representation");
            } else {
                vm->runtimeError("attempt to perform bitwise operation on " + a.typeToString());
            }
        }
    }
}

void VM::jitShl(VM* vm, uint32_t nextIp) {
    Value b = vm->pop();
    Value a = vm->pop();
    int64_t ia, ib;
    bool okA = vm->toIntegerNoString(a, ia);
    bool okB = vm->toIntegerNoString(b, ib);
    if (okA && okB) {
        vm->push(vm->makeInteger(lua_shift_left(ia, ib)));
    } else {
        vm->getFrame(0)->ip = nextIp;
        if (!vm->callBinaryMetamethod(a, b, "__shl")) {
            if (a.isNumber() && b.isNumber()) {
                int opIdx = !okA ? 0 : 1;
                vm->runtimeError("number" + vm->getVarInfo(nextIp - 1, opIdx) + " has no integer representation");
            } else {
                vm->runtimeError("attempt to perform bitwise operation on " + a.typeToString() + " and " + b.typeToString());
            }
        }
    }
}

void VM::jitShr(VM* vm, uint32_t nextIp) {
    Value b = vm->pop();
    Value a = vm->pop();
    int64_t ia, ib;
    bool okA = vm->toIntegerNoString(a, ia);
    bool okB = vm->toIntegerNoString(b, ib);
    if (okA && okB) {
        vm->push(vm->makeInteger(lua_shift_right(ia, ib)));
    } else {
        vm->getFrame(0)->ip = nextIp;
        if (!vm->callBinaryMetamethod(a, b, "__shr")) {
            if (a.isNumber() && b.isNumber()) {
                int opIdx = !okA ? 0 : 1;
                vm->runtimeError("number" + vm->getVarInfo(nextIp - 1, opIdx) + " has no integer representation");
            } else {
                vm->runtimeError("attempt to perform bitwise operation on " + a.typeToString() + " and " + b.typeToString());
            }
        }
    }
}

void VM::jitEq(VM* vm, uint32_t nextIp) {
    Value b = vm->pop();
    Value a = vm->pop();
    if (a == b) {
        vm->push(Value::boolean(true));
    } else {
        vm->getFrame(0)->ip = nextIp;
        if (!vm->callBinaryMetamethod(a, b, "__eq")) {
            vm->push(Value::boolean(false));
        }
    }
}

void VM::jitLt(VM* vm, uint32_t nextIp) {
    Value b = vm->pop();
    Value a = vm->pop();
    if (a.isNumber() && b.isNumber()) {
        vm->push(vm->less(a, b));
    } else {
        vm->getFrame(0)->ip = nextIp;
        if (!vm->callBinaryMetamethod(a, b, "__lt")) {
            vm->runtimeError("attempt to compare " + a.typeToString() + " and " + b.typeToString());
        }
    }
}

void VM::jitLe(VM* vm, uint32_t nextIp) {
    Value b = vm->pop();
    Value a = vm->pop();
    if (a.isNumber() && b.isNumber()) {
        vm->push(vm->lessEqual(a, b));
    } else {
        vm->getFrame(0)->ip = nextIp;
        if (!vm->callBinaryMetamethod(a, b, "__le")) {
            vm->runtimeError("attempt to compare " + a.typeToString() + " and " + b.typeToString());
        }
    }
}

bool VM::jitForPrep(VM* vm, uint32_t rawBase, uint32_t offset) {
    (void)offset;
    bool stepDefault = (rawBase & 0x80) != 0;
    uint8_t base = rawBase & 0x7F;
    size_t actualBase = vm->currentFrame().stackBase + base;
    if (stepDefault) {
        if (actualBase + 1 >= vm->currentCoroutine_->stack.size()) {
            vm->runtimeError("Invalid stack for numeric for");
            return true;
        }
        vm->push(Value::integer(1));
        vm->push(Value::nil());
    } else {
        if (actualBase + 2 >= vm->currentCoroutine_->stack.size()) {
            vm->runtimeError("Invalid stack for numeric for");
            return true;
        }
        vm->push(Value::nil());
    }
    Value& v_init = vm->currentCoroutine_->stack[actualBase];
    Value& v_limit = vm->currentCoroutine_->stack[actualBase + 1];
    Value& v_step = vm->currentCoroutine_->stack[actualBase + 2];
    Value& v_ext = vm->currentCoroutine_->stack[actualBase + 3];

    auto convertVal = [vm](Value& v, const char* name, int64_t& outI, double& outD, bool& isInt) -> bool {
        if (v.isInteger()) {
            outI = v.asInteger();
            isInt = true;
            return true;
        } else if (v.isFloat()) {
            outD = v.asNumber();
            isInt = false;
            return true;
        } else if (v.isString()) {
            if (vm->stringToNumber(vm->getStringValue(v), outD, outI, isInt)) {
                return true;
            }
        }
        vm->runtimeError(std::string("bad 'for' ") + name + " (number expected, got " + vm->typeName(v) + ")");
        return false;
    };

    int64_t initI = 0, limitI = 0, stepI = 0;
    double initD = 0.0, limitD = 0.0, stepD = 0.0;
    bool initIsInt = false, limitIsInt = false, stepIsInt = false;

    if (!convertVal(v_init, "initial value", initI, initD, initIsInt)) return true;
    if (!convertVal(v_limit, "limit", limitI, limitD, limitIsInt)) return true;
    if (!convertVal(v_step, "step", stepI, stepD, stepIsInt)) return true;

    if ((stepIsInt && stepI == 0) || (!stepIsInt && stepD == 0.0)) {
        vm->runtimeError("'for' step is zero");
        return true;
    }

    if (initIsInt && stepIsInt) {
        bool skipLoop = false;
        if (limitIsInt) {
            if (stepI > 0 ? (initI > limitI) : (initI < limitI)) {
                skipLoop = true;
            }
        } else {
            if (std::isnan(limitD)) {
                skipLoop = true;
            } else if (stepI > 0) {
                if (limitD < static_cast<double>(std::numeric_limits<int64_t>::min())) {
                    skipLoop = true;
                } else if (limitD >= static_cast<double>(std::numeric_limits<int64_t>::max())) {
                    limitI = std::numeric_limits<int64_t>::max();
                    if (initI > limitI) skipLoop = true;
                } else {
                    limitI = static_cast<int64_t>(std::floor(limitD));
                    if (initI > limitI) skipLoop = true;
                }
            } else {
                if (limitD > static_cast<double>(std::numeric_limits<int64_t>::max())) {
                    skipLoop = true;
                } else if (limitD <= static_cast<double>(std::numeric_limits<int64_t>::min())) {
                    limitI = std::numeric_limits<int64_t>::min();
                    if (initI < limitI) skipLoop = true;
                } else {
                    limitI = static_cast<int64_t>(std::ceil(limitD));
                    if (initI < limitI) skipLoop = true;
                }
            }
        }

        if (skipLoop) {
            vm->currentCoroutine_->stack.resize(actualBase);
            return true;
        }

        v_init = vm->makeInteger(initI);
        v_limit = vm->makeInteger(limitI);
        v_step = vm->makeInteger(stepI);
        if (v_init.isObj()) vm->writeBarrierBackward(vm->currentCoroutine_, v_init.asObj());
        if (v_limit.isObj()) vm->writeBarrierBackward(vm->currentCoroutine_, v_limit.asObj());
        if (v_step.isObj()) vm->writeBarrierBackward(vm->currentCoroutine_, v_step.asObj());
        v_ext = v_init;
    } else {
        double initF = initIsInt ? static_cast<double>(initI) : initD;
        double limitF = limitIsInt ? static_cast<double>(limitI) : limitD;
        double stepF = stepIsInt ? static_cast<double>(stepI) : stepD;

        bool skipLoop = false;
        if (std::isnan(initF) || std::isnan(limitF) || std::isnan(stepF)) {
            skipLoop = true;
        } else if (stepF > 0 ? (initF > limitF) : (initF < limitF)) {
            skipLoop = true;
        }

        if (skipLoop) {
            vm->currentCoroutine_->stack.resize(actualBase);
            return true;
        }

        v_init = Value::number(initF);
        v_limit = Value::number(limitF);
        v_step = Value::number(stepF);
        v_ext = v_init;
    }
    return false;
}

bool VM::jitForLoopFallback(VM* vm, uint32_t base) {
    size_t actualBase = vm->currentFrame().stackBase + base;
    if (actualBase + 3 >= vm->currentCoroutine_->stack.size()) {
        vm->runtimeError("Invalid stack for numeric for loop");
        return false;
    }
    Value& v_init = vm->currentCoroutine_->stack[actualBase];
    Value& v_limit = vm->currentCoroutine_->stack[actualBase + 1];
    Value& v_step = vm->currentCoroutine_->stack[actualBase + 2];
    Value& v_ext = vm->currentCoroutine_->stack[actualBase + 3];

    bool canContinue = false;
    if (v_init.isInteger() && v_step.isInteger()) {
        int64_t initI = v_init.asInteger();
        int64_t stepI = v_step.asInteger();
        int64_t limitI = v_limit.asInteger();

        int64_t nextI;
        bool overflow = __builtin_add_overflow(initI, stepI, &nextI);
        if (!overflow) {
            canContinue = (stepI > 0) ? (nextI <= limitI) : (nextI >= limitI);
        }
        if (canContinue) {
            vm->closeUpvalues(actualBase + 3);
            v_init = vm->makeInteger(nextI);
            if (v_init.isObj()) vm->writeBarrierBackward(vm->currentCoroutine_, v_init.asObj());
            v_ext = v_init;
        }
    } else {
        double initF = v_init.asNumber();
        double stepF = v_step.asNumber();
        double limitF = v_limit.asNumber();
        double nextF = initF + stepF;
        canContinue = (stepF > 0) ? (nextF <= limitF) : (nextF >= limitF);
        if (canContinue) {
            vm->closeUpvalues(actualBase + 3);
            v_init = Value::number(nextF);
            v_ext = v_init;
        }
    }
    if (!canContinue) {
        vm->currentCoroutine_->stack.resize(actualBase);
    }
    return canContinue;
}
