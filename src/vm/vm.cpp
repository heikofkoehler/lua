#include "vm/vm.hpp"
#include "compiler/lexer.hpp"
#include "compiler/parser.hpp"
#include "compiler/codegen.hpp"
#include <iostream>
#include <cmath>

// Forward declarations for stdlib registration
void registerStringLibrary(VM* vm, TableObject* stringTable);
void registerTableLibrary(VM* vm, TableObject* tableTable);
void registerMathLibrary(VM* vm, TableObject* mathTable);
void registerSocketLibrary(VM* vm, TableObject* socketTable);
void registerCoroutineLibrary(VM* vm, TableObject* coroutineTable);
void registerBaseLibrary(VM* vm);

VM::VM() : mainCoroutine_(nullptr), currentCoroutine_(nullptr), 
           hadError_(false), inPcall_(false), lastErrorMessage_(""), stdlibInitialized_(false),
           gcObjects_(nullptr), bytesAllocated_(0), nextGC_(1024 * 1024), gcEnabled_(true) {    // Create main coroutine
    createCoroutine(nullptr);
    mainCoroutine_ = coroutines_.back();
    mainCoroutine_->status = CoroutineObject::Status::RUNNING;
    currentCoroutine_ = mainCoroutine_;
}

VM::~VM() {
    reset(); // Clean up strings, tables, closures, etc.
    for (auto* func : functions_) {
        delete func;
    }
}

