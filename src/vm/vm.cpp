#include "vm/vm.hpp"
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
           gcState_(GCState::PAUSE),
           gcObjects_(nullptr), bytesAllocated_(0), nextGC_(1024 * 1024), 
           memoryLimit_(100 * 1024 * 1024), // Default 100MB limit
           gcEnabled_(true) {
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
        printf("DEBUG getString FAIL: index=%zu size=%zu\n", index, strings_.size());
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
    // Closure size is variable: sizeof(ClosureObject) + upvalueCount * sizeof(UpvalueObject*)
    // But our allocateObject only checks sizeof(T). 
    // For simplicity, let's manually handle it if it's variable.
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

void VM::setupRootUpvalues(ClosureObject* closure) {
    if (closure->upvalueCount() == 0) return;

    UpvalueObject* envUpvalue = nullptr;
    
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
    
    // Set all upvalues to envUpvalue initially to avoid nulls
    // though the compiler should only use index 0 for _ENV
    for (size_t i = 0; i < closure->upvalueCount(); i++) {
        if (closure->getUpvalueObj(i) == nullptr) {
            closure->setUpvalue(i, envUpvalue);
        }
    }
}

UpvalueObject* VM::captureUpvalue(size_t stackIndex) {
    // Check if upvalue already exists for this stack slot
    for (UpvalueObject* openUpvalue : currentCoroutine_->openUpvalues) {
        if (!openUpvalue->isClosed() && openUpvalue->stackIndex() == stackIndex) {
            return openUpvalue;
        }
    }

    UpvalueObject* upvalue = allocateObject<UpvalueObject>(currentCoroutine_, stackIndex);

    // Insert into currentCoroutine_->openUpvalues (keep sorted by stack index for efficient closing)
    auto it = currentCoroutine_->openUpvalues.begin();
    while (it != currentCoroutine_->openUpvalues.end() && (*it)->stackIndex() < stackIndex) {
        ++it;
    }
    currentCoroutine_->openUpvalues.insert(it, upvalue);

    return upvalue;
}

void VM::closeUpvalues(size_t lastStackIndex) {
    auto it = currentCoroutine_->openUpvalues.begin();
    while (it != currentCoroutine_->openUpvalues.end()) {
        UpvalueObject* upvalue = *it;
        if (!upvalue->isClosed() && upvalue->stackIndex() >= lastStackIndex) {
            upvalue->close(currentCoroutine_->stack);
            it = currentCoroutine_->openUpvalues.erase(it);
        } else {
            ++it;
        }
    }
}

FileObject* VM::openFile(const std::string& filename, const std::string& mode) {
    return allocateObject<FileObject>(filename, mode);
}

void VM::closeFile(FileObject* file) {
    if (file) file->close();
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

    // Register _G (global environment) early so libraries can populate it
    TableObject* gTable = createTable();
    globals_["_G"] = Value::table(gTable);

    // Register base library
    extern void registerBaseLibrary(VM* vm);
    registerBaseLibrary(this);

    // Create 'math' table
    TableObject* mathTable = createTable();
    extern void registerMathLibrary(VM* vm, TableObject* mathTable);
    registerMathLibrary(this, mathTable);
    setGlobal("math", Value::table(mathTable));

    // Create 'string' table
    TableObject* stringTable = createTable();
    extern void registerStringLibrary(VM* vm, TableObject* stringTable);
    registerStringLibrary(this, stringTable);
    setGlobal("string", Value::table(stringTable));

    // Create 'table' table
    TableObject* tableTable = createTable();
    extern void registerTableLibrary(VM* vm, TableObject* tableTable);
    registerTableLibrary(this, tableTable);
    setGlobal("table", Value::table(tableTable));

    // Create 'os' table
    TableObject* osTable = createTable();
    extern void registerOSLibrary(VM* vm, TableObject* osTable);
    registerOSLibrary(this, osTable);
    setGlobal("os", Value::table(osTable));

    // Create 'io' table
    TableObject* ioTable = createTable();
    extern void registerIOLibrary(VM* vm, TableObject* ioTable);
    registerIOLibrary(this, ioTable);
    setGlobal("io", Value::table(ioTable));

    // Create 'socket' table
    TableObject* socketTable = createTable();
    extern void registerSocketLibrary(VM* vm, TableObject* socketTable);
    registerSocketLibrary(this, socketTable);
    setGlobal("socket", Value::table(socketTable));

    // Create 'coroutine' table
    TableObject* coroutineTable = createTable();
    extern void registerCoroutineLibrary(VM* vm, TableObject* coroutineTable);
    registerCoroutineLibrary(this, coroutineTable);
    setGlobal("coroutine", Value::table(coroutineTable));

    // Create 'debug' table
    TableObject* debugTable = createTable();
    extern void registerDebugLibrary(VM* vm, TableObject* debugTable);
    registerDebugLibrary(this, debugTable);
    setGlobal("debug", Value::table(debugTable));
}

void VM::runInitializationFrames() {
    // (This might be called if stdlib registration pushed Lua functions to coroutine)
    if (currentCoroutine_->frames.size() > 0) {
        if (run(currentCoroutine_->frames.size() - 1)) {
            // Success, pop results left by the initialization script
            for (size_t i = 0; i < currentCoroutine_->lastResultCount; i++) {
                pop();
            }
        }
    }
}

void VM::setGlobal(const std::string& name, const Value& value) {
    globals_[name] = value;
    
    // Also update _G table if it exists
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

bool VM::run(const FunctionObject& function) {
    return run(function, {});
}

bool VM::run(const FunctionObject& function, const std::vector<Value>& args) {
    try {
        // Save current state for recursive calls
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

        // Initialize standard library on first run (needs chunk for string pool)
        if (!stdlibInitialized_) {
            initStandardLibrary();
        }

#ifdef PRINT_CODE
        function.chunk()->disassemble(function.name());
#endif

        // Create root closure
        ClosureObject* closure = createClosure(const_cast<FunctionObject*>(&function));
        
        // Clear frames for a clean start if this is the first call AND stack is empty
        if (currentCoroutine_->frames.empty() && currentCoroutine_->stack.empty()) {
            currentCoroutine_->stack.clear();
        }
        
        // Initialize root upvalues (the first one is _ENV)
        setupRootUpvalues(closure);

        push(Value::closure(closure));
        
        // Push arguments
        for (const auto& arg : args) {
            push(arg);
        }

        // Use callValue to push the frame correctly
        if (!callValue(static_cast<int>(args.size()), 1)) {
            return false;
        }

        if (currentCoroutine_->hookMask & CoroutineObject::MASK_CALL) {
            callHook("call");
        }

        bool result = run(oldFrameCount);

        if (currentCoroutine_->status == CoroutineObject::Status::SUSPENDED) {
            // Coroutine yielded - do NOT restore state or pop frames!
            // The state will be restored when resume() finishes and returns to resumer.
            return result;
        }

        // Restore previous state
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
    size_t stackSizeBefore = currentCoroutine_->stack.size() - argCount; // Leave pcall on stack

    bool prevPcall = inPcall_;
    inPcall_ = true;
    
    bool success = false;
    try {
        // callValue pops (argCount - 1) and the function, and sets up a new frame if closure
        success = callValue(argCount - 1, 0); // 0 means return all results

        // If it was a closure, a new frame was pushed. We need to run it until it pops.
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
        success = false;
        isHandlingError_ = true; // Prevent GC/memory errors while unwinding and allocating error string
        // Runtime error occurred during execution
        // Stack and frames need to be unwound
        while (currentCoroutine_->frames.size() > prevFrames) {
            currentCoroutine_->frames.pop_back();
        }
        
        while (currentCoroutine_->stack.size() > stackSizeBefore) {
            pop();
        }
        
        push(Value::boolean(false));
        push(Value::runtimeString(internString(lastErrorMessage_)));
        hadError_ = false; // Recovered
        isHandlingError_ = false;
        currentCoroutine_->lastResultCount = 2;
    } else {
        // Success. The results are on the stack.
        // We need to push `true` before the results.
        size_t resultCount = currentCoroutine_->lastResultCount;
        
        std::vector<Value> results;
        for (size_t i = 0; i < resultCount; i++) {
            results.push_back(pop());
        }
        std::reverse(results.begin(), results.end());
        
        // Pop function and arguments
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
    
    // Save error handler
    Value msgh = peek(argCount - 2);
    size_t stackSizeBefore = currentCoroutine_->stack.size() - argCount; // Leave xpcall on stack

    bool prevPcall = inPcall_;
    inPcall_ = true;

    // Shift arguments to call f: [f, arg1, arg2, ...]
    std::vector<Value> args;
    for (int i = 0; i < argCount - 2; i++) {
        args.push_back(pop());
    }
    pop(); // pop msgh
    for (auto it = args.rbegin(); it != args.rend(); ++it) {
        push(*it);
    }

    // Now stack is: [..., f, arg1, arg2, ...]
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
        isHandlingError_ = true; // Prevent GC/memory errors while unwinding and allocating error string
        // Runtime error occurred during execution
        // Stack and frames need to be unwound
        while (currentCoroutine_->frames.size() > prevFrames) {
            currentCoroutine_->frames.pop_back();
        }

        while (currentCoroutine_->stack.size() > stackSizeBefore) {
            pop();
        }

        // Push false, then call msgh(lastErrorMessage_)
        push(Value::boolean(false));

        // Push msgh and call it
        push(msgh);
        push(Value::runtimeString(internString(lastErrorMessage_)));

        if (!callValue(1, 2)) {
            // msgh failed!
            isHandlingError_ = false;
            return false;
        }
        
        // Wait, msgh might be Lua function.
        if (currentCoroutine_->frames.size() > prevFrames) {
            run(prevFrames);
        }
        
        // Result of msgh is now on top. 
        hadError_ = false; // Recovered
        isHandlingError_ = false;
        currentCoroutine_->lastResultCount = 2;
    } else {
        // Success. The results are on the stack.
        size_t resultCount = currentCoroutine_->lastResultCount;
        
        std::vector<Value> results;
        for (size_t i = 0; i < resultCount; i++) {
            results.push_back(pop());
        }
        std::reverse(results.begin(), results.end());
        
        // Pop function and arguments
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
    // std::cout << "DEBUG pop from " << currentCoroutine_ << " size now " << currentCoroutine_->stack.size() << std::endl;
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
    
    // Runtime strings are stored in the constant pool as indices into the strings_ pool
    // and need to be interned properly during execution if they are used as keys or by identity
    if (constant.isString() && !constant.isRuntimeString()) {
        StringObject* str = currentFrame().chunk->getString(constant.asStringIndex());
        StringObject* runtimeStr = internString(str->chars(), str->length());
        return Value::runtimeString(runtimeStr);
    }
    
    return constant;
}

void VM::runtimeError(const std::string& message) {
    if (isHandlingError_) {
        // Nested error during error handling - this is bad.
        // Just throw without more printing.
        throw RuntimeError(message);
    }

    isHandlingError_ = true;
    lastErrorMessage_ = message;
    hadError_ = true;

    if (!inPcall_) {
        // Get line number from current instruction
        int line = -1;
        if (!currentCoroutine_->frames.empty()) {
            const Chunk* chunk = currentFrame().chunk;
            if (chunk && currentFrame().ip > 0) {
                line = chunk->getLine(currentFrame().ip - 1);
            }
        }

        if (line != -1) {
            std::cout << "RUNTIME ERROR at line " << line << ": " << message << std::endl;
        } else {
            std::cout << "RUNTIME ERROR: " << message << std::endl;
        }
    }

    throw RuntimeError(message);
}

void VM::traceExecution() {
    // Print stack contents
    std::cout << "          ";
    for (const Value& value : currentCoroutine_->stack) {
        std::cout << "[ " << value << " ]";
    }
    std::cout << std::endl;

    const Chunk* chunk = currentCoroutine_->frames.empty() ? currentCoroutine_->chunk : currentFrame().chunk;
    chunk->disassembleInstruction(currentFrame().ip);
}

Value VM::add(const Value& a, const Value& b) {
    if (!a.isNumber() || !b.isNumber()) {
        runtimeError("Operands must be numbers");
        return Value::nil();
    }
    if (a.isInteger() && b.isInteger()) {
        return Value::integer(a.asInteger() + b.asInteger());
    }
    return Value::number(a.asNumber() + b.asNumber());
}

Value VM::subtract(const Value& a, const Value& b) {
    if (!a.isNumber() || !b.isNumber()) {
        runtimeError("Operands must be numbers");
        return Value::nil();
    }
    if (a.isInteger() && b.isInteger()) {
        return Value::integer(a.asInteger() - b.asInteger());
    }
    return Value::number(a.asNumber() - b.asNumber());
}

Value VM::multiply(const Value& a, const Value& b) {
    if (!a.isNumber() || !b.isNumber()) {
        runtimeError("Operands must be numbers");
        return Value::nil();
    }
    if (a.isInteger() && b.isInteger()) {
        return Value::integer(a.asInteger() * b.asInteger());
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
        return Value::integer(-a.asInteger());
    }
    return Value::number(-a.asNumber());
}

Value VM::bitwiseNot(const Value& a) {
    if (!a.isNumber()) {
        runtimeError("attempt to perform bitwise operation on non-number");
        return Value::nil();
    }
    return Value::integer(~a.asInteger());
}

Value VM::equal(const Value& a, const Value& b) {
    return Value::boolean(a.equals(b));
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
        return str ? str->chars() : "";
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
    return callValue(2, 2); // 1 result expected
}

bool VM::callValue(int argCount, int retCount, bool isTailCall) {
    Value callee = peek(argCount);
    
    if (callee.isNativeFunction()) {
        NativeFunction function = nativeFunctions_[callee.asNativeFunctionIndex()];
        
        size_t funcPosition = currentCoroutine_->stack.size() - argCount - 1;
        
        if (!function(this, argCount)) {
            return false;
        }

        size_t resultCount = currentCoroutine_->stack.size() - funcPosition - 1;
        std::vector<Value> results;
        results.reserve(resultCount);
        for (size_t i = 0; i < resultCount; i++) {
            results.push_back(pop());
        }
        std::reverse(results.begin(), results.end());

        while (currentCoroutine_->stack.size() > funcPosition) {
            pop();
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
            // Pop the varargs from the stack so they don't occupy local variable slots
            for (int i = 0; i < argCount - arity; i++) {
                currentCoroutine_->stack.pop_back();
            }
        } else if (argCount > arity && !function->hasVarargs()) {
            // Discard extra arguments
            for (int i = 0; i < argCount - arity; i++) {
                currentCoroutine_->stack.pop_back();
            }
        }
        
        currentCoroutine_->frames.push_back(std::move(frame));
        currentCoroutine_->chunk = function->chunk();
        return true;
    }
    
    // Check for __call metamethod
    Value mm = getMetamethod(callee, "__call");
    if (!mm.isNil()) {
        // We need to insert the metamethod right before the callee (which becomes the first argument)
        // Stack currently: [..., callee, arg1, arg2, ..., argN]
        // We need: [..., mm, callee, arg1, arg2, ..., argN]
        size_t calleePos = currentCoroutine_->stack.size() - argCount - 1;
        currentCoroutine_->stack.insert(currentCoroutine_->stack.begin() + calleePos, mm);
        
        // Now call the metamethod with argCount + 1 arguments
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

    bool result = run(oldCo->frames.size());

    currentCoroutine_ = oldCo;
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
    int index = static_cast<int>(type);
    if (index >= 0 && index < Value::NUM_TYPES) {
        typeMetatables_[index] = mt;
    }
}

Value VM::getTypeMetatable(Value::Type type) const {
    int index = static_cast<int>(type);
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

