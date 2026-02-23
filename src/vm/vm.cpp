#include "vm/vm.hpp"
#include <iostream>
#include <cmath>

// Forward declarations for stdlib registration
void registerStringLibrary(VM* vm, TableObject* stringTable);
void registerTableLibrary(VM* vm, TableObject* tableTable);
void registerMathLibrary(VM* vm, TableObject* mathTable);
void registerSocketLibrary(VM* vm, TableObject* socketTable);
void registerBaseLibrary(VM* vm);

VM::VM() : chunk_(nullptr), rootChunk_(nullptr), ip_(0), hadError_(false), stdlibInitialized_(false),
           gcObjects_(nullptr), bytesAllocated_(0), nextGC_(1024 * 1024), gcEnabled_(true) {
    stack_.reserve(STACK_MAX);
    frames_.reserve(FRAMES_MAX);
    // Standard library will be initialized on first run() call
}

void VM::reset() {
    stack_.clear();
    frames_.clear();
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
    openUpvalues_.clear();
    // Clean up files (VM owns them)
    for (auto* file : files_) {
        delete file;
    }
    files_.clear();
    ip_ = 0;
    hadError_ = false;
    chunk_ = nullptr;
    rootChunk_ = nullptr;
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

    // Trigger GC if needed
    bytesAllocated_ += sizeof(StringObject) + length;
    if (bytesAllocated_ > nextGC_) {
        collectGarbage();
    }

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
    TableObject* table = new TableObject();
    addObject(table);  // Register with GC
    size_t index = tables_.size();
    tables_.push_back(table);

    // Trigger GC if needed
    bytesAllocated_ += sizeof(TableObject);
    if (bytesAllocated_ > nextGC_) {
        collectGarbage();
    }

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
    ClosureObject* closure = new ClosureObject(function, function->upvalueCount());
    addObject(closure);  // Register with GC
    size_t index = closures_.size();
    closures_.push_back(closure);
    return index;
}

ClosureObject* VM::getClosure(size_t index) {
    if (index >= closures_.size()) {
        runtimeError("Invalid closure index");
        return nullptr;
    }
    return closures_[index];
}

