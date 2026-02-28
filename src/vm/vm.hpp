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

    static VM* currentVM;

    // Execute a chunk of bytecode
    // Returns true if execution succeeded, false on error
    bool run(const FunctionObject& function);
    bool run();
    bool run(size_t targetFrameCount);
    
    // Execute a protected call
    bool pcall(int argCount);
    bool xpcall(int argCount);

    // Execute Lua source code
    bool runSource(const std::string& source, const std::string& name = "script");
    FunctionObject* compileSource(const std::string& source, const std::string& name = "script");

    // Reset VM state
    void reset();

    // Stack operations (for debugging/testing)
    const std::vector<Value>& stack() const { return currentCoroutine_->stack; }

    // Function table operations
    size_t registerFunction(FunctionObject* func);
    FunctionObject* getFunction(size_t index);

    // String interning operations
    StringObject* internString(const char* chars, size_t length);
    StringObject* internString(const std::string& str);
    StringObject* getString(size_t index);

    // Table operations
    TableObject* createTable();

    // Userdata operations
    class UserdataObject* createUserdata(void* data);

    // Closure operations
    ClosureObject* createClosure(FunctionObject* function);

    // Coroutine operations
    CoroutineObject* createCoroutine(ClosureObject* closure);
    bool resumeCoroutine(CoroutineObject* co);

    // Upvalue operations
    UpvalueObject* captureUpvalue(size_t stackIndex);
    void closeUpvalues(size_t lastStackIndex);

    // File operations
    FileObject* openFile(const std::string& filename, const std::string& mode);
    void closeFile(FileObject* file);

    // Socket operations
    SocketObject* createSocket(socket_t fd);
    void closeSocket(SocketObject* socket);

    // Native function operations
    size_t registerNativeFunction(const std::string& name, NativeFunction func);
    NativeFunction getNativeFunction(size_t index);
    void addNativeToTable(TableObject* table, const char* name, NativeFunction func);
    void initStandardLibrary();
    void runInitializationFrames();
    void setGlobal(const std::string& name, const Value& value);

    // Root chunk access (for native functions to access compile-time strings)
    const Chunk* rootChunk() const { return currentCoroutine_->rootChunk; }

    // Trace execution
    void setTraceExecution(bool enable) { traceExecution_ = enable; }
    bool getTraceExecution() const { return traceExecution_; }

    // Public stack operations (for native functions to use)
    void push(const Value& value);
    Value pop();
    Value peek(size_t distance = 0) const;
    void runtimeError(const std::string& message);

    // Access to globals (for base library)
    std::unordered_map<std::string, Value>& globals() { return globals_; }

    // Access to current coroutine
    CoroutineObject* currentCoroutine() { return currentCoroutine_; }
    CallFrame* getFrame(int level);

    // Registry for internal use (stable storage)
    void setRegistry(const std::string& key, const Value& value) { registry_[key] = value; }
    Value getRegistry(const std::string& key) const;

    // Garbage collection
    size_t bytesAllocated() const { return bytesAllocated_; }
    void collectGarbage();
    void markRoots();
    void markValue(const Value& value);
    void markObject(GCObject* object);
    void sweep();
    void freeObject(GCObject* object);
    void addObject(GCObject* object);
    void processWeakTables();
    void removeUnmarkedWeakEntries();

    // Metamethod helper
    Value getMetamethod(const Value& obj, const std::string& method);
    bool callBinaryMetamethod(const Value& a, const Value& b, const std::string& method);
    bool callValue(int argCount, int retCount, bool isTailCall = false);
    void callHook(const char* event, int line = -1);
    std::string getStringValue(const Value& value);

    // Global metatables
    void setTypeMetatable(Value::Type type, const Value& mt);
    Value getTypeMetatable(Value::Type type) const;

private:
    // Arithmetic operations
    Value add(const Value& a, const Value& b);
    Value subtract(const Value& a, const Value& b);
    Value multiply(const Value& a, const Value& b);
    Value divide(const Value& a, const Value& b);
    Value integerDivide(const Value& a, const Value& b);
    Value modulo(const Value& a, const Value& b);
    Value power(const Value& a, const Value& b);
    Value bitwiseAnd(const Value& a, const Value& b);
    Value bitwiseOr(const Value& a, const Value& b);
    Value bitwiseXor(const Value& a, const Value& b);
    Value shiftLeft(const Value& a, const Value& b);
    Value shiftRight(const Value& a, const Value& b);
    Value concat(const Value& a, const Value& b);
    Value negate(const Value& a);
    Value bitwiseNot(const Value& a);

    // Comparison operations
    Value equal(const Value& a, const Value& b);
    Value less(const Value& a, const Value& b);
    Value lessEqual(const Value& a, const Value& b);

    // Logical operations
    Value logicalNot(const Value& a);

    // Execution state
    bool traceExecution_ = false;  // Whether to print every instruction
    Value typeMetatables_[Value::NUM_TYPES];
    std::vector<CoroutineObject*> coroutines_; // Coroutine pool (owns objects)
    CoroutineObject* mainCoroutine_;
    CoroutineObject* currentCoroutine_;
    bool hadError_;               // Error flag
    bool inPcall_;                // Whether we are inside a protected call
    std::string lastErrorMessage_; // Last runtime error message
    bool stdlibInitialized_;      // Whether standard library has been initialized

    // Garbage collection
    GCObject* gcObjects_;         // Linked list of all GC objects
    size_t bytesAllocated_;       // Total bytes allocated
    size_t nextGC_;               // Threshold for next GC
    bool gcEnabled_;              // Can disable GC for debugging
    std::vector<class TableObject*> weakTables_;

    std::unordered_map<std::string, Value> registry_; // Internal registry
    std::unordered_map<std::string, Value> globals_;  // Global variables
    std::vector<FunctionObject*> functions_;  // Function pool (owned)
    std::vector<StringObject*> strings_;  // Compile-time string pool (owned)
    std::unordered_map<std::string, StringObject*> runtimeStrings_; // Runtime string interning
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
