#ifndef LUA_VM_HPP
#define LUA_VM_HPP

#include "common/common.hpp"
#include "value/value.hpp"
#include "value/function.hpp"
#include "compiler/chunk.hpp"
#include <vector>
#include <unordered_map>
#include <string>

// Call frame for function calls
struct CallFrame {
    FunctionObject* function;   // Function being executed
    const Chunk* callerChunk;   // Chunk to return to
    size_t ip;                  // Instruction pointer to return to
    size_t stackBase;           // Where this frame's locals start on value stack
};

// Virtual Machine: Stack-based bytecode interpreter
// Executes compiled Lua bytecode

class VM {
public:
    VM();

    // Execute a chunk of bytecode
    // Returns true if execution succeeded, false on error
    bool run(const Chunk& chunk);

    // Reset VM state
    void reset();

    // Stack operations (for debugging/testing)
    const std::vector<Value>& stack() const { return stack_; }

private:
    // Stack operations
    void push(const Value& value);
    Value pop();
    Value peek(size_t distance = 0) const;

    // Arithmetic operations
    Value add(const Value& a, const Value& b);
    Value subtract(const Value& a, const Value& b);
    Value multiply(const Value& a, const Value& b);
    Value divide(const Value& a, const Value& b);
    Value modulo(const Value& a, const Value& b);
    Value power(const Value& a, const Value& b);
    Value negate(const Value& a);

    // Comparison operations
    Value equal(const Value& a, const Value& b);
    Value less(const Value& a, const Value& b);
    Value lessEqual(const Value& a, const Value& b);

    // Logical operations
    Value logicalNot(const Value& a);

    // Runtime error reporting
    void runtimeError(const std::string& message);

    // Execution state
    const Chunk* chunk_;          // Current chunk being executed
    size_t ip_;                   // Instruction pointer
    std::vector<Value> stack_;    // Value stack
    std::unordered_map<std::string, Value> globals_;  // Global variables
    std::vector<CallFrame> frames_;  // Call stack
    bool hadError_;               // Error flag

    // Stack size limits
    static constexpr size_t STACK_MAX = 256;
    static constexpr size_t FRAMES_MAX = 64;

    // Get current call frame
    CallFrame& currentFrame();
    const CallFrame& currentFrame() const;

    // Helper to read next byte
    uint8_t readByte();

    // Helper to read constant
    Value readConstant();

    // Trace execution (for debugging)
    void traceExecution();
};

#endif // LUA_VM_HPP