size_t VM::captureUpvalue(size_t stackIndex) {
    // Check if upvalue already exists for this stack slot
    for (UpvalueObject* openUpvalue : openUpvalues_) {
        if (!openUpvalue->isClosed() && openUpvalue->stackIndex() == stackIndex) {
            // Find and return index
            for (size_t i = 0; i < upvalues_.size(); i++) {
                if (upvalues_[i] == openUpvalue) {
                    return i;
                }
            }
        }
    }

    // Create new upvalue
    UpvalueObject* upvalue = new UpvalueObject(stackIndex);
    addObject(upvalue);  // Register with GC
    size_t index = upvalues_.size();
    upvalues_.push_back(upvalue);

    // Insert into openUpvalues_ (keep sorted by stack index for efficient closing)
    auto it = openUpvalues_.begin();
    while (it != openUpvalues_.end() && (*it)->stackIndex() < stackIndex) {
        ++it;
    }
    openUpvalues_.insert(it, upvalue);

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
    auto it = openUpvalues_.begin();
    while (it != openUpvalues_.end()) {
        UpvalueObject* upvalue = *it;
        if (!upvalue->isClosed() && upvalue->stackIndex() >= lastStackIndex) {
            upvalue->close(stack_);
            it = openUpvalues_.erase(it);
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

    // Create 'socket' table
    size_t socketTableIdx = createTable();
    TableObject* socketTable = getTable(socketTableIdx);
    registerSocketLibrary(this, socketTable);
    globals_["socket"] = Value::table(socketTableIdx);
}

CallFrame& VM::currentFrame() {
    if (frames_.empty()) {
        throw RuntimeError("No active call frame");
    }
    return frames_.back();
}

const CallFrame& VM::currentFrame() const {
    if (frames_.empty()) {
        throw RuntimeError("No active call frame");
    }
    return frames_.back();
}

bool VM::run(const Chunk& chunk) {
    chunk_ = &chunk;
    rootChunk_ = &chunk;  // Save root chunk for function lookups
    ip_ = 0;
    hadError_ = false;

    // Initialize standard library on first run (needs chunk for string pool)
    if (!stdlibInitialized_) {
        initStandardLibrary();
        stdlibInitialized_ = true;
    }

#ifdef PRINT_CODE
    chunk.disassemble("script");
#endif

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
                const std::string& varName = chunk_->getIdentifier(nameIndex);
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
                const std::string& varName = chunk_->getIdentifier(nameIndex);
                globals_[varName] = peek(0);
                break;
            }

            case OpCode::OP_GET_LOCAL: {
                uint8_t slot = readByte();
                // Add stackBase offset if inside a function
                size_t actualSlot = frames_.empty() ? slot : (currentFrame().stackBase + slot);
                push(stack_[actualSlot]);
                break;
            }

            case OpCode::OP_SET_LOCAL: {
                uint8_t slot = readByte();
                // Add stackBase offset if inside a function
                size_t actualSlot = frames_.empty() ? slot : (currentFrame().stackBase + slot);
                stack_[actualSlot] = peek(0);
                break;
            }

            case OpCode::OP_GET_UPVALUE: {
                uint8_t upvalueIndex = readByte();
                if (!frames_.empty()) {
                    size_t uvIndex = currentFrame().closure->getUpvalue(upvalueIndex);
                    UpvalueObject* upvalue = getUpvalue(uvIndex);
                    if (upvalue) {
                        push(upvalue->get(stack_));
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
                if (!frames_.empty()) {
                    size_t uvIndex = currentFrame().closure->getUpvalue(upvalueIndex);
                    UpvalueObject* upvalue = getUpvalue(uvIndex);
                    if (upvalue) {
                        upvalue->set(stack_, peek(0));
                    }
                } else {
                    runtimeError("Upvalue access outside of closure");
                }
                break;
            }

            case OpCode::OP_CLOSE_UPVALUE: {
                // Close upvalue at top of stack
                closeUpvalues(stack_.size() - 1);
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
                } else if (!callBinaryMetamethod(a, b, "__le")) {
                    runtimeError("attempt to compare " + a.typeToString() + " and " + b.typeToString());
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
                        // Compile-time string from chunk pool
                        str = rootChunk_->getString(index);
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
                ip_ += offset;
                break;
            }

            case OpCode::OP_JUMP_IF_FALSE: {
                uint16_t offset = readByte() | (readByte() << 8);
                if (peek(0).isFalsey()) {
                    ip_ += offset;
                }
                break;
            }

            case OpCode::OP_LOOP: {
                uint16_t offset = readByte() | (readByte() << 8);
                ip_ -= offset;
                break;
            }

            case OpCode::OP_CLOSURE: {
                uint8_t constantIndex = readByte();
                Value funcValue = chunk_->constants()[constantIndex];
                size_t funcIndex = funcValue.asFunctionIndex();
                // Look up function in current chunk (not rootChunk_) for nested functions
                FunctionObject* function = chunk_->getFunction(funcIndex);

                // Create closure
                size_t closureIndex = createClosure(function);
                ClosureObject* closure = getClosure(closureIndex);

                // Capture upvalues
                for (int i = 0; i < function->upvalueCount(); i++) {
                    uint8_t isLocal = readByte();
                    uint8_t index = readByte();

                    if (isLocal) {
                        // Capture local variable from current frame
                        size_t stackIndex = frames_.empty() ? index :
                            (currentFrame().stackBase + index);
                        size_t upvalueIndex = captureUpvalue(stackIndex);
                        closure->setUpvalue(i, upvalueIndex);
                    } else {
                        // Capture upvalue from enclosing closure
                        if (!frames_.empty()) {
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
                callValue(argCount, retCount);
                break;
            }

            case OpCode::OP_RETURN_VALUE: {
                // Read the count of return values
                uint8_t count = readByte();

                // Pop all return values from the stack (in reverse order)
                std::vector<Value> returnValues;
                returnValues.reserve(count);
                for (int i = 0; i < count; i++) {
                    returnValues.push_back(pop());
                }
                // Reverse so they're in correct order
                std::reverse(returnValues.begin(), returnValues.end());

                if (frames_.empty()) {
                    // Returning from main script - shouldn't happen normally
                    for (const auto& value : returnValues) {
                        push(value);
                    }
                    return !hadError_;
                }

                // Get the expected return count from the call frame
                uint8_t expectedRetCount = currentFrame().retCount;

                // Adjust return values based on what caller expects
                if (expectedRetCount > 0 && returnValues.size() > expectedRetCount) {
                    // Keep only the first expectedRetCount values
                    returnValues.resize(expectedRetCount);
                } else if (expectedRetCount > 0 && returnValues.size() < expectedRetCount) {
                    // Pad with nils if we returned fewer than expected
                    while (returnValues.size() < expectedRetCount) {
                        returnValues.push_back(Value::nil());
                    }
                }
                // If expectedRetCount == 0, keep all values (no adjustment)

                // Close upvalues for locals that are going out of scope
                size_t stackBase = currentFrame().stackBase;
                closeUpvalues(stackBase);

                // Pop all locals and arguments (down to stackBase)
                while (stack_.size() > stackBase) {
                    pop();
                }

                // Also pop the closure object itself (it's at stackBase - 1)
                pop();

                // Get return state before popping frame
                const Chunk* returnChunk = currentFrame().callerChunk;
                size_t returnIP = currentFrame().ip;

                // Pop call frame
                frames_.pop_back();

                // Restore execution state
                chunk_ = returnChunk;
                ip_ = returnIP;

                // Push all return values (replaces where function was)
                for (const auto& value : returnValues) {
                    push(value);
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
                    filenameStr = rootChunk_->getString(filenameVal.asStringIndex());
                }

                StringObject* modeStr = nullptr;
                if (modeVal.isRuntimeString()) {
                    modeStr = getString(modeVal.asStringIndex());
                } else {
                    modeStr = rootChunk_->getString(modeVal.asStringIndex());
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
                    dataStr = rootChunk_->getString(dataVal.asStringIndex());
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
                // Push all varargs onto the stack
                if (frames_.empty()) {
                    runtimeError("Cannot access varargs outside of a function");
                    break;
                }

                CallFrame& frame = currentFrame();
                uint8_t varargCount = frame.varargCount;
                size_t varargBase = frame.varargBase;

                // Push all varargs
                for (uint8_t i = 0; i < varargCount; i++) {
                    push(stack_[varargBase + i]);
                }

                // If no varargs, push nil
                if (varargCount == 0) {
                    push(Value::nil());
                }

                break;
            }

            case OpCode::OP_RETURN:
                return !hadError_;

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
    if (stack_.size() >= STACK_MAX) {
        runtimeError("Stack overflow");
        return;
    }
    stack_.push_back(value);
}

Value VM::pop() {
    if (stack_.empty()) {
        runtimeError("Stack underflow");
        return Value::nil();
    }
    Value value = stack_.back();
    stack_.pop_back();
    return value;
}

Value VM::peek(size_t distance) const {
    if (distance >= stack_.size()) {
        return Value::nil();
    }
    return stack_[stack_.size() - 1 - distance];
}

uint8_t VM::readByte() {
    return chunk_->at(ip_++);
}

Value VM::readConstant() {
    uint8_t index = readByte();
    Value constant = chunk_->getConstant(index);
    
    // If it's a compile-time string, intern it to get a runtime string
    // This ensures all strings in the VM use the same pool and can be compared by index/identity
    if (constant.isString() && !constant.isRuntimeString()) {
        StringObject* str = chunk_->getString(constant.asStringIndex());
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

bool VM::callValue(int argCount, int retCount) {
    Value callee = peek(argCount);

    if (callee.isNativeFunction()) {
        size_t funcIndex = callee.asNativeFunctionIndex();
        NativeFunction func = getNativeFunction(funcIndex);

        if (!func) {
            runtimeError("Invalid native function");
            return false;
        }

        size_t funcPosition = stack_.size() - argCount - 1;

        if (!func(this, argCount)) {
            return false;
        }

        // After native call, stack is: [..., func, result1, result2, ...]
        size_t resultCount = stack_.size() - funcPosition - 1;

        std::vector<Value> results;
        results.reserve(resultCount);
        for (size_t i = 0; i < resultCount; i++) {
            results.push_back(pop());
        }

        pop(); // Pop function

        for (auto it = results.rbegin(); it != results.rend(); ++it) {
            push(*it);
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

        if (!hasVarargs && argCount != arity) {
            runtimeError("Expected " + std::to_string(arity) + " arguments but got " + std::to_string(argCount));
            return false;
        } else if (hasVarargs && argCount < arity) {
            runtimeError("Expected at least " + std::to_string(arity) + " arguments but got " + std::to_string(argCount));
            return false;
        }

        if (frames_.size() >= FRAMES_MAX) {
            runtimeError("Stack overflow");
            return false;
        }

        uint8_t varargCount = 0;
        size_t varargBase = 0;
        if (hasVarargs && argCount > arity) {
            varargCount = argCount - arity;
            varargBase = stack_.size() - varargCount;
        }

        CallFrame frame;
        frame.closure = closure;
        frame.callerChunk = chunk_;
        frame.ip = ip_;
        frame.stackBase = stack_.size() - argCount;
        frame.retCount = retCount;
        frame.varargCount = varargCount;
        frame.varargBase = varargBase;
        frames_.push_back(frame);

        chunk_ = function->chunk();
        ip_ = 0;
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
             // callee is at stack_.size() - 1 - argCount.
             
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

    runtimeError("Attempt to call a " + callee.typeToString() + " value");
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
            str = rootChunk_->getString(index);
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
    // Get line number from current instruction
    int line = chunk_->getLine(ip_ - 1);
    Log::error(message, line);
    hadError_ = true;
}

void VM::traceExecution() {
    // Print stack contents
    std::cout << "          ";
    for (const auto& value : stack_) {
        std::cout << "[ " << value << " ]";
    }
    std::cout << std::endl;

    // Disassemble current instruction
    chunk_->disassembleInstruction(ip_);
}

// ============================================================================