void VM::reset() {
    // Clean up coroutines (VM owns them)
    for (auto* co : coroutines_) {
        delete co;
    }
    coroutines_.clear();
    mainCoroutine_ = nullptr;
    currentCoroutine_ = nullptr;

    // Create a new main coroutine for next use
    createCoroutine(nullptr);
    mainCoroutine_ = coroutines_.back();
    mainCoroutine_->status = CoroutineObject::Status::RUNNING;
    currentCoroutine_ = mainCoroutine_;

    // Note: functions_ not cleaned up here since Chunk owns function objects
    functions_.clear();
    // Clean up strings (VM owns them)
    for (auto* str : strings_) {
        delete str;
    }
    strings_.clear();
    stringIndices_.clear();
    // Clean up tables (VM owns them)
    for (auto* table : tables_) {
        delete table;
    }
    tables_.clear();
    // Clean up closures (VM owns them)
    for (auto* closure : closures_) {
        delete closure;
    }
    closures_.clear();
    // Clean up upvalues (VM owns them)
    for (auto* upvalue : upvalues_) {
        delete upvalue;
    }
    upvalues_.clear();
    // Clean up files (VM owns them)
    for (auto* file : files_) {
        delete file;
    }
    files_.clear();
    hadError_ = false;
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

size_t VM::internString(const char* chars, size_t length) {
    // Trigger GC if needed BEFORE allocation
    if (bytesAllocated_ > nextGC_) {
        collectGarbage();
    }

    // Create temporary string to compute hash
    StringObject temp(chars, length);
    uint32_t hash = temp.hash();

    // Check if string already exists
    auto it = stringIndices_.find(hash);
    if (it != stringIndices_.end()) {
        size_t index = it->second;
        // Verify it's actually the same string (handle hash collisions)
        if (strings_[index] && strings_[index]->equals(chars, length)) {
            return index;
        }
    }

    // String doesn't exist, create and intern it
    if (bytesAllocated_ > nextGC_) {
        collectGarbage();
    }

    StringObject* str = new StringObject(chars, length);
    addObject(str);  // Register with GC
    
    // Try to reuse a freed slot first
    size_t index = strings_.size();
    bool reused = false;
    for (size_t i = 0; i < strings_.size(); i++) {
        if (strings_[i] == nullptr) {
            strings_[i] = str;
            index = i;
            reused = true;
            break;
        }
    }
    
    if (!reused) {
        strings_.push_back(str);
    }
    
    stringIndices_[hash] = index;

    bytesAllocated_ += sizeof(StringObject) + length;

    return index;
}

size_t VM::internString(const std::string& str) {
    return internString(str.c_str(), str.length());
}

StringObject* VM::getString(size_t index) {
    if (index >= strings_.size()) {
        runtimeError("Invalid string index");
        return nullptr;
    }
    return strings_[index];
}

size_t VM::createTable() {
    // Trigger GC if needed BEFORE allocation
    if (bytesAllocated_ > nextGC_) {
        collectGarbage();
    }

    TableObject* table = new TableObject();
    addObject(table);  // Register with GC
    size_t index = tables_.size();
    tables_.push_back(table);

    bytesAllocated_ += sizeof(TableObject);

    return index;
}


TableObject* VM::getTable(size_t index) {
    if (index >= tables_.size()) {
        runtimeError("Invalid table index");
        return nullptr;
    }
    return tables_[index];
}

size_t VM::createClosure(FunctionObject* function) {
    if (bytesAllocated_ > nextGC_) {
        collectGarbage();
    }

    ClosureObject* closure = new ClosureObject(function, function->upvalueCount());
    addObject(closure);  // Register with GC
    size_t index = closures_.size();
    closures_.push_back(closure);

    bytesAllocated_ += sizeof(ClosureObject) + sizeof(size_t) * function->upvalueCount();

    return index;
}

ClosureObject* VM::getClosure(size_t index) {
    if (index >= closures_.size()) {
        runtimeError("Invalid closure index");
        return nullptr;
    }
    return closures_[index];
}

size_t VM::createCoroutine(ClosureObject* closure) {
    if (bytesAllocated_ > nextGC_) {
        collectGarbage();
    }

    CoroutineObject* co = new CoroutineObject();
    addObject(co);  // Register with GC
    size_t index = coroutines_.size();
    coroutines_.push_back(co);

    if (closure) {
        // Find the index of this closure.
        size_t closureIdx = SIZE_MAX;
        for (size_t i = 0; i < closures_.size(); i++) {
            if (closures_[i] == closure) {
                closureIdx = i;
                break;
            }
        }
        
        if (closureIdx != SIZE_MAX) {
            co->stack.push_back(Value::closure(closureIdx));
            
            CallFrame frame;
            frame.closure = closure;
            frame.callerChunk = nullptr;
            frame.ip = 0;
            frame.stackBase = 1;
            frame.retCount = 0;
            // varargs is automatically empty
            co->frames.push_back(frame);
            
            co->chunk = closure->function()->chunk();
            co->rootChunk = closure->function()->chunk(); // Set rootChunk for the coroutine
            co->ip = 0;
        }
    }

    bytesAllocated_ += sizeof(CoroutineObject);

    return index;
}

CoroutineObject* VM::getCoroutine(size_t index) {
    if (index >= coroutines_.size()) {
        runtimeError("Invalid thread index");
        return nullptr;
    }
    return coroutines_[index];
}

size_t VM::getCoroutineIndex(CoroutineObject* co) {
    for (size_t i = 0; i < coroutines_.size(); i++) {
        if (coroutines_[i] == co) {
            return i;
        }
    }
    return SIZE_MAX;
}

size_t VM::captureUpvalue(size_t stackIndex) {
    // Check if upvalue already exists for this stack slot
    for (UpvalueObject* openUpvalue : currentCoroutine_->openUpvalues) {
        if (!openUpvalue->isClosed() && openUpvalue->stackIndex() == stackIndex) {
            // Find and return index
            for (size_t i = 0; i < upvalues_.size(); i++) {
                if (upvalues_[i] == openUpvalue) {
                    return i;
                }
            }
        }
    }

    if (bytesAllocated_ > nextGC_) {
        collectGarbage();
    }

    // Create new upvalue
    UpvalueObject* upvalue = new UpvalueObject(stackIndex);
    addObject(upvalue);  // Register with GC
    size_t index = upvalues_.size();
    upvalues_.push_back(upvalue);

    // Insert into currentCoroutine_->openUpvalues (keep sorted by stack index for efficient closing)
    auto it = currentCoroutine_->openUpvalues.begin();
    while (it != currentCoroutine_->openUpvalues.end() && (*it)->stackIndex() < stackIndex) {
        ++it;
    }
    currentCoroutine_->openUpvalues.insert(it, upvalue);

    bytesAllocated_ += sizeof(UpvalueObject);

    return index;
}


UpvalueObject* VM::getUpvalue(size_t index) {
    if (index >= upvalues_.size()) {
        runtimeError("Invalid upvalue index");
        return nullptr;
    }
    return upvalues_[index];
}

void VM::closeUpvalues(size_t lastStackIndex) {
    // Close all open upvalues at or above lastStackIndex
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

size_t VM::openFile(const std::string& filename, const std::string& mode) {
    FileObject* file = new FileObject(filename, mode);
    addObject(file);  // Register with GC
    files_.push_back(file);
    return files_.size() - 1;
}

FileObject* VM::getFile(size_t index) {
    if (index >= files_.size()) {
        return nullptr;
    }
    return files_[index];
}

void VM::closeFile(size_t index) {
    FileObject* file = getFile(index);
    if (file) {
        file->close();
    }
}

size_t VM::createSocket(socket_t fd) {
    SocketObject* socket = new SocketObject(fd);
    addObject(socket);  // Register with GC
    sockets_.push_back(socket);
    return sockets_.size() - 1;
}

size_t VM::registerSocket(SocketObject* socket) {
    addObject(socket);  // Register with GC
    sockets_.push_back(socket);
    return sockets_.size() - 1;
}

SocketObject* VM::getSocket(size_t index) {
    if (index >= sockets_.size()) {
        return nullptr;
    }
    SocketObject* sock = sockets_[index];
    if (!sock) {
        return nullptr;  // Socket was closed and nullified
    }
    return sock;
}

void VM::closeSocket(size_t index) {
    SocketObject* socket = getSocket(index);
    if (socket) {
        socket->close();
        // Nullify the socket in the vector so it can't be accidentally reused
        sockets_[index] = nullptr;
    }
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
    // Use VM's string pool for keys so they match runtime interned strings
    size_t nameIndex = internString(name);
    table->set(Value::runtimeString(nameIndex), Value::nativeFunction(funcIndex));
}

void VM::initStandardLibrary() {
    // Register base library (global functions like collectgarbage)
    registerBaseLibrary(this);

    // Create 'string' table
    size_t stringTableIdx = createTable();
    TableObject* stringTable = getTable(stringTableIdx);
    registerStringLibrary(this, stringTable);
    globals_["string"] = Value::table(stringTableIdx);

    // Create 'table' table
    size_t tableTableIdx = createTable();
    TableObject* tableTable = getTable(tableTableIdx);
    registerTableLibrary(this, tableTable);
    globals_["table"] = Value::table(tableTableIdx);

    // Create 'math' table
    size_t mathTableIdx = createTable();
    TableObject* mathTable = getTable(mathTableIdx);
    registerMathLibrary(this, mathTable);
    globals_["math"] = Value::table(mathTableIdx);

    // Create 'os' table
    size_t osTableIdx = createTable();
    TableObject* osTable = getTable(osTableIdx);
    void registerOSLibrary(VM* vm, TableObject* osTable);
    registerOSLibrary(this, osTable);
    globals_["os"] = Value::table(osTableIdx);

    // Create 'socket' table
    size_t socketTableIdx = createTable();
    TableObject* socketTable = getTable(socketTableIdx);
    registerSocketLibrary(this, socketTable);
    globals_["socket"] = Value::table(socketTableIdx);

    // Create 'coroutine' table
    size_t coroutineTableIdx = createTable();
    TableObject* coroutineTable = getTable(coroutineTableIdx);
    registerCoroutineLibrary(this, coroutineTable);
    globals_["coroutine"] = Value::table(coroutineTableIdx);

    // Create 'debug' table
    size_t debugTableIdx = createTable();
    TableObject* debugTable = getTable(debugTableIdx);
    void registerDebugLibrary(VM* vm, TableObject* debugTable);
    registerDebugLibrary(this, debugTable);
    globals_["debug"] = Value::table(debugTableIdx);

    // Define coroutine.wrap in Lua
    const char* wrapScript = 
        "function __coroutine_wrap(f)\n"
        "    local co = coroutine.create(f)\n"
        "    return function(...)\n"
        "        local res = table.pack(coroutine.resume(co, ...))\n"
        "        if not res[1] then\n"
        "            error(res[2])\n"
        "        end\n"
        "        return table.unpack(res, 2, res.n)\n"
        "    end\n"
        "end\n"
        "coroutine.wrap = __coroutine_wrap\n"
        "__coroutine_wrap = nil\n";

    runSource(wrapScript, "coroutine_wrap_init");
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

bool VM::run(const Chunk& chunk) {
    // Save current state for recursive calls
    const Chunk* oldChunk = currentCoroutine_->chunk;
    const Chunk* oldRoot = currentCoroutine_->rootChunk;
    size_t oldIP = currentCoroutine_->ip;
    size_t oldFrameCount = currentCoroutine_->frames.size();

    currentCoroutine_->chunk = &chunk;
    if (currentCoroutine_->rootChunk == nullptr) {
        currentCoroutine_->rootChunk = &chunk;
    }
    if (mainCoroutine_->rootChunk == nullptr) {
        mainCoroutine_->rootChunk = &chunk;
    }
    currentCoroutine_->ip = 0;
    hadError_ = false;

    // Initialize standard library on first run (needs chunk for string pool)
    if (!stdlibInitialized_) {
        stdlibInitialized_ = true; // Set before calling to avoid recursion
        initStandardLibrary();
    }

#ifdef PRINT_CODE
    chunk.disassemble("script");
#endif

    // Push root call frame for this chunk
    CallFrame rootFrame;
    rootFrame.closure = nullptr;
    rootFrame.callerChunk = nullptr;
    rootFrame.ip = 0;
    rootFrame.stackBase = currentCoroutine_->stack.size(); // Use current stack size as base
    rootFrame.retCount = 0;
    // varargs is empty
    currentCoroutine_->frames.push_back(rootFrame);

    bool result = run();

    if (currentCoroutine_->status == CoroutineObject::Status::SUSPENDED) {
        // Coroutine yielded - do NOT restore state or pop frames!
        // The state will be restored when resume() finishes and returns to resumer.
        return result;
    }

    // Restore previous state
    currentCoroutine_->chunk = oldChunk;
    currentCoroutine_->rootChunk = oldRoot;
    currentCoroutine_->ip = oldIP;
    
    // Clean up frames from this call (if any left)
    while (currentCoroutine_->frames.size() > oldFrameCount) {
        currentCoroutine_->frames.pop_back();
    }

    return result;
}

bool VM::run() {
    return run(0);
}

bool VM::run(size_t targetFrameCount) {
    // Main execution loop
    while (true) {
#ifdef TRACE_EXECUTION
        traceExecution();
#endif

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
                const std::string& varName = currentCoroutine_->chunk->getIdentifier(nameIndex);
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
                const std::string& varName = currentCoroutine_->chunk->getIdentifier(nameIndex);
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
                    size_t uvIndex = currentFrame().closure->getUpvalue(upvalueIndex);
                    UpvalueObject* upvalue = getUpvalue(uvIndex);
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
                    size_t uvIndex = currentFrame().closure->getUpvalue(upvalueIndex);
                    UpvalueObject* upvalue = getUpvalue(uvIndex);
                    if (upvalue) {
                        upvalue->set(currentCoroutine_->stack, peek(0));
                    }
                } else {
                    runtimeError("Upvalue access outside of closure");
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
                    push(Value::number(a.asNumber() + b.asNumber()));
                } else if (!callBinaryMetamethod(a, b, "__add")) {
                    runtimeError("attempt to perform arithmetic on " + a.typeToString() + " and " + b.typeToString());
                }
                break;
            }

            case OpCode::OP_SUB: {
                Value b = pop();
                Value a = pop();
                if (a.isNumber() && b.isNumber()) {
                    push(Value::number(a.asNumber() - b.asNumber()));
                } else if (!callBinaryMetamethod(a, b, "__sub")) {
                    runtimeError("attempt to perform arithmetic on " + a.typeToString() + " and " + b.typeToString());
                }
                break;
            }

            case OpCode::OP_MUL: {
                Value b = pop();
                Value a = pop();
                if (a.isNumber() && b.isNumber()) {
                    push(Value::number(a.asNumber() * b.asNumber()));
                } else if (!callBinaryMetamethod(a, b, "__mul")) {
                    runtimeError("attempt to perform arithmetic on " + a.typeToString() + " and " + b.typeToString());
                }
                break;
            }

            case OpCode::OP_DIV: {
                Value b = pop();
                Value a = pop();
                if (a.isNumber() && b.isNumber()) {
                    double divisor = b.asNumber();
                    if (divisor == 0.0) {
                        runtimeError("Division by zero");
                        push(Value::nil());
                    } else {
                        push(Value::number(a.asNumber() / divisor));
                    }
                } else if (!callBinaryMetamethod(a, b, "__div")) {
                    runtimeError("attempt to perform arithmetic on " + a.typeToString() + " and " + b.typeToString());
                }
                break;
            }

            case OpCode::OP_MOD: {
                Value b = pop();
                Value a = pop();
                if (a.isNumber() && b.isNumber()) {
                    push(Value::number(std::fmod(a.asNumber(), b.asNumber())));
                } else if (!callBinaryMetamethod(a, b, "__mod")) {
                    runtimeError("attempt to perform arithmetic on " + a.typeToString() + " and " + b.typeToString());
                }
                break;
            }

            case OpCode::OP_POW: {
                Value b = pop();
                Value a = pop();
                if (a.isNumber() && b.isNumber()) {
                    push(Value::number(std::pow(a.asNumber(), b.asNumber())));
                } else if (!callBinaryMetamethod(a, b, "__pow")) {
                    runtimeError("attempt to perform arithmetic on " + a.typeToString() + " and " + b.typeToString());
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
                        callValue(1, 1);
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
                        str = currentCoroutine_->chunk ? currentCoroutine_->chunk->getString(index) : currentCoroutine_->rootChunk->getString(index);
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

            case OpCode::OP_JUMP: {
                uint16_t offset = readByte() | (readByte() << 8);
                currentCoroutine_->ip += offset;
                break;
            }

            case OpCode::OP_JUMP_IF_FALSE: {
                uint16_t offset = readByte() | (readByte() << 8);
                if (peek(0).isFalsey()) {
                    currentCoroutine_->ip += offset;
                }
                break;
            }

            case OpCode::OP_LOOP: {
                uint16_t offset = readByte() | (readByte() << 8);
                currentCoroutine_->ip -= offset;
                break;
            }

            case OpCode::OP_CLOSURE: {
                uint8_t constantIndex = readByte();
                Value funcValue = currentCoroutine_->chunk->constants()[constantIndex];
                size_t funcIndex = funcValue.asFunctionIndex();
                // Look up function in current chunk (not currentCoroutine_->rootChunk) for nested functions
                FunctionObject* function = currentCoroutine_->chunk->getFunction(funcIndex);

                // Create closure
                size_t closureIndex = createClosure(function);
                ClosureObject* closure = getClosure(closureIndex);

                // Capture upvalues
                for (int i = 0; i < function->upvalueCount(); i++) {
                    uint8_t isLocal = readByte();
                    uint8_t index = readByte();

                    if (isLocal) {
                        // Capture local variable from current frame
                        size_t stackIndex = currentCoroutine_->frames.empty() ? index :
                            (currentFrame().stackBase + index);
                        size_t upvalueIndex = captureUpvalue(stackIndex);
                        closure->setUpvalue(i, upvalueIndex);
                    } else {
                        // Capture upvalue from enclosing closure
                        if (!currentCoroutine_->frames.empty()) {
                            size_t upvalueIndex = currentFrame().closure->getUpvalue(index);
                            closure->setUpvalue(i, upvalueIndex);
                        }
                    }
                }

                push(Value::closure(closureIndex));
                break;
            }

            case OpCode::OP_CALL: {
                uint8_t argCount = readByte();
                uint8_t retCount = readByte();  // Number of return values to keep (0 = all)
                if (!callValue(argCount, retCount)) {
                    return false;
                }
                break;
            }

            case OpCode::OP_CALL_MULTI: {
                uint8_t fixedArgCount = readByte();
                uint8_t retCount = readByte();
                // actual argCount = fixedArgs + lastResultCount
                int actualArgCount = static_cast<int>(fixedArgCount) + static_cast<int>(currentCoroutine_->lastResultCount);
                if (!callValue(actualArgCount, retCount)) {
                    return false;
                }
                break;
            }

            case OpCode::OP_RETURN_VALUE: {
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
                    size_t expected = static_cast<size_t>(expectedRetCount);
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
                size_t returnIP = currentFrame().ip;

                // Pop call frame
                currentCoroutine_->frames.pop_back();

                // Restore execution state
                currentCoroutine_->chunk = returnChunk;
                currentCoroutine_->ip = returnIP;

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
                size_t tableIndex = createTable();
                push(Value::table(tableIndex));
                break;
            }

            case OpCode::OP_GET_TABLE: {
                Value key = pop();
                Value tableValue = pop();

                if (!tableValue.isTable()) {
                    runtimeError("Attempt to index a non-table value");
                    push(Value::nil());
                    break;
                }

                size_t tableIndex = tableValue.asTableIndex();
                TableObject* table = getTable(tableIndex);
                if (table) {
                    Value value = table->get(key);
                    if (!value.isNil()) {
                        push(value);
                    } else {
                        // Check __index
                        Value indexMethod = getMetamethod(tableValue, "__index");
                        if (!indexMethod.isNil()) {
                            if (indexMethod.isFunction()) {
                                push(indexMethod);
                                push(tableValue);
                                push(key);
                                callValue(2, 1);
                            } else if (indexMethod.isTable()) {
                                TableObject* indexTable = getTable(indexMethod.asTableIndex());
                                push(indexTable->get(key));
                            } else {
                                push(Value::nil());
                            }
                        } else {
                            push(Value::nil());
                        }
                    }
                } else {
                    push(Value::nil());
                }
                break;
            }

            case OpCode::OP_SET_TABLE: {
                Value value = pop();
                Value key = pop();
                Value tableValue = pop();

                if (!tableValue.isTable()) {
                    runtimeError("Attempt to index a non-table value");
                    break;
                }

                TableObject* table = getTable(tableValue.asTableIndex());
                if (table->has(key)) {
                    table->set(key, value);
                } else {
                    Value newIndex = getMetamethod(tableValue, "__newindex");
                    if (newIndex.isNil()) {
                        table->set(key, value);
                    } else if (newIndex.isFunction()) {
                        push(newIndex);
                        push(tableValue);
                        push(key);
                        push(value);
                        callValue(3, 0);
                    } else if (newIndex.isTable()) {
                        TableObject* newIndexTable = getTable(newIndex.asTableIndex());
                        newIndexTable->set(key, value);
                    } else {
                        table->set(key, value);
                    }
                }
                break;
            }

            case OpCode::OP_IO_OPEN: {
                // Pop mode and filename strings
                Value modeVal = pop();
                Value filenameVal = pop();

                if ((!modeVal.isString() && !modeVal.isRuntimeString()) || 
                    (!filenameVal.isString() && !filenameVal.isRuntimeString())) {
                    runtimeError("io_open requires string arguments");
                    push(Value::nil());
                    break;
                }

                StringObject* filenameStr = nullptr;
                if (filenameVal.isRuntimeString()) {
                    filenameStr = getString(filenameVal.asStringIndex());
                } else {
                    filenameStr = currentCoroutine_->rootChunk->getString(filenameVal.asStringIndex());
                }

                StringObject* modeStr = nullptr;
                if (modeVal.isRuntimeString()) {
                    modeStr = getString(modeVal.asStringIndex());
                } else {
                    modeStr = currentCoroutine_->rootChunk->getString(modeVal.asStringIndex());
                }

                if (!filenameStr || !modeStr) {
                    runtimeError("Invalid string in io_open");
                    push(Value::nil());
                    break;
                }

                // Open file
                size_t fileIndex = openFile(filenameStr->chars(), modeStr->chars());
                FileObject* file = getFile(fileIndex);

                if (!file || !file->isOpen()) {
                    push(Value::nil());
                } else {
                    push(Value::file(fileIndex));
                }
                break;
            }

            case OpCode::OP_IO_WRITE: {
                // Pop data and file handle
                Value dataVal = pop();
                Value fileVal = pop();

                if (!fileVal.isFile()) {
                    runtimeError("io_write requires file handle");
                    break;
                }

                if (!dataVal.isString() && !dataVal.isRuntimeString()) {
                    runtimeError("io_write requires string data");
                    break;
                }

                FileObject* file = getFile(fileVal.asFileIndex());
                
                StringObject* dataStr = nullptr;
                if (dataVal.isRuntimeString()) {
                    dataStr = getString(dataVal.asStringIndex());
                } else {
                    dataStr = currentCoroutine_->rootChunk->getString(dataVal.asStringIndex());
                }

                if (!file || !dataStr) {
                    runtimeError("Invalid file or string in io_write");
                    break;
                }

                if (!file->write(dataStr->chars())) {
                    runtimeError("Failed to write to file");
                    push(Value::boolean(false));
                } else {
                    push(Value::boolean(true));
                }
                break;
            }

            case OpCode::OP_IO_READ: {
                // Pop file handle
                Value fileVal = pop();

                if (!fileVal.isFile()) {
                    runtimeError("io_read requires file handle");
                    push(Value::nil());
                    break;
                }

                FileObject* file = getFile(fileVal.asFileIndex());
                if (!file) {
                    runtimeError("Invalid file handle");
                    push(Value::nil());
                    break;
                }

                // Read entire file
                std::string content = file->readAll();
                size_t stringIndex = internString(content);
                push(Value::runtimeString(stringIndex));
                break;
            }

            case OpCode::OP_IO_CLOSE: {
                // Pop file handle
                Value fileVal = pop();

                if (!fileVal.isFile()) {
                    runtimeError("io_close requires file handle");
                    push(Value::nil());
                    break;
                }

                closeFile(fileVal.asFileIndex());
                push(Value::nil());
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
                    // Push exactly retCount values
                    for (int i = 0; i < (int)retCount; i++) {
                        if (i < (int)varargs.size()) {
                            push(varargs[i]);
                        } else {
                            push(Value::nil());
                        }
                    }
                    currentCoroutine_->lastResultCount = retCount;
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
                
                TableObject* table = getTable(tableValue.asTableIndex());
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
    return currentCoroutine_->chunk->at(currentCoroutine_->ip++);
}

Value VM::readConstant() {
    uint8_t index = readByte();
    Value constant = currentCoroutine_->chunk->getConstant(index);
    
    // If it's a compile-time string, intern it to get a runtime string
    // This ensures all strings in the VM use the same pool and can be compared by index/identity
    if (constant.isString() && !constant.isRuntimeString()) {
        StringObject* str = currentCoroutine_->chunk->getString(constant.asStringIndex());
        size_t runtimeIdx = internString(str->chars(), str->length());
        return Value::runtimeString(runtimeIdx);
    }
    
    return constant;
}

Value VM::getMetamethod(const Value& obj, const std::string& method) {
    if (obj.isTable()) {
        TableObject* table = getTable(obj.asTableIndex());
        Value mt = table->getMetatable();
        if (!mt.isNil() && mt.isTable()) {
            TableObject* meta = getTable(mt.asTableIndex());
            // Create a string value for lookup
            size_t idx = internString(method);
            Value key = Value::runtimeString(idx);
            return meta->get(key);
        }
    }
    return Value::nil();
}

bool VM::pcall(int argCount) {
    size_t prevFrames = currentCoroutine_->frames.size();
    size_t stackSizeBefore = currentCoroutine_->stack.size() - argCount - 1;

    bool prevPcall = inPcall_;
    inPcall_ = true;
    
    // callValue pops argCount and the function, and sets up a new frame if closure
    bool success = callValue(argCount, 0); // 0 means return all results
    
    if (!success) {
        // Error during call setup (e.g. invalid function, or error in native func)
        // Clean up stack
        while (currentCoroutine_->stack.size() > stackSizeBefore) {
            pop();
        }
        push(Value::boolean(false));
        push(Value::runtimeString(internString(lastErrorMessage_)));
        hadError_ = false; // Recovered
        inPcall_ = prevPcall;
        return true;
    }
    
    // If it was a closure, a new frame was pushed. We need to run it until it pops.
    if (currentCoroutine_->frames.size() > prevFrames) {
        success = run(prevFrames);
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
    } else {
        // Success. The results are on the stack.
        // We need to push `true` before the results.
        size_t resultCount = currentCoroutine_->lastResultCount;
        
        std::vector<Value> results;
        for (size_t i = 0; i < resultCount; i++) {
            results.push_back(pop());
        }
        std::reverse(results.begin(), results.end());
        
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
    size_t stackSizeBefore = currentCoroutine_->stack.size() - argCount - 1;

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
    bool success = callValue(argCount - 2, 0);
    
    if (success && currentCoroutine_->frames.size() > prevFrames) {
        success = run(prevFrames);
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
        
        // Push false, then call msgh(lastErrorMessage_)
        push(Value::boolean(false));
        
        // Push msgh and call it
        push(msgh);
        push(Value::runtimeString(internString(lastErrorMessage_)));
        
        hadError_ = false; // Prepare to call error handler
        if (callValue(1, 1)) {
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
        
        push(Value::boolean(true));
        for (const auto& res : results) {
            push(res);
        }
        currentCoroutine_->lastResultCount = resultCount + 1;
    }
    
    return true;
}

bool VM::callValue(int argCount, int retCount) {
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
            size_t expected = static_cast<size_t>(retCount);
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
        size_t closureIndex = callee.asClosureIndex();
        ClosureObject* closure = getClosure(closureIndex);

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
        frame.callerChunk = currentCoroutine_->chunk;
        frame.ip = currentCoroutine_->ip;
        frame.stackBase = currentCoroutine_->stack.size() - argCount;
        frame.retCount = retCount;
        frame.varargs = std::move(varargs);
        // std::cout << "DEBUG frame: closure=" << closureIndex << " base=" << frame.stackBase << " args=" << argCount << " size=" << currentCoroutine_->stack.size() << std::endl;
        currentCoroutine_->frames.push_back(frame);

        currentCoroutine_->chunk = function->chunk();
        currentCoroutine_->ip = 0;
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
             
             return callValue(argCount + 1, retCount);
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
    return callValue(2, 1);
}

Value VM::add(const Value& a, const Value& b) {
    if (!a.isNumber() || !b.isNumber()) {
        runtimeError("Operands must be numbers");
        return Value::nil();
    }
    return Value::number(a.asNumber() + b.asNumber());
}

Value VM::subtract(const Value& a, const Value& b) {
    if (!a.isNumber() || !b.isNumber()) {
        runtimeError("Operands must be numbers");
        return Value::nil();
    }
    return Value::number(a.asNumber() - b.asNumber());
}

Value VM::multiply(const Value& a, const Value& b) {
    if (!a.isNumber() || !b.isNumber()) {
        runtimeError("Operands must be numbers");
        return Value::nil();
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
    return Value::number(a.asNumber() / divisor);
}

Value VM::modulo(const Value& a, const Value& b) {
    if (!a.isNumber() || !b.isNumber()) {
        runtimeError("Operands must be numbers");
        return Value::nil();
    }
    return Value::number(std::fmod(a.asNumber(), b.asNumber()));
}

Value VM::power(const Value& a, const Value& b) {
    if (!a.isNumber() || !b.isNumber()) {
        runtimeError("Operands must be numbers");
        return Value::nil();
    }
    return Value::number(std::pow(a.asNumber(), b.asNumber()));
}

Value VM::concat(const Value& a, const Value& b) {
    std::string result = getStringValue(a) + getStringValue(b);
    size_t stringIdx = internString(result);
    return Value::runtimeString(stringIdx);
}

std::string VM::getStringValue(const Value& value) {
    if (value.isString()) {
        size_t index = value.asStringIndex();
        StringObject* str = nullptr;
        if (value.isRuntimeString()) {
            str = getString(index);
        } else {
            // Use current chunk if possible, otherwise fall back to root
            str = currentCoroutine_->chunk ? currentCoroutine_->chunk->getString(index) : currentCoroutine_->rootChunk->getString(index);
        }
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

void VM::runtimeError(const std::string& message) {
    lastErrorMessage_ = message;
    hadError_ = true;

    if (inPcall_) {
        // Do not print anything, just record the error
        return;
    }

    // Get line number from current instruction
    int line = currentCoroutine_->chunk->getLine(currentCoroutine_->ip - 1);
    uint8_t op = currentCoroutine_->chunk->at(currentCoroutine_->ip - 1);
    std::cout << "RUNTIME ERROR at line " << line << " (op " << (int)op << " " << opcodeName(static_cast<OpCode>(op)) << "): " << message << std::endl;
    
    std::cout << "Stack trace (bottom to top):" << std::endl;
    for (size_t i = 0; i < currentCoroutine_->stack.size(); i++) {
        std::cout << "  [" << i << "] " << currentCoroutine_->stack[i].typeToString() << ": " << currentCoroutine_->stack[i] << std::endl;
    }

    Log::error(message, line);
    hadError_ = true;
}

void VM::traceExecution() {
    // Print stack contents
    std::cout << "          ";
    for (const auto& value : currentCoroutine_->stack) {
        std::cout << "[ " << value << " ]";
    }
    std::cout << std::endl;

    // Disassemble current instruction
    currentCoroutine_->chunk->disassembleInstruction(currentCoroutine_->ip);
}

bool VM::runSource(const std::string& source, const std::string& name) {
    try {
        Lexer lexer(source);
        Parser parser(lexer);
        auto program = parser.parse();
        if (!program) return false;

        CodeGenerator codegen;
        auto chunk = codegen.generate(program.get());
        if (!chunk) return false;

#ifdef PRINT_CODE
        chunk->disassemble(name);
#endif

        // Create a FunctionObject to own the Chunk and keep it alive in the VM's function table
        FunctionObject* function = new FunctionObject(name, 0, std::move(chunk));
        registerFunction(function);

        // Run the chunk
        return run(*function->chunk());
    } catch (const std::exception& e) {
        std::cerr << "Error in runSource (" << name << "): " << e.what() << std::endl;
        return false;
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
