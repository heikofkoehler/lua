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
                    StringObject* str = rootChunk_->getString(index);
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
                push(funcValue);
                break;
            }

            case OpCode::OP_CALL: {
                uint8_t argCount = readByte();
                Value callee = peek(argCount);

                if (!callee.isFunctionObject()) {
                    runtimeError("Can only call functions");
                    break;
                }

                // Get function index from the value, then retrieve function from root chunk
                size_t funcIndex = callee.asFunctionIndex();
                FunctionObject* function = rootChunk_->getFunction(funcIndex);

                if (argCount != function->arity()) {
                    runtimeError("Expected " + std::to_string(function->arity()) +
                                 " arguments but got " + std::to_string(argCount));
                    break;
                }

                if (frames_.size() >= FRAMES_MAX) {
                    runtimeError("Stack overflow");
                    break;
                }

                // Create new call frame
                CallFrame frame;
                frame.function = function;
                frame.callerChunk = chunk_;  // Save current chunk
                frame.ip = ip_;  // Save current IP
                frame.stackBase = stack_.size() - argCount;
                frames_.push_back(frame);

                // Switch to function's chunk
                chunk_ = function->chunk();
                ip_ = 0;
                break;
            }

            case OpCode::OP_RETURN_VALUE: {
                Value result = pop();

                if (frames_.empty()) {
                    // Returning from main script - shouldn't happen normally
                    push(result);
                    return !hadError_;
                }

                // Pop all locals and arguments (down to stackBase)
                size_t stackBase = currentFrame().stackBase;
                while (stack_.size() > stackBase) {
                    pop();
                }

                // Also pop the function object itself (it's at stackBase - 1)
                pop();

                // Get return state before popping frame
                const Chunk* returnChunk = currentFrame().callerChunk;
                size_t returnIP = currentFrame().ip;

                // Pop call frame
                frames_.pop_back();

                // Restore execution state
                chunk_ = returnChunk;
                ip_ = returnIP;

                // Push return value (replaces where function was)
                push(result);
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
