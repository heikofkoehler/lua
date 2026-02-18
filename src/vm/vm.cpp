#include "vm/vm.hpp"
#include <iostream>
#include <cmath>

VM::VM() : chunk_(nullptr), ip_(0), hadError_(false) {
    stack_.reserve(STACK_MAX);
}

void VM::reset() {
    stack_.clear();
    ip_ = 0;
    hadError_ = false;
    chunk_ = nullptr;
}

bool VM::run(const Chunk& chunk) {
    chunk_ = &chunk;
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
                push(stack_[slot]);
                break;
            }

            case OpCode::OP_SET_LOCAL: {
                uint8_t slot = readByte();
                stack_[slot] = peek(0);
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
                std::cout << value << std::endl;
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
