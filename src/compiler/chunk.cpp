#include "compiler/chunk.hpp"
#include <iostream>
#include <iomanip>

void Chunk::write(uint8_t byte, int line) {
    code_.push_back(byte);
    lines_.push_back(line);
}

size_t Chunk::addConstant(const Value& value) {
    constants_.push_back(value);
    return constants_.size() - 1;
}

int Chunk::getLine(size_t offset) const {
    if (offset >= lines_.size()) {
        return -1;
    }
    return lines_[offset];
}

void Chunk::disassemble(const std::string& name) const {
    std::cout << "== " << name << " ==" << std::endl;

    for (size_t offset = 0; offset < code_.size();) {
        offset = disassembleInstruction(offset);
    }
}

size_t Chunk::disassembleInstruction(size_t offset) const {
    std::cout << std::setfill('0') << std::setw(4) << offset << " ";

    // Print line number
    if (offset > 0 && lines_[offset] == lines_[offset - 1]) {
        std::cout << "   | ";
    } else {
        std::cout << std::setw(4) << lines_[offset] << " ";
    }

    uint8_t instruction = code_[offset];
    OpCode op = static_cast<OpCode>(instruction);

    switch (op) {
        case OpCode::OP_CONSTANT:
            return constantInstruction("OP_CONSTANT", offset);

        case OpCode::OP_NIL:
            return simpleInstruction("OP_NIL", offset);
        case OpCode::OP_TRUE:
            return simpleInstruction("OP_TRUE", offset);
        case OpCode::OP_FALSE:
            return simpleInstruction("OP_FALSE", offset);

        case OpCode::OP_ADD:
            return simpleInstruction("OP_ADD", offset);
        case OpCode::OP_SUB:
            return simpleInstruction("OP_SUB", offset);
        case OpCode::OP_MUL:
            return simpleInstruction("OP_MUL", offset);
        case OpCode::OP_DIV:
            return simpleInstruction("OP_DIV", offset);
        case OpCode::OP_MOD:
            return simpleInstruction("OP_MOD", offset);
        case OpCode::OP_POW:
            return simpleInstruction("OP_POW", offset);

        case OpCode::OP_NEG:
            return simpleInstruction("OP_NEG", offset);
        case OpCode::OP_NOT:
            return simpleInstruction("OP_NOT", offset);

        case OpCode::OP_EQUAL:
            return simpleInstruction("OP_EQUAL", offset);
        case OpCode::OP_LESS:
            return simpleInstruction("OP_LESS", offset);
        case OpCode::OP_LESS_EQUAL:
            return simpleInstruction("OP_LESS_EQUAL", offset);
        case OpCode::OP_GREATER:
            return simpleInstruction("OP_GREATER", offset);
        case OpCode::OP_GREATER_EQUAL:
            return simpleInstruction("OP_GREATER_EQUAL", offset);

        case OpCode::OP_PRINT:
            return simpleInstruction("OP_PRINT", offset);
        case OpCode::OP_POP:
            return simpleInstruction("OP_POP", offset);
        case OpCode::OP_RETURN:
            return simpleInstruction("OP_RETURN", offset);

        default:
            std::cout << "Unknown opcode " << static_cast<int>(instruction) << std::endl;
            return offset + 1;
    }
}

size_t Chunk::simpleInstruction(const char* name, size_t offset) const {
    std::cout << name << std::endl;
    return offset + 1;
}

size_t Chunk::constantInstruction(const char* name, size_t offset) const {
    uint8_t constantIndex = code_[offset + 1];
    std::cout << std::left << std::setw(16) << name
              << std::right << std::setw(4) << static_cast<int>(constantIndex)
              << " '";
    constants_[constantIndex].print(std::cout);
    std::cout << "'" << std::endl;
    return offset + 2;
}
