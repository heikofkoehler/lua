#ifndef LUA_CHUNK_HPP
#define LUA_CHUNK_HPP

#include "common/common.hpp"
#include "value/value.hpp"
#include "vm/opcode.hpp"
#include <vector>

// Chunk: A sequence of bytecode instructions with associated metadata
// Represents a compiled unit of Lua code (function, script, etc.)

class Chunk {
public:
    Chunk() = default;

    // Write a byte to the chunk
    void write(uint8_t byte, int line);

    // Add a constant to the constant pool
    // Returns the index of the constant in the pool
    size_t addConstant(const Value& value);

    // Access bytecode
    const std::vector<uint8_t>& code() const { return code_; }
    uint8_t at(size_t offset) const { return code_.at(offset); }
    size_t size() const { return code_.size(); }

    // Access constants
    const std::vector<Value>& constants() const { return constants_; }
    const Value& getConstant(size_t index) const { return constants_.at(index); }

    // Get line number for instruction at offset
    int getLine(size_t offset) const;

    // Disassemble for debugging
    void disassemble(const std::string& name) const;
    size_t disassembleInstruction(size_t offset) const;

private:
    std::vector<uint8_t> code_;        // Bytecode instructions
    std::vector<Value> constants_;     // Constant pool
    std::vector<int> lines_;           // Line numbers (parallel to code_)

    // Helper for disassembly
    size_t simpleInstruction(const char* name, size_t offset) const;
    size_t constantInstruction(const char* name, size_t offset) const;
};

#endif // LUA_CHUNK_HPP
