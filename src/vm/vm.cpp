#include "vm/vm.hpp"
#include <iostream>
#include <cmath>

VM::VM() : chunk_(nullptr), rootChunk_(nullptr), ip_(0), hadError_(false) {
    stack_.reserve(STACK_MAX);
    frames_.reserve(FRAMES_MAX);
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
        if (strings_[index]->equals(chars, length)) {
            return index;
        }
    }

    // String doesn't exist, create and intern it
    StringObject* str = new StringObject(chars, length);
    size_t index = strings_.size();
    strings_.push_back(str);
    stringIndices_[hash] = index;
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
    size_t index = tables_.size();
    tables_.push_back(table);
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
                push(add(a, b));
                break;
            }

            case OpCode::OP_SUB: {
                Value b = pop();
                Value a = pop();
                push(subtract(a, b));
                break;
            }

            case OpCode::OP_MUL: {
                Value b = pop();
                Value a = pop();
                push(multiply(a, b));
                break;
            }

            case OpCode::OP_DIV: {
                Value b = pop();
                Value a = pop();
                push(divide(a, b));
                break;
            }

            case OpCode::OP_MOD: {
                Value b = pop();
                Value a = pop();
                push(modulo(a, b));
                break;
            }

            case OpCode::OP_POW: {
                Value b = pop();
                Value a = pop();
                push(power(a, b));
                break;
            }

            case OpCode::OP_NEG: {
                Value a = pop();
                push(negate(a));
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
                push(equal(a, b));
                break;
            }

            case OpCode::OP_LESS: {
                Value b = pop();
                Value a = pop();
                push(less(a, b));
                break;
            }

            case OpCode::OP_LESS_EQUAL: {
                Value b = pop();
                Value a = pop();
                push(lessEqual(a, b));
                break;
            }

            case OpCode::OP_GREATER: {
                Value b = pop();
                Value a = pop();
                push(Value::boolean(a.asNumber() > b.asNumber()));
                break;
            }

            case OpCode::OP_GREATER_EQUAL: {
                Value b = pop();
                Value a = pop();
                push(Value::boolean(a.asNumber() >= b.asNumber()));
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
                Value callee = peek(argCount);

                if (!callee.isClosure()) {
                    runtimeError("Can only call closures");
                    break;
                }

                // Get closure index from the value, then retrieve closure
                size_t closureIndex = callee.asClosureIndex();
                ClosureObject* closure = getClosure(closureIndex);

                if (!closure) {
                    runtimeError("Invalid closure");
                    break;
                }

                FunctionObject* function = closure->function();

                // Check argument count
                int arity = function->arity();
                bool hasVarargs = function->hasVarargs();

                if (!hasVarargs && argCount != arity) {
                    runtimeError("Expected " + std::to_string(arity) +
                                 " arguments but got " + std::to_string(argCount));
                    break;
                } else if (hasVarargs && argCount < arity) {
                    runtimeError("Expected at least " + std::to_string(arity) +
                                 " arguments but got " + std::to_string(argCount));
                    break;
                }

                if (frames_.size() >= FRAMES_MAX) {
                    runtimeError("Stack overflow");
                    break;
                }

                // Calculate varargs if function accepts them
                uint8_t varargCount = 0;
                size_t varargBase = 0;
                if (hasVarargs && argCount > arity) {
                    varargCount = argCount - arity;
                    varargBase = stack_.size() - varargCount;
                }

                // Create new call frame
                CallFrame frame;
                frame.closure = closure;
                frame.callerChunk = chunk_;  // Save current chunk
                frame.ip = ip_;  // Save current IP
                frame.stackBase = stack_.size() - argCount;
                frame.retCount = retCount;  // Store expected return count
                frame.varargCount = varargCount;
                frame.varargBase = varargBase;
                frames_.push_back(frame);

                // Switch to function's chunk
                chunk_ = function->chunk();
                ip_ = 0;
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
                    push(value);
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

                size_t tableIndex = tableValue.asTableIndex();
                TableObject* table = getTable(tableIndex);
                if (table) {
                    table->set(key, value);
                }
                // Note: SET_TABLE doesn't leave anything on stack
                break;
            }

            case OpCode::OP_IO_OPEN: {
                // Pop mode and filename strings
                Value modeVal = pop();
                Value filenameVal = pop();

                if (!modeVal.isString() || !filenameVal.isString()) {
                    runtimeError("io_open requires string arguments");
                    push(Value::nil());
                    break;
                }

                // Get actual strings from chunk's string pool
                StringObject* filenameStr = rootChunk_->getString(filenameVal.asStringIndex());
                StringObject* modeStr = rootChunk_->getString(modeVal.asStringIndex());

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

                if (!dataVal.isString()) {
                    runtimeError("io_write requires string data");
                    break;
                }

                FileObject* file = getFile(fileVal.asFileIndex());
                StringObject* dataStr = rootChunk_->getString(dataVal.asStringIndex());

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
    return chunk_->getConstant(index);
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

Value VM::negate(const Value& a) {
    if (!a.isNumber()) {
        runtimeError("Operand must be a number");
        return Value::nil();
    }
    return Value::number(-a.asNumber());
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
