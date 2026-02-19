#ifndef LUA_VM_HPP
#define LUA_VM_HPP

#include "common/common.hpp"
#include "value/value.hpp"
#include "value/function.hpp"
#include "value/closure.hpp"
#include "value/upvalue.hpp"
#include "value/string.hpp"
#include "value/table.hpp"
#include "value/file.hpp"
#include "compiler/chunk.hpp"
#include <vector>
#include <unordered_map>
#include <string>

// Call frame for function calls
struct CallFrame {
    ClosureObject* closure;     // Closure being executed (function + upvalues)
    const Chunk* callerChunk;   // Chunk to return to
    size_t ip;                  // Instruction pointer to return to
    size_t stackBase;           // Where this frame's locals start on value stack
    uint8_t retCount;           // Number of return values expected (0 = all, 1+ = that many)
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

    // Function table operations
    size_t registerFunction(FunctionObject* func);
    FunctionObject* getFunction(size_t index);

    // String interning operations
    size_t internString(const char* chars, size_t length);
    size_t internString(const std::string& str);
    StringObject* getString(size_t index);

    // Table operations
    size_t createTable();
    TableObject* getTable(size_t index);

    // Closure operations
    size_t createClosure(FunctionObject* function);
    ClosureObject* getClosure(size_t index);

    // Upvalue operations
    size_t captureUpvalue(size_t stackIndex);
    UpvalueObject* getUpvalue(size_t index);
    void closeUpvalues(size_t lastStackIndex);

    // File operations
    size_t openFile(const std::string& filename, const std::string& mode);
    FileObject* getFile(size_t index);
    void closeFile(size_t index);

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
    const Chunk* rootChunk_;      // Root chunk (for function lookups)
    size_t ip_;                   // Instruction pointer
    std::vector<Value> stack_;    // Value stack
    std::unordered_map<std::string, Value> globals_;  // Global variables
    std::vector<CallFrame> frames_;  // Call stack
    std::vector<FunctionObject*> functions_;  // Function table (owns function objects)
    std::vector<StringObject*> strings_;  // String pool (owns string objects, interned)
    std::unordered_map<uint32_t, size_t> stringIndices_;  // Hash to index mapping for interning
    std::vector<TableObject*> tables_;  // Table pool (owns table objects)
    std::vector<ClosureObject*> closures_;  // Closure pool (owns closure objects)
    std::vector<UpvalueObject*> upvalues_;  // Upvalue pool (owns upvalue objects)
    std::vector<UpvalueObject*> openUpvalues_;  // Open upvalues (sorted by stack index)
    std::vector<FileObject*> files_;  // File pool (owns file objects)
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
