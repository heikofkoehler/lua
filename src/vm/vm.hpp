#ifndef LUA_VM_HPP
#define LUA_VM_HPP

#include "common/common.hpp"
#include "vm/gc.hpp"
#include "value/value.hpp"
#include "value/function.hpp"
#include "value/closure.hpp"
#include "value/upvalue.hpp"
#include "value/string.hpp"
#include "value/table.hpp"
#include "value/file.hpp"
#include "value/socket.hpp"
#include "value/coroutine.hpp"
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
    uint8_t varargCount;        // Number of varargs passed to this function
    size_t varargBase;          // Stack location where varargs start
};

// Virtual Machine: Stack-based bytecode interpreter
// Executes compiled Lua bytecode

// Native function signature
// Returns true on success, false on error
// Pops argCount arguments from stack, pushes results onto stack
class VM;
using NativeFunction = bool (*)(VM* vm, int argCount);

class VM {
public:
    VM();
    ~VM();

    // Execute a chunk of bytecode
    // Returns true if execution succeeded, false on error
    bool run(const Chunk& chunk);
    bool run();

    // Execute Lua source code
    bool runSource(const std::string& source, const std::string& name = "script");

    // Reset VM state
    void reset();

    // Stack operations (for debugging/testing)
    const std::vector<Value>& stack() const { return currentCoroutine_->stack; }

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

    // Coroutine operations
    size_t createCoroutine(ClosureObject* closure);
    CoroutineObject* getCoroutine(size_t index);
    size_t getCoroutineIndex(CoroutineObject* co);
    bool resumeCoroutine(CoroutineObject* co);

    // Upvalue operations
    size_t captureUpvalue(size_t stackIndex);
    UpvalueObject* getUpvalue(size_t index);
    void closeUpvalues(size_t lastStackIndex);

    // File operations
    size_t openFile(const std::string& filename, const std::string& mode);
    FileObject* getFile(size_t index);
    void closeFile(size_t index);

    // Socket operations
    size_t createSocket(socket_t fd);
    size_t registerSocket(SocketObject* socket);
    SocketObject* getSocket(size_t index);
    void closeSocket(size_t index);

    // Native function operations
    size_t registerNativeFunction(const std::string& name, NativeFunction func);
    NativeFunction getNativeFunction(size_t index);
    void addNativeToTable(TableObject* table, const char* name, NativeFunction func);
    void initStandardLibrary();

    // Root chunk access (for native functions to access compile-time strings)
    const Chunk* rootChunk() const { return currentCoroutine_->rootChunk; }

    // Public stack operations (for native functions to use)
    void push(const Value& value);
    Value pop();
    Value peek(size_t distance = 0) const;
    void runtimeError(const std::string& message);

    // Access to globals (for base library)
    std::unordered_map<std::string, Value>& globals() { return globals_; }

    // Access to current coroutine
    CoroutineObject* currentCoroutine() { return currentCoroutine_; }

    // Garbage collection
    void collectGarbage();
    void markRoots();
    void markValue(const Value& value);
    void markObject(GCObject* object);
    void sweep();
    void freeObject(GCObject* object);
    void addObject(GCObject* object);

    // Metamethod helper
    Value getMetamethod(const Value& obj, const std::string& method);
    bool callBinaryMetamethod(const Value& a, const Value& b, const std::string& method);
    bool callValue(int argCount, int retCount);
    std::string getStringValue(const Value& value);

private:
    // Arithmetic operations
    Value add(const Value& a, const Value& b);
    Value subtract(const Value& a, const Value& b);
    Value multiply(const Value& a, const Value& b);
    Value divide(const Value& a, const Value& b);
    Value modulo(const Value& a, const Value& b);
    Value power(const Value& a, const Value& b);
    Value concat(const Value& a, const Value& b);
    Value negate(const Value& a);

    // Comparison operations
    Value equal(const Value& a, const Value& b);
    Value less(const Value& a, const Value& b);
    Value lessEqual(const Value& a, const Value& b);

    // Logical operations
    Value logicalNot(const Value& a);

    // Execution state
    std::vector<CoroutineObject*> coroutines_; // Coroutine pool (owns objects)
    CoroutineObject* mainCoroutine_;
    CoroutineObject* currentCoroutine_;
    bool hadError_;               // Error flag
    bool stdlibInitialized_;      // Whether standard library has been initialized

    // Garbage collection
    GCObject* gcObjects_;         // Linked list of all GC objects
    size_t bytesAllocated_;       // Total bytes allocated
    size_t nextGC_;               // Threshold for next GC
    bool gcEnabled_;              // Can disable GC for debugging

    std::unordered_map<std::string, Value> globals_;  // Global variables
    std::vector<FunctionObject*> functions_;  // Function table (owns function objects)
    std::vector<StringObject*> strings_;  // String pool (owns string objects, interned)
    std::unordered_map<uint32_t, size_t> stringIndices_;  // Hash to index mapping for interning
    std::vector<TableObject*> tables_;  // Table pool (owns table objects)
    std::vector<ClosureObject*> closures_;  // Closure pool (owns closure objects)
    std::vector<UpvalueObject*> upvalues_;  // Upvalue pool (owns upvalue objects)
    std::vector<FileObject*> files_;  // File pool (owns file objects)
    std::vector<SocketObject*> sockets_;  // Socket pool (owns socket objects)
    std::vector<NativeFunction> nativeFunctions_;  // Native function table

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
