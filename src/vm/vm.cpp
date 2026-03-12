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
#include <iostream>
#include <stdarg.h>
#include <algorithm>

VM* VM::currentVM = nullptr;

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
           warnEnabled_(true) {
    currentVM = this;
    for (int i = 0; i < Value::NUM_TYPES; i++) {
        typeMetatables_[i] = Value::nil();
    }
    // Initialize main coroutine
    createCoroutine(nullptr);
    mainCoroutine_ = coroutines_.back();
    mainCoroutine_->status = CoroutineObject::Status::RUNNING;
    currentCoroutine_ = mainCoroutine_;
}

VM::~VM() {
    currentVM = nullptr;
    
    // 1. Clear all handles that could be roots
    globals_.clear();
    registry_.clear();
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
    registry_.clear();
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

    hadError_ = false;
    isHandlingError_ = false;
    inPcall_ = false;
}

size_t VM::registerFunction(FunctionObject* func) {
    size_t index = functions_.size();
    functions_.push_back(func);
    return index;
}

FunctionObject* VM::getFunction(size_t index) {
    if (index >= functions_.size()) {
        runtimeError("Invalid function index");
        return nullptr;
    }
    return functions_[index];
}

StringObject* VM::internString(const char* chars, size_t length) {
    std::string s(chars, length);
    auto it = runtimeStrings_.find(s);
    if (it != runtimeStrings_.end()) {
        return it->second;
    }

    // String size: sizeof(StringObject) + length + 1 (for null terminator)
    size_t stringSize = sizeof(StringObject) + length + 1;
    checkGC(stringSize);

    try {
        StringObject* str = new StringObject(chars, length);
        addObject(str);
        runtimeStrings_[s] = str;
        return str;
    } catch (const std::bad_alloc&) {
        collectGarbage();
        try {
            StringObject* str = new StringObject(chars, length);
            addObject(str);
            runtimeStrings_[s] = str;
            return str;
        } catch (const std::bad_alloc&) {
            runtimeError("not enough memory (hard allocation failure)");
            return nullptr;
        }
    }
}

StringObject* VM::internString(const std::string& str) {
    return internString(str.c_str(), str.length());
}

StringObject* VM::getString(size_t index) {
    if (index >= strings_.size()) {
        runtimeError("Invalid string index");
        return nullptr;
    }
    return strings_[index];
}

TableObject* VM::createTable() {
    return allocateObject<TableObject>();
}

