#ifndef LUA_CHUNK_HPP
#define LUA_CHUNK_HPP

#include "common/common.hpp"
#include "value/value.hpp"
#include "vm/opcode.hpp"
#include <vector>

// Forward declaration
class FunctionObject;

// Chunk: A sequence of bytecode instructions with associated metadata
// Represents a compiled unit of Lua code (function, script, etc.)

class Chunk {
public:
    Chunk() = default;
    ~Chunk();  // Destructor to clean up function objects

    // Write a byte to the chunk
    void write(uint8_t byte, int line);

    // Add a constant to the constant pool
    // Returns the index of the constant in the pool
    size_t addConstant(const Value& value);

    // Add an identifier (variable name) to the identifier pool
    // Returns the index of the identifier
    size_t addIdentifier(const std::string& name);
    const std::string& getIdentifier(size_t index) const;

    // Add a function to the function pool
    // Returns the index of the function
    size_t addFunction(FunctionObject* func);
    FunctionObject* getFunction(size_t index) const;

    // Access bytecode
    const std::vector<uint8_t>& code() const { return code_; }
    std::vector<uint8_t>& code() { return code_; }  // Non-const for patching jumps
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
    std::vector<std::string> identifiers_;  // Identifier pool (variable names)
    std::vector<FunctionObject*> functions_;  // Function pool (owned)
    std::vector<int> lines_;           // Line numbers (parallel to code_)

    // Helper for disassembly
    size_t simpleInstruction(const char* name, size_t offset) const;
    size_t constantInstruction(const char* name, size_t offset) const;
    size_t jumpInstruction(const char* name, int sign, size_t offset) const;
    size_t byteInstruction(const char* name, size_t offset) const;
};

#endif // LUA_CHUNK_HPP
