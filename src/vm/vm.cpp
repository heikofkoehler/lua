#include "vm/vm.hpp"
#include "compiler/lexer.hpp"
#include "compiler/parser.hpp"
#include "compiler/codegen.hpp"
#include "value/userdata.hpp"
#include <iostream>
#include <cmath>

VM* VM::currentVM = nullptr;

// Forward declarations for stdlib registration
void registerStringLibrary(VM* vm, TableObject* stringTable);
void registerTableLibrary(VM* vm, TableObject* tableTable);
void registerMathLibrary(VM* vm, TableObject* mathTable);
void registerSocketLibrary(VM* vm, TableObject* socketTable);
void registerCoroutineLibrary(VM* vm, TableObject* coroutineTable);
void registerOSLibrary(VM* vm, TableObject* osTable);
void registerIOLibrary(VM* vm, TableObject* ioTable);
void registerDebugLibrary(VM* vm, TableObject* debugTable);
void registerBaseLibrary(VM* vm);

VM::VM() : 
#ifdef TRACE_EXECUTION
           traceExecution_(true), 
#else
           traceExecution_(false), 
#endif
           mainCoroutine_(nullptr), currentCoroutine_(nullptr), 
           hadError_(false), inPcall_(false), isHandlingError_(false), lastErrorMessage_(""), stdlibInitialized_(false),
           gcObjects_(nullptr), bytesAllocated_(0), nextGC_(1024 * 1024), 
           memoryLimit_(100 * 1024 * 1024), // Default 100MB limit
           gcEnabled_(true) {
    currentVM = this;
    for (int i = 0; i < Value::NUM_TYPES; i++) {
        typeMetatables_[i] = Value::nil();
    }
    // Create main coroutine
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

size_t VM::registerNativeFunction(const std::string& /* name */, NativeFunction func) {
    nativeFunctions_.push_back(func);
    return nativeFunctions_.size() - 1;
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

void VM::runInitializationFrames() {
    size_t baseFrames = currentCoroutine_->frames.size();
    while (currentCoroutine_->frames.size() > baseFrames) {
        // Run until this specific initialization frame returns
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

void VM::initStandardLibrary() {
    if (stdlibInitialized_) return;
    stdlibInitialized_ = true;

    // Register _G (global environment) early so libraries can populate it
    TableObject* gTable = createTable();
    globals_["_G"] = Value::table(gTable);

    // Register base library
    registerBaseLibrary(this);

    // Create 'string' table
    TableObject* stringTable = createTable();
    setGlobal("string", Value::table(stringTable));

    // Create 'table' table
    TableObject* tableTable = createTable();
    setGlobal("table", Value::table(tableTable));
    registerTableLibrary(this, tableTable);

    registerStringLibrary(this, stringTable);

    // Create 'math' table
    TableObject* mathTable = createTable();
    registerMathLibrary(this, mathTable);
    setGlobal("math", Value::table(mathTable));

    // Create 'os' table
    TableObject* osTable = createTable();
    registerOSLibrary(this, osTable);
    setGlobal("os", Value::table(osTable));

    // Create 'io' table
    TableObject* ioTable = createTable();
    void registerIOLibrary(VM* vm, TableObject* ioTable);
    registerIOLibrary(this, ioTable);
    setGlobal("io", Value::table(ioTable));

    // Create 'socket' table
    TableObject* socketTable = createTable();
    registerSocketLibrary(this, socketTable);
    setGlobal("socket", Value::table(socketTable));

    // Create 'coroutine' table
    TableObject* coroutineTable = createTable();
    registerCoroutineLibrary(this, coroutineTable);
    setGlobal("coroutine", Value::table(coroutineTable));

    // Create 'debug' table
    TableObject* debugTable = createTable();
    void registerDebugLibrary(VM* vm, TableObject* debugTable);
    registerDebugLibrary(this, debugTable);
    setGlobal("debug", Value::table(debugTable));

    // Run all initialization scripts registered during library loading
    runInitializationFrames();
}

CallFrame* VM::getFrame(int level) {
    if (level < 1 || static_cast<size_t>(level) > currentCoroutine_->frames.size()) {
        return nullptr;
    }
    // level 1 is the most recent frame (top of frames vector)
    return &currentCoroutine_->frames[currentCoroutine_->frames.size() - level];
}

void VM::callHook(const char* event, int line) {
    if (currentCoroutine_->inHook || currentCoroutine_->hook.isNil()) return;
    
    currentCoroutine_->inHook = true;
    
    // Push hook function and arguments
    push(currentCoroutine_->hook);
    push(Value::runtimeString(internString(event)));
    if (line != -1) {
        push(Value::number(line));
    } else {
        push(Value::nil());
    }
    
    size_t prevFrames = currentCoroutine_->frames.size();
    // Call hook function.
    if (callValue(2, 1)) {
        if (currentCoroutine_->frames.size() > prevFrames) {
            // Lua hook: execute until it returns to current frame level
            run(prevFrames);
        } else {
            // Native hook: already finished, pop the single nil result
            pop();
        }
    }
    
    currentCoroutine_->inHook = false;
}

CallFrame& VM::currentFrame() {
    if (currentCoroutine_->frames.empty()) {
        throw RuntimeError("No active call frame");
    }
    return currentCoroutine_->frames.back();
}

const CallFrame& VM::currentFrame() const {
    if (currentCoroutine_->frames.empty()) {
        throw RuntimeError("No active call frame");
    }
    return currentCoroutine_->frames.back();
}

bool VM::run(const FunctionObject& function) {
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
        
        // Clear frames for a clean start if this is the first call
        if (currentCoroutine_->frames.empty()) {
            currentCoroutine_->stack.clear();
        }
        
        // Initialize root upvalues (the first one is _ENV)
        if (closure->upvalueCount() > 0) {
            UpvalueObject* envUpvalue = nullptr;
            
            // Try to inherit _ENV from current frame if we're called from Lua
            if (!currentCoroutine_->frames.empty()) {
                envUpvalue = currentFrame().closure->getUpvalueObj(0); // Assumes _ENV is always upvalue 0
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
                
                envUpvalue = new UpvalueObject(gTable);
                addObject(envUpvalue);
            }
            
            closure->setUpvalue(0, envUpvalue);
        }

        push(Value::closure(closure));
        
        // Use callValue to push the frame correctly
        if (!callValue(0, 1)) {
            return false;
        }

        if (currentCoroutine_->hookMask & CoroutineObject::MASK_CALL) {
            callHook("call");
        }

        bool result = run();

        if (currentCoroutine_->status == CoroutineObject::Status::SUSPENDED) {
            // Coroutine yielded - do NOT restore state or pop frames!
            // The state will be restored when resume() finishes and returns to resumer.
            return result;
        }

        // Restore previous state
        currentCoroutine_->chunk = oldChunk;
        currentCoroutine_->rootChunk = oldRoot;
        
        // Clean up frames from this call (if any left)
        while (currentCoroutine_->frames.size() > oldFrameCount) {
            currentCoroutine_->frames.pop_back();
        }

        return result;
    } catch (const RuntimeError& e) {
        isHandlingError_ = false;
        return false;
    }
}

bool VM::run() {
    return run(0);
}

bool VM::run(size_t targetFrameCount) {
    // Main execution loop
    while (true) {
        // Handle debug hooks
        if (stdlibInitialized_ && !currentCoroutine_->inHook && currentCoroutine_->hookMask != 0) {
            bool triggerCount = false;
            bool triggerLine = false;
            int currentLine = -1;

            if (currentCoroutine_->hookMask & CoroutineObject::MASK_COUNT) {
                if (--currentCoroutine_->hookCount <= 0) {
                    triggerCount = true;
                    currentCoroutine_->hookCount = currentCoroutine_->baseHookCount;
                }
            }

            if (currentCoroutine_->hookMask & CoroutineObject::MASK_LINE) {
                if (!currentCoroutine_->frames.empty()) {
                    currentLine = currentFrame().chunk->getLine(currentFrame().ip);
                    if (currentLine != currentCoroutine_->lastLine) {
                        triggerLine = true;
                        currentCoroutine_->lastLine = currentLine;
                    }
                }
            }

            if (triggerCount) callHook("count");
            if (hadError_) return false;
            
            if (triggerLine) callHook("line", currentLine);
            if (hadError_) return false;
        }

        if (traceExecution_) {
            traceExecution();
        }
        
        uint8_t instruction = readByte();
        OpCode op = static_cast<OpCode>(instruction);

        switch (op) {
            case OpCode::OP_CONSTANT: {
                Value constant = readConstant();
                push(constant);
                break;
            }

            case OpCode::OP_NIL:
                push(Value::nil());
                break;

            case OpCode::OP_TRUE:
                push(Value::boolean(true));
                break;

            case OpCode::OP_FALSE:
                push(Value::boolean(false));
                break;

            case OpCode::OP_GET_GLOBAL: {
                uint8_t nameIndex = readByte();
                const std::string& varName = currentFrame().chunk->getIdentifier(nameIndex);
                auto it = globals_.find(varName);
                if (it == globals_.end()) {
                    runtimeError("Undefined variable '" + varName + "'");
                    push(Value::nil());
                } else {
                    push(it->second);
                }
                break;
            }

            case OpCode::OP_SET_GLOBAL: {
                uint8_t nameIndex = readByte();
                const std::string& varName = currentFrame().chunk->getIdentifier(nameIndex);
                globals_[varName] = peek(0);
                break;
            }

            case OpCode::OP_GET_LOCAL: {
                uint8_t slot = readByte();
                // Add stackBase offset if inside a function
                size_t actualSlot = currentCoroutine_->frames.empty() ? slot : (currentFrame().stackBase + slot);
                push(currentCoroutine_->stack[actualSlot]);
                break;
            }

            case OpCode::OP_SET_LOCAL: {
                uint8_t slot = readByte();
                // Add stackBase offset if inside a function
                size_t actualSlot = currentCoroutine_->frames.empty() ? slot : (currentFrame().stackBase + slot);
#ifdef DEBUG
                std::cout << "DEBUG SET_LOCAL: slot=" << (int)slot << " actual=" << actualSlot << " val=" << peek(0) << std::endl;
#endif
                currentCoroutine_->stack[actualSlot] = peek(0);
                break;
            }

            case OpCode::OP_GET_UPVALUE: {
                uint8_t upvalueIndex = readByte();
                if (!currentCoroutine_->frames.empty()) {
                    UpvalueObject* upvalue = currentFrame().closure->getUpvalueObj(upvalueIndex);
                    if (upvalue) {
                        push(upvalue->get(currentCoroutine_->stack));
                    } else {
                        push(Value::nil());
                    }
                } else {
                    runtimeError("Upvalue access outside of closure");
                    push(Value::nil());
                }
                break;
            }

            case OpCode::OP_SET_UPVALUE: {
                uint8_t upvalueIndex = readByte();
                if (!currentCoroutine_->frames.empty()) {
                    UpvalueObject* upvalue = currentFrame().closure->getUpvalueObj(upvalueIndex);
                    if (upvalue) {
                        upvalue->set(currentCoroutine_->stack, peek(0));
                    }
                } else {
                    runtimeError("Upvalue access outside of closure");
                }
                break;
            }

            case OpCode::OP_GET_TABUP: {
                uint8_t upIndex = readByte();
                Value key = readConstant();
                UpvalueObject* upvalue = currentFrame().closure->getUpvalueObj(upIndex);
                Value upTable = upvalue->get(currentCoroutine_->stack);

                if (upTable.isTable()) {
                    push(upTable.asTableObj()->get(key));
                } else {
                    runtimeError("attempt to index a " + upTable.typeToString() + " value");
                }
                break;
            }
            case OpCode::OP_SET_TABUP: {
                uint8_t upIndex = readByte();
                Value key = readConstant();
                Value value = pop();
                UpvalueObject* upvalue = currentFrame().closure->getUpvalueObj(upIndex);
                Value upTable = upvalue->get(currentCoroutine_->stack);
                
                if (upTable.isTable()) {
                    upTable.asTableObj()->set(key, value);
                } else {
                    runtimeError("attempt to index a " + upTable.typeToString() + " value");
                }
                break;
            }

            case OpCode::OP_CLOSE_UPVALUE: {
                // Close upvalue at top of stack
                closeUpvalues(currentCoroutine_->stack.size() - 1);
                pop();
                break;
            }

            case OpCode::OP_ADD: {
                Value b = pop();
                Value a = pop();
                if (a.isNumber() && b.isNumber()) {
                    push(add(a, b));
                } else if (!callBinaryMetamethod(a, b, "__add")) {
                    runtimeError("attempt to perform arithmetic on " + a.typeToString() + " and " + b.typeToString());
                }
                break;
            }

            case OpCode::OP_SUB: {
                Value b = pop();
                Value a = pop();
                if (a.isNumber() && b.isNumber()) {
                    push(subtract(a, b));
                } else if (!callBinaryMetamethod(a, b, "__sub")) {
                    runtimeError("attempt to perform arithmetic on " + a.typeToString() + " and " + b.typeToString());
                }
                break;
            }

            case OpCode::OP_MUL: {
                Value b = pop();
                Value a = pop();
                if (a.isNumber() && b.isNumber()) {
                    push(multiply(a, b));
                } else if (!callBinaryMetamethod(a, b, "__mul")) {
                    runtimeError("attempt to perform arithmetic on " + a.typeToString() + " and " + b.typeToString());
                }
                break;
            }

            case OpCode::OP_DIV: {
                Value b = pop();
                Value a = pop();
                if (a.isNumber() && b.isNumber()) {
                    push(divide(a, b));
                } else if (!callBinaryMetamethod(a, b, "__div")) {
                    runtimeError("attempt to perform arithmetic on " + a.typeToString() + " and " + b.typeToString());
                }
                break;
            }

            case OpCode::OP_IDIV: {
                Value b = pop();
                Value a = pop();
                if (a.isNumber() && b.isNumber()) {
                    push(integerDivide(a, b));
                } else if (!callBinaryMetamethod(a, b, "__idiv")) {
                    runtimeError("attempt to perform arithmetic on " + a.typeToString() + " and " + b.typeToString());
                }
                break;
            }

            case OpCode::OP_MOD: {
                Value b = pop();
                Value a = pop();
                if (a.isNumber() && b.isNumber()) {
                    push(modulo(a, b));
                } else if (!callBinaryMetamethod(a, b, "__mod")) {
                    runtimeError("attempt to perform arithmetic on " + a.typeToString() + " and " + b.typeToString());
                }
                break;
            }

            case OpCode::OP_POW: {
                Value b = pop();
                Value a = pop();
                if (a.isNumber() && b.isNumber()) {
                    push(power(a, b));
                } else if (!callBinaryMetamethod(a, b, "__pow")) {
                    runtimeError("attempt to perform arithmetic on " + a.typeToString() + " and " + b.typeToString());
                }
                break;
            }

            case OpCode::OP_BAND: {
                Value b = pop();
                Value a = pop();
                if (a.isNumber() && b.isNumber()) {
                    push(bitwiseAnd(a, b));
                } else if (!callBinaryMetamethod(a, b, "__band")) {
                    runtimeError("attempt to perform bitwise operation on " + a.typeToString() + " and " + b.typeToString());
                }
                break;
            }

            case OpCode::OP_BOR: {
                Value b = pop();
                Value a = pop();
                if (a.isNumber() && b.isNumber()) {
                    push(bitwiseOr(a, b));
                } else if (!callBinaryMetamethod(a, b, "__bor")) {
                    runtimeError("attempt to perform bitwise operation on " + a.typeToString() + " and " + b.typeToString());
                }
                break;
            }

            case OpCode::OP_BXOR: {
                Value b = pop();
                Value a = pop();
                if (a.isNumber() && b.isNumber()) {
                    push(bitwiseXor(a, b));
                } else if (!callBinaryMetamethod(a, b, "__bxor")) {
                    runtimeError("attempt to perform bitwise operation on " + a.typeToString() + " and " + b.typeToString());
                }
                break;
            }

            case OpCode::OP_SHL: {
                Value b = pop();
                Value a = pop();
                if (a.isNumber() && b.isNumber()) {
                    push(shiftLeft(a, b));
                } else if (!callBinaryMetamethod(a, b, "__shl")) {
                    runtimeError("attempt to perform bitwise operation on " + a.typeToString() + " and " + b.typeToString());
                }
                break;
            }

            case OpCode::OP_SHR: {
                Value b = pop();
                Value a = pop();
                if (a.isNumber() && b.isNumber()) {
                    push(shiftRight(a, b));
                } else if (!callBinaryMetamethod(a, b, "__shr")) {
                    runtimeError("attempt to perform bitwise operation on " + a.typeToString() + " and " + b.typeToString());
                }
                break;
            }

            case OpCode::OP_CONCAT: {
                Value b = pop();
                Value a = pop();
                if ((a.isString() || a.isNumber()) && (b.isString() || b.isNumber())) {
                    push(concat(a, b));
                } else if (!callBinaryMetamethod(a, b, "__concat")) {
                    runtimeError("attempt to concatenate " + a.typeToString() + " and " + b.typeToString());
                }
                break;
            }

            case OpCode::OP_NEG: {
                Value a = pop();
                if (a.isNumber()) {
                    push(Value::number(-a.asNumber()));
                } else {
                    Value func = getMetamethod(a, "__unm");
                    if (!func.isNil()) {
                        push(func);
                        push(a);
                        callValue(1, 2); // Expect 1 result (1+1=2)
                    } else {
                        runtimeError("attempt to perform arithmetic on " + a.typeToString());
                    }
                }
                break;
            }

            case OpCode::OP_NOT: {
                Value a = pop();
                push(logicalNot(a));
                break;
            }

            case OpCode::OP_BNOT: {
                Value a = pop();
                if (a.isNumber()) {
                    push(bitwiseNot(a));
                } else if (!callBinaryMetamethod(a, a, "__bnot")) { // Unary bitwise NOT
                    runtimeError("attempt to perform bitwise operation on " + a.typeToString());
                }
                break;
            }

            case OpCode::OP_LEN: {
                Value a = pop();
                if (a.isString()) {
                    push(Value::number(static_cast<double>(getStringValue(a).length())));
                } else if (a.isTable()) {
                    Value mm = getMetamethod(a, "__len");
                    if (!mm.isNil()) {
                        push(mm);
                        push(a);
                        callValue(1, 2); // Expect 1 result (1+1=2)
                    } else {
                        push(Value::number(static_cast<double>(a.asTableObj()->length())));
                    }
                } else {
                    Value mm = getMetamethod(a, "__len");
                    if (!mm.isNil()) {
                        push(mm);
                        push(a);
                        callValue(1, 2);
                    } else {
                        runtimeError("attempt to get length of a " + a.typeToString() + " value");
                    }
                }
                break;
            }

            case OpCode::OP_EQUAL: {
                Value b = pop();
                Value a = pop();
                if (equal(a, b).asBool()) {
                    push(Value::boolean(true));
                } else if (a.isTable() && b.isTable() && callBinaryMetamethod(a, b, "__eq")) {
                    // Metamethod called, result will be pushed
                } else {
                    push(Value::boolean(false));
                }
                break;
            }

            case OpCode::OP_LESS: {
                Value b = pop();
                Value a = pop();
                if (a.isNumber() && b.isNumber()) {
                    push(Value::boolean(a.asNumber() < b.asNumber()));
                } else if ((a.isString() || a.isRuntimeString()) && (b.isString() || b.isRuntimeString())) {
                    push(Value::boolean(getStringValue(a) < getStringValue(b)));
                } else if (!callBinaryMetamethod(a, b, "__lt")) {
                    runtimeError("attempt to compare " + a.typeToString() + " and " + b.typeToString());
                }
                break;
            }

            case OpCode::OP_LESS_EQUAL: {
                Value b = pop();
                Value a = pop();
                if (a.isNumber() && b.isNumber()) {
                    push(Value::boolean(a.asNumber() <= b.asNumber()));
                } else if ((a.isString() || a.isRuntimeString()) && (b.isString() || b.isRuntimeString())) {
                    push(Value::boolean(getStringValue(a) <= getStringValue(b)));
                } else {
#ifdef DEBUG
                    std::cout << "DEBUG LE: a=" << a << " (bits=" << std::hex << a.bits() << std::dec << ") b=" << b << " (bits=" << std::hex << b.bits() << std::dec << ")" << std::endl;
#endif
                    if (!callBinaryMetamethod(a, b, "__le")) {
                        runtimeError("attempt to compare " + a.typeToString() + " and " + b.typeToString());
                    }
                }
                break;
            }

            case OpCode::OP_GREATER: {
                Value b = pop();
                Value a = pop();
                if (a.isNumber() && b.isNumber()) {
                    push(Value::boolean(a.asNumber() > b.asNumber()));
                } else if ((a.isString() || a.isRuntimeString()) && (b.isString() || b.isRuntimeString())) {
                    push(Value::boolean(getStringValue(a) > getStringValue(b)));
                } else if (!callBinaryMetamethod(b, a, "__lt")) {
                    runtimeError("attempt to compare " + a.typeToString() + " and " + b.typeToString());
                }
                break;
            }

            case OpCode::OP_GREATER_EQUAL: {
                Value b = pop();
                Value a = pop();
                if (a.isNumber() && b.isNumber()) {
                    push(Value::boolean(a.asNumber() >= b.asNumber()));
                } else if ((a.isString() || a.isRuntimeString()) && (b.isString() || b.isRuntimeString())) {
                    push(Value::boolean(getStringValue(a) >= getStringValue(b)));
                } else if (!callBinaryMetamethod(b, a, "__le")) {
                    runtimeError("attempt to compare " + a.typeToString() + " and " + b.typeToString());
                }
                break;
            }

            case OpCode::OP_PRINT: {
                Value value = pop();
                // Special handling for strings
                if (value.isString()) {
                    size_t index = value.asStringIndex();
                    StringObject* str = nullptr;

                    // Check if it's a runtime string or compile-time string
                    if (value.isRuntimeString()) {
                        // Runtime string from VM pool
                        str = getString(index);
                    } else {
                        // Use current chunk if possible, otherwise fall back to root
                        str = currentFrame().chunk ? currentFrame().chunk->getString(index) : currentCoroutine_->rootChunk->getString(index);
                    }

                    if (str) {
                        std::cout << str->chars() << std::endl;
                    } else {
                        std::cout << "<invalid string>" << std::endl;
                    }
                } else {
                    std::cout << value << std::endl;
                }
                break;
            }

            case OpCode::OP_POP:
                pop();
                break;

            case OpCode::OP_DUP:
                push(peek(0));
                break;

            case OpCode::OP_SWAP: {
                Value a = pop();
                Value b = pop();
                push(a);
                push(b);
                break;
            }

            case OpCode::OP_ROTATE: {
                uint8_t n = readByte();
                if (n >= 2 && currentCoroutine_->stack.size() >= n) {
                    // move the n-th value from top to the top of the stack
                    // Stack: [..., val_n, val_n-1, ..., val_1] -> [..., val_n-1, ..., val_1, val_n]
                    size_t idx = currentCoroutine_->stack.size() - n;
                    Value val = currentCoroutine_->stack[idx];
                    currentCoroutine_->stack.erase(currentCoroutine_->stack.begin() + idx);
                    currentCoroutine_->stack.push_back(val);
                }
                break;
            }

            case OpCode::OP_JUMP: {
                uint16_t offset = readByte() | (readByte() << 8);
                currentFrame().ip += offset;
                break;
            }

            case OpCode::OP_JUMP_IF_FALSE: {
                uint16_t offset = readByte() | (readByte() << 8);
                if (peek(0).isFalsey()) {
                    currentFrame().ip += offset;
                }
                break;
            }

            case OpCode::OP_LOOP: {
                uint16_t offset = readByte() | (readByte() << 8);
                currentFrame().ip -= offset;
                break;
            }

            case OpCode::OP_CLOSURE: {
                uint8_t constantIndex = readByte();
                Value funcValue = currentFrame().chunk->constants()[constantIndex];
                size_t funcIndex = funcValue.asFunctionIndex();
                // Look up function in current chunk (not rootChunk) for nested functions
                FunctionObject* function = currentFrame().chunk->getFunction(funcIndex);

                // Create closure
                ClosureObject* closure = createClosure(function);

                // Capture upvalues
                for (size_t i = 0; i < closure->upvalueCount(); i++) {
                    uint8_t isLocal = readByte();
                    uint8_t index = readByte();

                    if (isLocal) {

                        // Capture local variable from current frame
                        size_t stackIndex = currentFrame().stackBase + index;
                        UpvalueObject* upvalue = captureUpvalue(stackIndex);
                        closure->setUpvalue(i, upvalue);
                    } else {
                        // Capture upvalue from enclosing closure
                        UpvalueObject* upvalue = currentFrame().closure->getUpvalueObj(index);
                        closure->setUpvalue(i, upvalue);
                    }
                }

                push(Value::closure(closure));
                break;
            }

            case OpCode::OP_CALL: {
                uint8_t argCount = readByte();
                uint8_t retCount = readByte();  // Number of return values to keep (0 = all)
                size_t prevFrames = currentCoroutine_->frames.size();
                if (!callValue(argCount, retCount)) {
                    return false;
                }
                // If it was a Lua call, trigger hook
                if (currentCoroutine_->frames.size() > prevFrames && 
                    (currentCoroutine_->hookMask & CoroutineObject::MASK_CALL)) {
                    callHook("call");
                }
                break;
            }

            case OpCode::OP_CALL_MULTI: {
                uint8_t fixedArgCount = readByte();
                uint8_t retCount = readByte();
                // actual argCount = fixedArgs + lastResultCount
                int actualArgCount = static_cast<int>(fixedArgCount) + static_cast<int>(currentCoroutine_->lastResultCount);
                size_t prevFrames = currentCoroutine_->frames.size();
                if (!callValue(actualArgCount, retCount)) {
                    return false;
                }
                // If it was a Lua call, trigger hook
                if (currentCoroutine_->frames.size() > prevFrames && 
                    (currentCoroutine_->hookMask & CoroutineObject::MASK_CALL)) {
                    callHook("call");
                }
                break;
            }

            case OpCode::OP_TAILCALL: {
                uint8_t argCount = readByte();
                // A tailcall always expects ALL return values (retCount = 0)
                size_t prevFrames = currentCoroutine_->frames.size();
                if (!callValue(argCount, 0, true)) {
                    return false;
                }
                // If it was a Lua call, trigger hook
                if (currentCoroutine_->frames.size() > prevFrames && 
                    (currentCoroutine_->hookMask & CoroutineObject::MASK_CALL)) {
                    callHook("call");
                }
                break;
            }

            case OpCode::OP_TAILCALL_MULTI: {
                uint8_t fixedArgCount = readByte();
                int actualArgCount = static_cast<int>(fixedArgCount) + static_cast<int>(currentCoroutine_->lastResultCount);
                size_t prevFrames = currentCoroutine_->frames.size();
                if (!callValue(actualArgCount, 0, true)) {
                    return false;
                }
                // If it was a Lua call, trigger hook
                if (currentCoroutine_->frames.size() > prevFrames && 
                    (currentCoroutine_->hookMask & CoroutineObject::MASK_CALL)) {
                    callHook("call");
                }
                break;
            }

            case OpCode::OP_RETURN_VALUE: {
                // Handle debug hook before cleanup
                if (currentCoroutine_->hookMask & CoroutineObject::MASK_RET) {
                    callHook("return");
                }

                // Read the count of return values
                uint8_t count = readByte();
                size_t actualCount = count;
                if (count == 0) {
                    actualCount = currentCoroutine_->lastResultCount;
                }

                // Pop all return values from the stack (in reverse order)
                std::vector<Value> returnValues;
                returnValues.reserve(actualCount);
                for (size_t i = 0; i < actualCount; i++) {
                    returnValues.push_back(pop());
                }
                // Reverse so they're in correct order
                std::reverse(returnValues.begin(), returnValues.end());

                // If it's the VERY LAST frame of the coroutine, do special root cleanup and return.
                if (currentCoroutine_->frames.size() == 1) {
                    size_t stackBase = currentFrame().stackBase;
                    while (currentCoroutine_->stack.size() > stackBase) {
                        pop();
                    }
                    if (stackBase > 0) pop(); // Pop closure

                    // Push results

                    currentCoroutine_->lastResultCount = returnValues.size();
                    for (const auto& value : returnValues) {
                        push(value);
                    }
                    
                    currentCoroutine_->frames.pop_back();
                    currentCoroutine_->status = CoroutineObject::Status::DEAD;
                    return !hadError_;
                }
                
                // Get the expected return count from the call frame
                uint8_t expectedRetCount = currentFrame().retCount;

                // Adjust return values based on what caller expects
                if (expectedRetCount > 0) {
                    size_t expected = static_cast<size_t>(expectedRetCount - 1);
                    if (returnValues.size() > expected) {
                        // Keep only the first expected values
                        returnValues.resize(expected);
                    } else if (returnValues.size() < expected) {
                        // Pad with nils if we returned fewer than expected
                        while (returnValues.size() < expected) {
                            returnValues.push_back(Value::nil());
                        }
                    }
                }
                // If expectedRetCount == 0, keep all values (no adjustment)

                // Close upvalues for locals that are going out of scope
                size_t stackBase = currentFrame().stackBase;
                closeUpvalues(stackBase);

                // Pop all locals and arguments (down to stackBase)
                while (currentCoroutine_->stack.size() > stackBase) {
                    pop();
                }

                // Also pop the closure object itself (it's at stackBase - 1)
                pop();

                // Get return state before popping frame
                const Chunk* returnChunk = currentFrame().callerChunk;

                // Pop call frame
                currentCoroutine_->frames.pop_back();

                // Restore execution state
                currentCoroutine_->chunk = returnChunk;
                // Note: currentFrame().ip is now the caller's ip

                // Set lastResultCount before pushing so it matches the number of returned values
                currentCoroutine_->lastResultCount = returnValues.size();

                // Push all return values (replaces where function was)
                for (const auto& value : returnValues) {
                    push(value);
                }
                
                // Check if we hit the target frame count (for pcall/load)
                if (targetFrameCount > 0 && currentCoroutine_->frames.size() <= targetFrameCount) {
                    return !hadError_;
                }
                break;
            }

            case OpCode::OP_NEW_TABLE: {
                TableObject* table = createTable();
                push(Value::table(table));
                break;
            }

            case OpCode::OP_GET_TABLE: {
                Value key = pop();
                Value tableValue = pop();

                if (tableValue.isTable()) {
                    TableObject* table = tableValue.asTableObj();
                    Value value = table->get(key);
                    if (!value.isNil()) {
                        push(value);
                        break;
                    }
                }

                // Not found in table or not a table, check metatable for the property itself
                // (e.g. string methods are in the string metatable or its __index)
                if (key.isString()) {
                    Value mm = getMetamethod(tableValue, getStringValue(key));
                    if (!mm.isNil()) {
                        push(mm);
                        break;
                    }
                }

                // If not found as property, check standard __index metamethod
                Value indexMethod = getMetamethod(tableValue, "__index");
                if (indexMethod.isNil()) {
                    if (!tableValue.isTable()) {
                        runtimeError("attempt to index a " + tableValue.typeToString() + " value");
                    }
                    push(Value::nil());
                } else if (indexMethod.isFunction()) {
                    push(indexMethod);
                    push(tableValue);
                    push(key);
                    callValue(2, 2); // Expect 1 result (1 + 1 = 2)
                } else if (indexMethod.isTable()) {
                    // Recurse into the __index table
                    TableObject* indexTable = indexMethod.asTableObj();
                    Value result = key.isString() ? indexTable->get(getStringValue(key)) : indexTable->get(key);
                    push(result);
                } else {
                    push(Value::nil());
                }
                break;
            }

            case OpCode::OP_SET_TABLE: {
                Value value = pop();
                Value key = pop();
                Value tableValue = pop();

                if (tableValue.isTable()) {
                    TableObject* table = tableValue.asTableObj();
                    if (table->has(key)) {
                        table->set(key, value);
                        break;
                    }
                }

                Value newIndex = getMetamethod(tableValue, "__newindex");
                if (newIndex.isNil()) {
                    if (tableValue.isTable()) {
                        TableObject* table = tableValue.asTableObj();
                        table->set(key, value);
                    } else {
                        runtimeError("attempt to index a " + tableValue.typeToString() + " value");
                    }
                } else if (newIndex.isFunction()) {
                    push(newIndex);
                    push(tableValue);
                    push(key);
                    push(value);
                    callValue(3, 1); // Expect 0 results (0 + 1 = 1)
                } else if (newIndex.isTable()) {
                    TableObject* niTable = newIndex.asTableObj();
                    if (key.isString()) {
                        niTable->set(getStringValue(key), value);
                    } else {
                        niTable->set(key, value);
                    }
                } else {
                    if (tableValue.isTable()) {
                        TableObject* table = tableValue.asTableObj();
                        table->set(key, value);
                    } else {
                        runtimeError("attempt to index a " + tableValue.typeToString() + " value");
                    }
                }
                break;
            }

            case OpCode::OP_GET_VARARG: {
                uint8_t retCount = readByte();
                // Push varargs onto the stack
                if (currentCoroutine_->frames.empty()) {
                    runtimeError("Cannot access varargs outside of a function");
                    break;
                }

                CallFrame& frame = currentFrame();
                const auto& varargs = frame.varargs;
                
                if (retCount == 0) {
                    // Push all varargs
                    for (size_t i = 0; i < varargs.size(); i++) {
                        push(varargs[i]);
                    }
                    currentCoroutine_->lastResultCount = varargs.size();
                } else {
                    // Push exactly retCount - 1 values
                    int count = (int)retCount - 1;
                    for (int i = 0; i < count; i++) {
                        if (i < (int)varargs.size()) {
                            push(varargs[i]);
                        } else {
                            push(Value::nil());
                        }
                    }
                    currentCoroutine_->lastResultCount = count;
                }
                break;
            }

            case OpCode::OP_SET_TABLE_MULTI: {
                // Stack: [..., table, key_base, val1, val2, ..., valN]
                // key_base is the FIRST numeric key to start with.
                // N is lastResultCount.
                size_t n = currentCoroutine_->lastResultCount;
                
                std::vector<Value> values;
                values.reserve(n);
                for (size_t i = 0; i < n; i++) {
                    values.push_back(pop());
                }
                std::reverse(values.begin(), values.end());
                
                Value keyBaseVal = pop();
                Value tableValue = pop();
                
                if (!tableValue.isTable()) {
                    runtimeError("Attempt to index a non-table value");
                    break;
                }
                
                TableObject* table = tableValue.asTableObj();
                double keyBase = keyBaseVal.asNumber();
                
                for (size_t i = 0; i < n; i++) {
                    table->set(Value::number(keyBase + i), values[i]);
                }
                break;
            }

            case OpCode::OP_YIELD: {
                uint8_t count = readByte();
                uint8_t retCount = readByte();
                
                // std::cout << "DEBUG YIELD: count=" << (int)count << " retCount=" << (int)retCount << " stackSize=" << currentCoroutine_->stack.size() << std::endl;

                // Pop yielded values and save them
                currentCoroutine_->yieldedValues.clear();
                for (int i = 0; i < count; i++) {
                    currentCoroutine_->yieldedValues.push_back(pop());
                }
                // Reverse so they are in original order
                std::reverse(currentCoroutine_->yieldedValues.begin(), currentCoroutine_->yieldedValues.end());

                currentCoroutine_->status = CoroutineObject::Status::SUSPENDED;
                currentCoroutine_->yieldCount = count;
                currentCoroutine_->retCount = retCount;
                return true; // Return to resumer
            }

            case OpCode::OP_RETURN:
                if (currentCoroutine_->hookMask & CoroutineObject::MASK_RET) {
                    callHook("return");
                }
                if (currentCoroutine_->frames.size() == 1) {
                    currentCoroutine_->status = CoroutineObject::Status::DEAD;
                    return !hadError_;
                }
                if (targetFrameCount > 0 && currentCoroutine_->frames.size() <= targetFrameCount + 1) {
                    // Similar to OP_RETURN_VALUE, if we hit the target frame count.
                    // But OP_RETURN usually means returning from a chunk without return values.
                    // This is handled by returning nil if needed, but the compiler usually emits OP_NIL then OP_RETURN_VALUE.
                    // We'll just return from the loop here.
                    return !hadError_;
                }
                // Internal returns are handled by OP_RETURN_VALUE
                break;

            default:
                runtimeError("Unknown opcode");
                return false;
        }

        if (hadError_) {
            return false;
        }
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
    Value constant = currentFrame().chunk->getConstant(index);
    
    // If it's a compile-time string, intern it to get a runtime string
    // This ensures all strings in the VM use the same pool and can be compared by index/identity
    if (constant.isString() && !constant.isRuntimeString()) {
        StringObject* str = currentFrame().chunk->getString(constant.asStringIndex());
        StringObject* runtimeStr = internString(str->chars(), str->length());
        return Value::runtimeString(runtimeStr);
    }
    
    return constant;
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
        success = false;
    }
    
    inPcall_ = prevPcall;

    if (!success) {
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
    if (argCount < 1) {
        runtimeError("xpcall expects at least 2 arguments (func, msgh)");
        return false;
    }

    size_t prevFrames = currentCoroutine_->frames.size();
    // Stack: [..., func, msgh, arg1, arg2, ...]
    // argCount is number of args to func PLUS msgh
    // Wait, Lua xpcall is: xpcall(f, msgh, ...)
    // Our stack for native xpcall is: [..., xpcall_native, f, msgh, arg1, arg2, ...]
    // argCount is (1 + 1 + N) = 2 + N.
    
    // Actually, in native_xpcall: argCount is 2 + N.
    // Let's assume argCount here is the total number of arguments passed to xpcall (f, msgh, args).
    
    // First argument is f, second is msgh, rest are args to f.
    // We want to call f with (argCount - 2) args.
    
    // Save error handler
    Value msgh = peek(argCount - 2);
    size_t stackSizeBefore = currentCoroutine_->stack.size() - argCount; // Leave xpcall on stack

    bool prevPcall = inPcall_;
    inPcall_ = true;
    
    // Shift arguments to call f: [f, arg1, arg2, ...]
    // We need to remove msgh from the stack temporarily.
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
        success = false;
    }
    
    inPcall_ = prevPcall;

    if (!success) {
        printf("DEBUG: pcall/xpcall failure unwind. prevFrames=%zu current=%zu\n", prevFrames, currentCoroutine_->frames.size());
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
        
        hadError_ = false; // Prepare to call error handler
        if (callValue(1, 2)) { // Expect 1 result (1+1=2)
            // Error handler should return the new error object
            // Results of callValue are already pushed
        } else {
            // Error in error handler!
            push(Value::runtimeString(internString("error in error handler")));
        }
        
        hadError_ = false; // Recovered (pcall/xpcall always return true to VM::run)
    } else {
        // Success.
        size_t resultCount = currentCoroutine_->lastResultCount;
        
        std::vector<Value> results;
        for (size_t i = 0; i < resultCount; i++) {
            results.push_back(pop());
        }
        std::reverse(results.begin(), results.end());
        
        // Pop remaining arguments if any
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

bool VM::callValue(int argCount, int retCount, bool isTailCall) {
    Value callee = peek(argCount);

    if (callee.isNativeFunction()) {
        size_t funcIndex = callee.asNativeFunctionIndex();
        NativeFunction func = getNativeFunction(funcIndex);

        if (!func) {
            runtimeError("Invalid native function");
            return false;
        }

        size_t funcPosition = currentCoroutine_->stack.size() - argCount - 1;

        if (!func(this, argCount)) {
            return false;
        }

        // After native call, stack is: [..., func, result1, result2, ...]
        size_t resultCount = currentCoroutine_->stack.size() - funcPosition - 1;

        std::vector<Value> results;
        results.reserve(resultCount);
        for (size_t i = 0; i < resultCount; i++) {
            results.push_back(pop());
        }
        // Reverse so results[0] is the FIRST return value
        std::reverse(results.begin(), results.end());

        pop(); // Pop function

        // Adjust result count based on what caller expects
        if (retCount > 0) {
            size_t expected = static_cast<size_t>(retCount - 1);
            if (results.size() > expected) {
                // Truncate
                results.resize(expected);
            } else if (results.size() < expected) {
                // Pad with nil
                while (results.size() < expected) {
                    results.push_back(Value::nil());
                }
            }
        }
        // If retCount == 0, keep all results (multires context)

        // Push results back in correct order
        currentCoroutine_->lastResultCount = results.size();
        for (const auto& result : results) {
            push(result);
        }
        return true;
    } else if (callee.isClosure()) {
        ClosureObject* closure = callee.asClosureObj();

        if (!closure) {
            runtimeError("Invalid closure");
            return false;
        }

        FunctionObject* function = closure->function();
        int arity = function->arity();
        bool hasVarargs = function->hasVarargs();

        if (argCount < arity) {
            // Pad with nils
            for (int i = 0; i < arity - argCount; i++) {
                push(Value::nil());
            }
            argCount = arity;
        }

        if (currentCoroutine_->frames.size() >= FRAMES_MAX) {
            runtimeError("Stack overflow");
            return false;
        }

        std::vector<Value> varargs;
        if (argCount > arity) {
            uint8_t extraCount = argCount - arity;
            if (hasVarargs) {
                for (int i = 0; i < extraCount; i++) {
                    varargs.push_back(currentCoroutine_->stack.back());
                    currentCoroutine_->stack.pop_back();
                }
                std::reverse(varargs.begin(), varargs.end());
            } else {
                for (int i = 0; i < extraCount; i++) {
                    currentCoroutine_->stack.pop_back();
                }
            }
            argCount = arity; // Adjust argCount so stackBase is correct
        }

        CallFrame frame;
        frame.closure = closure;
        frame.chunk = function->chunk();
        frame.ip = 0; // New frame starts at beginning of its chunk
        frame.retCount = retCount;
        frame.varargs = std::move(varargs);

        if (isTailCall && !currentCoroutine_->frames.empty()) {
            // Close upvalues for current frame
            size_t oldStackBase = currentCoroutine_->frames.back().stackBase;
            closeUpvalues(oldStackBase);
            
            size_t newStackBase = currentCoroutine_->stack.size() - argCount;
            size_t calleePos = newStackBase - 1;
            
            // Move callee and args down
            for (int i = 0; i <= argCount; i++) {
                currentCoroutine_->stack[oldStackBase - 1 + i] = currentCoroutine_->stack[calleePos + i];
            }
            
            // Pop the rest
            while (currentCoroutine_->stack.size() > oldStackBase + argCount) {
                currentCoroutine_->stack.pop_back();
            }
            
            frame.stackBase = oldStackBase;
            frame.callerChunk = currentCoroutine_->frames.back().callerChunk;
            
            currentCoroutine_->frames.pop_back();
            currentCoroutine_->frames.push_back(std::move(frame));
        } else {
            frame.stackBase = currentCoroutine_->stack.size() - argCount;
            frame.callerChunk = currentCoroutine_->chunk;
            currentCoroutine_->frames.push_back(std::move(frame));
        }

        currentCoroutine_->chunk = function->chunk();
        currentCoroutine_->lastResultCount = 0; // Initialize for new frame
        return true;
    } else if (callee.isTable()) {
        Value callMethod = getMetamethod(callee, "__call");
        if (!callMethod.isNil()) {
             // Insert callMethod before callee
             // Stack: [..., callee, arg1, arg2...]
             // Target: [..., callMethod, callee, arg1, arg2...]
             
             // We can just push callMethod, then rotate?
             // Or simpler: verify stack depth.
             // argCount is N.
             // callee is at currentCoroutine_->stack.size() - 1 - argCount.
             
             // Insert logic:
             // push(callMethod);
             // Move to correct position?
             // std::vector insert is O(N).
             
             // Let's just push callMethod and rotate everything above it up?
             // Or use a temp vector to rebuild stack top.
             
             std::vector<Value> args;
             for (int i=0; i < argCount; i++) args.push_back(pop());
             Value func = pop(); // callee
             
             push(callMethod);
             push(func);
             for (auto it = args.rbegin(); it != args.rend(); ++it) push(*it);
             
             return callValue(argCount + 1, retCount, isTailCall);
        }
    }

    runtimeError("Attempt to call a " + callee.typeToString() + " value: " + callee.toString());
    return false;
}

bool VM::callBinaryMetamethod(const Value& a, const Value& b, const std::string& method) {
    Value func = getMetamethod(a, method);
    if (func.isNil()) {
        func = getMetamethod(b, method);
    }
    
    if (func.isNil()) {
        return false;
    }
    
    push(func);
    push(a);
    push(b);
    return callValue(2, 2); // Expect 1 result (1+1=2)
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
    double divisor = b.asNumber();
    if (divisor == 0.0) {
        runtimeError("Division by zero");
        return Value::nil();
    }
    // Float division always returns float in Lua 5.3+
    return Value::number(a.asNumber() / divisor);
}

Value VM::modulo(const Value& a, const Value& b) {
    if (!a.isNumber() || !b.isNumber()) {
        runtimeError("Operands must be numbers");
        return Value::nil();
    }
    if (a.isInteger() && b.isInteger()) {
        int64_t div = b.asInteger();
        if (div == 0) {
            runtimeError("Modulo by zero");
            return Value::nil();
        }
        int64_t val = a.asInteger();
        int64_t res = val % div;
        if (res != 0 && (val < 0) != (div < 0)) {
            res += div;
        }
        return Value::integer(res);
    }
    double av = a.asNumber();
    double bv = b.asNumber();
    double res = std::fmod(av, bv);
    if (res != 0.0 && (av < 0.0) != (bv < 0.0)) {
        res += bv;
    }
    return Value::number(res);
}

Value VM::power(const Value& a, const Value& b) {
    if (!a.isNumber() || !b.isNumber()) {
        runtimeError("Operands must be numbers");
        return Value::nil();
    }
    // Power always returns float
    return Value::number(std::pow(a.asNumber(), b.asNumber()));
}

Value VM::integerDivide(const Value& a, const Value& b) {
    if (!a.isNumber() || !b.isNumber()) {
        runtimeError("Operands must be numbers");
        return Value::nil();
    }
    double divisor = b.asNumber();
    if (divisor == 0.0) {
        runtimeError("Division by zero");
        return Value::nil();
    }
    return Value::number(std::floor(a.asNumber() / divisor));
}

Value VM::bitwiseAnd(const Value& a, const Value& b) {
    if (!a.isNumber() || !b.isNumber()) {
        runtimeError("attempt to perform bitwise operation on non-number");
        return Value::nil();
    }
    return Value::integer(a.asInteger() & b.asInteger());
}

Value VM::bitwiseOr(const Value& a, const Value& b) {
    if (!a.isNumber() || !b.isNumber()) {
        runtimeError("attempt to perform bitwise operation on non-number");
        return Value::nil();
    }
    return Value::integer(a.asInteger() | b.asInteger());
}

Value VM::bitwiseXor(const Value& a, const Value& b) {
    if (!a.isNumber() || !b.isNumber()) {
        runtimeError("attempt to perform bitwise operation on non-number");
        return Value::nil();
    }
    return Value::integer(a.asInteger() ^ b.asInteger());
}

Value VM::shiftLeft(const Value& a, const Value& b) {
    if (!a.isNumber() || !b.isNumber()) {
        runtimeError("attempt to perform bitwise operation on non-number");
        return Value::nil();
    }
    return Value::integer(a.asInteger() << b.asInteger());
}

Value VM::shiftRight(const Value& a, const Value& b) {
    if (!a.isNumber() || !b.isNumber()) {
        runtimeError("attempt to perform bitwise operation on non-number");
        return Value::nil();
    }
    // Lua shift right is LOGICAL (fill with zeros) in Lua 5.3+
    // Mask to 48 bits (payload size) before shifting to avoid sign extension issues
    uint64_t uv = static_cast<uint64_t>(a.asInteger()) & 0x0000FFFFFFFFFFFFULL;
    return Value::integer(static_cast<int64_t>(uv >> b.asInteger()));
}

Value VM::concat(const Value& a, const Value& b) {
    std::string result = getStringValue(a) + getStringValue(b);
    StringObject* str = internString(result);
    return Value::runtimeString(str);
}

std::string VM::getStringValue(const Value& value) {
    if (value.isRuntimeString()) {
        return value.asStringObj()->chars();
    } else if (value.isString()) {
        size_t index = value.asStringIndex();
        // Use current chunk if possible, otherwise fall back to root
        const Chunk* chunk = currentCoroutine_->frames.empty() ? currentCoroutine_->chunk : currentFrame().chunk;
        StringObject* str = chunk ? chunk->getString(index) : currentCoroutine_->rootChunk->getString(index);
        return str ? str->chars() : "";
    }
    return value.toString();
}

Value VM::negate(const Value& a) {
    if (!a.isNumber()) {
        runtimeError("Operand must be a number");
        return Value::nil();
    }
    return Value::number(-a.asNumber());
}

Value VM::equal(const Value& a, const Value& b) {
    // Strings may come from two pools (compile-time vs runtime), so compare content
    bool aIsStr = a.isString() || a.isRuntimeString();
    bool bIsStr = b.isString() || b.isRuntimeString();
    if (aIsStr && bIsStr) {
        return Value::boolean(getStringValue(a) == getStringValue(b));
    }
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

Value VM::bitwiseNot(const Value& a) {
    if (!a.isNumber()) {
        runtimeError("attempt to perform bitwise operation on non-number");
        return Value::nil();
    }
    return Value::integer(~a.asInteger());
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
    for (const auto& value : currentCoroutine_->stack) {
        std::cout << "[ " << value << " ]";
    }
    std::cout << std::endl;

    // Disassemble current instruction
    const Chunk* chunk = currentCoroutine_->frames.empty() ? currentCoroutine_->chunk : currentFrame().chunk;
    chunk->disassembleInstruction(currentFrame().ip);
}

bool VM::runSource(const std::string& source, const std::string& name) {
    FunctionObject* func = compileSource(source, name);
    if (!func) return false;
    return run(*func);
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

        // Create a FunctionObject to own the Chunk and keep it alive in the VM's function table
        FunctionObject* ptr = function.get();
        registerFunction(function.release());
        return ptr;
    } catch (const std::exception& e) {
        std::cerr << "Error in compileSource (" << name << "): " << e.what() << std::endl;
        return nullptr;
    }
}

bool VM::resumeCoroutine(CoroutineObject* co) {
    CoroutineObject* oldCo = currentCoroutine_;
    currentCoroutine_ = co;
    
    bool result = run();

    currentCoroutine_ = oldCo;
    return result;
}

// ============================================================================