UserdataObject* VM::createUserdata(void* data) {
    return allocateObject<UserdataObject>(data);
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

CoroutineObject* VM::createCoroutine(ClosureObject* closure) {
    CoroutineObject* co = allocateObject<CoroutineObject>();
    coroutines_.push_back(co);

    if (closure) {
        co->stack.push_back(Value::closure(closure));
        
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
    }

    return co;
}

void VM::setupRootUpvalues(ClosureObject* closure, const Value& env) {
    if (closure->upvalueCount() == 0) return;

    UpvalueObject* envUpvalue = nullptr;
    
    if (!env.isNil()) {
        // Use explicit environment
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
    
    for (size_t i = 0; i < closure->upvalueCount(); i++) {
        if (closure->getUpvalueObj(i) == nullptr) {
            closure->setUpvalue(i, envUpvalue);
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

    while (!co->tbcVariables.empty() && co->tbcVariables.back() >= lastStackIndex) {
        size_t index = co->tbcVariables.back();
        co->tbcVariables.pop_back();

        Value val = co->stack[index];
        Value mm = getMetamethod(val, "__close");
        if (!mm.isNil()) {
            push(mm);
            push(val);
            push(error); // Error object
            
            size_t prevFrames = currentCoroutine_->frames.size();
            if (callValue(2, 1)) {
                if (currentCoroutine_->status == CoroutineObject::Status::SUSPENDED) return;
                if (currentCoroutine_->frames.size() > prevFrames) {
                    if (!run(prevFrames)) return;
                    if (currentCoroutine_->status == CoroutineObject::Status::SUSPENDED) return;
                }
            }
        }
    }
}

FileObject* VM::openFile(const std::string& filename, const std::string& mode) {
    return allocateObject<FileObject>(filename, mode);
}

FileObject* VM::createFile(FILE* f, const std::string& mode) {
    return allocateObject<FileObject>(f, mode);
}

FileObject* VM::popen(const std::string& command, const std::string& mode) {
#ifndef _WIN32
    FILE* pipe = ::popen(command.c_str(), mode.c_str());
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
}
SocketObject* VM::createSocket(socket_t fd) {
    return allocateObject<SocketObject>(fd);
}

void VM::closeSocket(SocketObject* socket) {
    if (socket) socket->close();
}

size_t VM::registerNativeFunction(const std::string& /*name*/, NativeFunction func) {
    size_t index = nativeFunctions_.size();
    nativeFunctions_.push_back(func);
    return index;
}

NativeFunction VM::getNativeFunction(size_t index) {
    if (index >= nativeFunctions_.size()) {
        runtimeError("Invalid native function index");
        return nullptr;
    }
    return nativeFunctions_[index];
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
    auto it = globals_.find(name);
    if (it != globals_.end()) {
        return it->second;
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
    for (size_t i = 0; i < function.chunk()->constants().size(); i++) {
        Value& val = const_cast<std::vector<Value>&>(function.chunk()->constants())[i];
        if (val.isString() && !val.isRuntimeString()) {
            StringObject* str = function.chunk()->getString(val.asStringIndex());
            val = Value::runtimeString(internString(str->chars(), str->length()));
            rootedConstants_.push_back(val);
        } else if (val.isFunction()) {
            FunctionObject* nested = function.chunk()->getFunction(val.asFunctionIndex());
            if (nested) internConstants(*nested);
        }
    }
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

#ifdef PRINT_CODE
        function.disassemble();
#endif

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
    inPcall_ = true;
    
    bool success = false;
    try {
        success = callValue(argCount - 1, 0);

        if (success && currentCoroutine_->frames.size() > prevFrames) {
            success = run(prevFrames);
        }
        
        if (hadError_) success = false;
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
    isHandlingError_ = false;
    hadError_ = false;

    if (!success) {
        isHandlingError_ = true;
        Value errorObj = Value::runtimeString(internString(lastErrorMessage_));
        try {
            closeUpvalues(stackSizeBefore, nullptr, errorObj);
        } catch (const RuntimeError& e) {
        }

        while (currentCoroutine_->frames.size() > prevFrames) {
            currentCoroutine_->frames.pop_back();
        }
        
        while (currentCoroutine_->stack.size() > stackSizeBefore) {
            pop();
        }
        
        push(Value::boolean(false));
        push(Value::runtimeString(internString(lastErrorMessage_)));
        hadError_ = false;
        isHandlingError_ = false;
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
    }
    
    return true;
}

bool VM::xpcall(int argCount) {
    if (argCount < 2) {
        runtimeError("xpcall expects at least 2 arguments");
        return false;
    }

    size_t prevFrames = currentCoroutine_->frames.size();
    
    Value msgh = peek(argCount - 2);
    size_t stackSizeBefore = currentCoroutine_->stack.size() - argCount;

    bool prevPcall = inPcall_;
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
    } catch (const RuntimeError& e) {
        isHandlingError_ = false;
        hadError_ = false;
        success = false;
    }

    inPcall_ = prevPcall;

    if (!success || hadError_) {
        isHandlingError_ = true;
        while (currentCoroutine_->frames.size() > prevFrames) {
            currentCoroutine_->frames.pop_back();
        }

        while (currentCoroutine_->stack.size() > stackSizeBefore) {
            pop();
        }

        push(Value::boolean(false));

        push(msgh);
        push(Value::runtimeString(internString(lastErrorMessage_)));

        if (!callValue(1, 2)) {
            isHandlingError_ = false;
            return false;
        }
        
        if (currentCoroutine_->frames.size() > prevFrames) {
            run(prevFrames);
        }
        
        hadError_ = false;
        isHandlingError_ = false;
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
        runtimeError("Stack overflow");
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

Value VM::readConstant() {
    uint8_t index = readByte();
    Value constant = currentFrame().chunk->constants()[index];
    
    if (constant.isString() && !constant.isRuntimeString()) {
        StringObject* str = currentFrame().chunk->getString(constant.asStringIndex());
        StringObject* runtimeStr = internString(str->chars(), str->length());
        return Value::runtimeString(runtimeStr);
    }
    
    return constant;
}

void VM::runtimeError(const std::string& message, int level) {
    if (isHandlingError_) {
        isHandlingError_ = false;
        try {
            runtimeError("(error in __close) " + message, level);
        } catch (const RuntimeError& e) {
            throw e;
        }
    }

    isHandlingError_ = true;
    lastErrorMessage_ = message;
    hadError_ = true;

    int line = -1;
    std::string source = sourceName_;

    int targetFrame = -1;
    if (level > 0 && !currentCoroutine_->frames.empty()) {
        targetFrame = static_cast<int>(currentCoroutine_->frames.size()) - level;
    }

    if (targetFrame >= 0 && targetFrame < static_cast<int>(currentCoroutine_->frames.size())) {
        const CallFrame& frame = currentCoroutine_->frames[targetFrame];
        const Chunk* chunk = frame.chunk;
        if (chunk) {
            source = chunk->sourceName();
            if (frame.ip > 0) {
                line = chunk->getLine(frame.ip - 1);
            }
        }
    }

    std::string prefix = "";
    if (line != -1) {
        std::string displaySource = source;
        if (!displaySource.empty() && displaySource[0] == '@') {
            displaySource = displaySource.substr(1);
        }
        prefix = displaySource + ":" + std::to_string(line) + ": ";
    } else if (level != 0) {
        prefix = source + ": ";
    }

    lastErrorMessage_ = prefix + message;

    if (!inPcall_) {
        std::cerr << lastErrorMessage_ << std::endl;
    }

    throw RuntimeError(lastErrorMessage_);
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

Value VM::add(const Value& a, const Value& b) {
    if (!a.isNumber() || !b.isNumber()) {
        runtimeError("Operands must be numbers");
        return Value::nil();
    }
    if (a.isInteger() && b.isInteger()) {
        return Value::integer(static_cast<int64_t>(static_cast<uint64_t>(a.asInteger()) + static_cast<uint64_t>(b.asInteger())));
    }
    return Value::number(a.asNumber() + b.asNumber());
}

Value VM::subtract(const Value& a, const Value& b) {
    if (!a.isNumber() || !b.isNumber()) {
        runtimeError("Operands must be numbers");
        return Value::nil();
    }
    if (a.isInteger() && b.isInteger()) {
        return Value::integer(static_cast<int64_t>(static_cast<uint64_t>(a.asInteger()) - static_cast<uint64_t>(b.asInteger())));
    }
    return Value::number(a.asNumber() - b.asNumber());
}

Value VM::multiply(const Value& a, const Value& b) {
    if (!a.isNumber() || !b.isNumber()) {
        runtimeError("Operands must be numbers");
        return Value::nil();
    }
    if (a.isInteger() && b.isInteger()) {
        return Value::integer(static_cast<int64_t>(static_cast<uint64_t>(a.asInteger()) * static_cast<uint64_t>(b.asInteger())));
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
    return Value::number(std::floor(a.asNumber() / b.asNumber()));
}

Value VM::modulo(const Value& a, const Value& b) {
    if (!a.isNumber() || !b.isNumber()) {
        runtimeError("Operands must be numbers");
        return Value::nil();
    }
    double da = a.asNumber();
    double db = b.asNumber();
    return Value::number(da - std::floor(da / db) * db);
}

Value VM::power(const Value& a, const Value& b) {
    if (!a.isNumber() || !b.isNumber()) {
        runtimeError("Operands must be numbers");
        return Value::nil();
    }
    return Value::number(std::pow(a.asNumber(), b.asNumber()));
}

Value VM::bitwiseAnd(const Value& a, const Value& b) {
    if (!a.isNumber() || !b.isNumber()) {
        runtimeError("Operands must be numbers");
        return Value::nil();
    }
    return Value::integer(a.asInteger() & b.asInteger());
}

Value VM::bitwiseOr(const Value& a, const Value& b) {
    if (!a.isNumber() || !b.isNumber()) {
        runtimeError("Operands must be numbers");
        return Value::nil();
    }
    return Value::integer(a.asInteger() | b.asInteger());
}

Value VM::bitwiseXor(const Value& a, const Value& b) {
    if (!a.isNumber() || !b.isNumber()) {
        runtimeError("Operands must be numbers");
        return Value::nil();
    }
    return Value::integer(a.asInteger() ^ b.asInteger());
}

Value VM::shiftLeft(const Value& a, const Value& b) {
    if (!a.isNumber() || !b.isNumber()) {
        runtimeError("Operands must be numbers");
        return Value::nil();
    }
    return Value::integer(a.asInteger() << b.asInteger());
}

Value VM::shiftRight(const Value& a, const Value& b) {
    if (!a.isNumber() || !b.isNumber()) {
        runtimeError("Operands must be numbers");
        return Value::nil();
    }
    return Value::integer(a.asInteger() >> b.asInteger());
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
        return Value::integer(static_cast<int64_t>(0ULL - static_cast<uint64_t>(a.asInteger())));
    }
    return Value::number(-a.asNumber());
}

Value VM::bitwiseNot(const Value& a) {
    if (!a.isNumber()) {
        runtimeError("attempt to perform bitwise operation on non-number");
        return Value::nil();
    }
    return Value::integer(static_cast<int64_t>(~static_cast<uint64_t>(a.asInteger())));
}

Value VM::equal(const Value& a, const Value& b) {
    return Value::boolean(a == b);
}

Value VM::less(const Value& a, const Value& b) {
    if (!a.isNumber() || !b.isNumber()) {
        runtimeError("Operands must be numbers");
        return Value::nil();
    }
    return Value::boolean(a.asNumber() < b.asNumber());
}

Value VM::lessEqual(const Value& a, const Value& b) {
    if (!a.isNumber() || !b.isNumber()) {
        runtimeError("Operands must be numbers");
        return Value::nil();
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

bool VM::callBinaryMetamethod(const Value& a, const Value& b, const std::string& method) {
    Value mm = getMetamethod(a, method);
    if (mm.isNil()) {
        mm = getMetamethod(b, method);
    }

    if (mm.isNil()) return false;

    push(mm);
    push(a);
    push(b);
    return callValue(2, 2);
}

bool VM::callValue(int argCount, int retCount, bool isTailCall) {
    Value callee = peek(argCount);
    
    if (callee.isNativeFunction() || callee.isCFunction()) {
        size_t funcPosition = currentCoroutine_->stack.size() - argCount - 1;
        currentCoroutine_->lastResultCount = 1;

        if (callee.isNativeFunction()) {
            NativeFunction function = nativeFunctions_[callee.asNativeFunctionIndex()];
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
            
            lua_State* oldL = currentL_;
            currentL_ = &L;
            int nres = function(&L);
            currentL_ = oldL;
            
            currentCoroutine_->lastResultCount = nres;
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

        while (currentCoroutine_->stack.size() > funcPosition) {
            pop();
        }

        if (retCount > 0 && currentCoroutine_->status != CoroutineObject::Status::SUSPENDED) {
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
            return true;
        }

        CallFrame frame;
        frame.closure = closure;
        frame.chunk = function->chunk();
        frame.ip = 0;
        frame.stackBase = currentCoroutine_->stack.size() - argCount;
        frame.retCount = retCount;
        frame.callerChunk = currentCoroutine_->frames.empty() ? nullptr : currentFrame().chunk;
        
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
        size_t calleePos = currentCoroutine_->stack.size() - argCount - 1;
        currentCoroutine_->stack.insert(currentCoroutine_->stack.begin() + calleePos, mm);
        return callValue(argCount + 1, retCount, isTailCall);
    }
    
    runtimeError("attempt to call a " + callee.typeToString() + " value");
    return false;
}

bool VM::resumeCoroutine(CoroutineObject* co) {
    if (co->status == CoroutineObject::Status::DEAD) {
        runtimeError("cannot resume dead coroutine");
        return false;
    }

    CoroutineObject* oldCo = currentCoroutine_;
    currentCoroutine_ = co;
    co->status = CoroutineObject::Status::RUNNING;
    co->caller = oldCo;

    bool result = run(0);

    currentCoroutine_ = oldCo;

    if (result) {
        if (co->status == CoroutineObject::Status::SUSPENDED) {
            for (const auto& val : co->yieldedValues) {
                push(val);
            }
            currentCoroutine_->lastResultCount = co->yieldedValues.size();
        } else if (co->status == CoroutineObject::Status::DEAD) {
            size_t count = co->stack.size();
            for (const auto& val : co->stack) {
                push(val);
            }
            currentCoroutine_->lastResultCount = count;
            co->stack.clear();
        }
    }

    return result;
}

Value VM::getRegistry(const std::string& key) const {
    auto it = registry_.find(key);
    if (it != registry_.end()) {
        return it->second;
    }
    return Value::nil();
}

void VM::setTypeMetatable(Value::Type type, const Value& mt) {
    int index = static_cast<int>(type) & 0x0F;
    if (index >= 0 && index < Value::NUM_TYPES) {
        typeMetatables_[index] = mt;
    }
}

Value VM::getTypeMetatable(Value::Type type) const {
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

void VM::callHook(const char* event, int line) {
    if (currentCoroutine_->inHook) return;
    currentCoroutine_->inHook = true;
    
    if (currentCoroutine_->hook.isFunction()) {
        push(currentCoroutine_->hook);
        push(Value::runtimeString(internString(event)));
        if (line != -1) {
            push(Value::number(static_cast<double>(line)));
        } else {
            push(Value::nil());
        }
        
        callValue(2, 1);
        run(currentCoroutine_->frames.size() - 1);
    }
    
    currentCoroutine_->inHook = false;
}

void VM::jitGetTable(VM* vm, uint32_t nextIp) {
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
            vm->runtimeError("attempt to index a " + tableValue.typeToString() + " value");
        }
        vm->push(Value::nil());
    } else {
        vm->getFrame(0)->ip = nextIp;
        if (indexMethod.isFunction()) {
            vm->push(indexMethod);
            vm->push(tableValue);
            vm->push(key);
            vm->callValue(2, 2); 
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
    Value value = vm->peek(0);
    Value key = vm->peek(1);
    Value tableValue = vm->peek(2);

    if (tableValue.isTable()) {
        TableObject* table = tableValue.asTableObj();
        if (table->has(key)) {
            table->set(key, value);
            vm->pop(); vm->pop(); vm->pop();
            return;
        }
    }

    Value newIndex = vm->getMetamethod(tableValue, "__newindex");
    if (newIndex.isNil()) {
        if (tableValue.isTable()) {
            tableValue.asTableObj()->set(key, value);
        } else {
            vm->runtimeError("attempt to index a " + tableValue.typeToString() + " value");
        }
        vm->pop(); vm->pop(); vm->pop();
    } else {
        vm->getFrame(0)->ip = nextIp;
        if (newIndex.isFunction()) {
            vm->currentCoroutine_->stack.insert(vm->currentCoroutine_->stack.end() - 3, newIndex);
            vm->callValue(3, 1);
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
                vm->runtimeError("attempt to index a " + tableValue.typeToString() + " value");
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
            vm->runtimeError("attempt to concatenate " + a.typeToString() + " and " + b.typeToString());
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
    vm->getFrame(0)->ip = nextIp;
    vm->push(vm->bitwiseAnd(a, b));
}

void VM::jitBor(VM* vm, uint32_t nextIp) {
    Value b = vm->pop();
    Value a = vm->pop();
    vm->getFrame(0)->ip = nextIp;
    vm->push(vm->bitwiseOr(a, b));
}

void VM::jitBxor(VM* vm, uint32_t nextIp) {
    Value b = vm->pop();
    Value a = vm->pop();
    vm->getFrame(0)->ip = nextIp;
    vm->push(vm->bitwiseXor(a, b));
}

void VM::jitBnot(VM* vm, uint32_t nextIp) {
    Value a = vm->pop();
    vm->getFrame(0)->ip = nextIp;
    vm->push(vm->bitwiseNot(a));
}

void VM::jitShl(VM* vm, uint32_t nextIp) {
    Value b = vm->pop();
    Value a = vm->pop();
    vm->getFrame(0)->ip = nextIp;
    vm->push(vm->shiftLeft(a, b));
}

void VM::jitShr(VM* vm, uint32_t nextIp) {
    Value b = vm->pop();
    Value a = vm->pop();
    vm->getFrame(0)->ip = nextIp;
    vm->push(vm->shiftRight(a, b));
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
