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

struct lua_State;

// Native function signature
// Returns true on success, false on error
// Pops argCount arguments from stack, pushes results onto stack
class VM;
class JITCompiler;
using NativeFunction = bool (*)(VM* vm, int argCount);

class VM {
    friend class JITCompiler;
public:
    VM();
    ~VM();

    static VM* currentVM;

    // Stack size limits
    static constexpr size_t STACK_LIMIT = 1000000;
    static constexpr size_t STACK_MAX = 1000000 + 2000;
    static constexpr size_t FRAMES_LIMIT = 20000;
    static constexpr size_t FRAMES_MAX = 20000 + 200;

    // Execute a chunk of bytecode
    // Returns true if execution succeeded, false on error
    bool run(const FunctionObject& function);
    bool run(const FunctionObject& function, const std::vector<Value>& args);
    bool run();
    bool run(size_t targetFrameCount);
    
    // Execute a protected call
    bool pcall(int argCount);
    bool xpcall(int argCount);

    // Execute Lua source code
    bool runSource(const std::string& source, const std::string& name = "script");
    bool runSource(const std::string& source, const std::string& name, const std::vector<Value>& args);
    FunctionObject* compileSource(const std::string& source, const std::string& name = "script");

    // Reset VM state
    void reset();

    void internConstants(const FunctionObject& function);

    // Stack operations (for debugging/testing)
    const std::vector<Value>& stack() const { return currentCoroutine_->stack; }

    // Function table operations
    size_t registerFunction(FunctionObject* func);
    FunctionObject* getFunction(size_t index);

    // String interning operations
    StringObject* internString(const char* chars, size_t length, bool isConstant = false);
    StringObject* internString(const std::string& str, bool isConstant = false);
    StringObject* getString(size_t index);

    // Table operations
    TableObject* createTable(size_t nseq = 0, size_t nrec = 0);

    // Userdata operations
    class UserdataObject* createUserdata(void* data, int numUserValues = 1, bool isLight = false);

    // Closure operations
    ClosureObject* createClosure(FunctionObject* function);
    ClosureObject* createCClosure(NativeFunction nativeFunc, const std::vector<Value>& upvalues);
    ClosureObject* createCClosure(lua_CFunction cFunc, const std::vector<Value>& upvalues);

    // Coroutine operations
    CoroutineObject* createCoroutine(ClosureObject* closure);
    CoroutineObject* createCoroutine(const Value& func);
    bool resumeCoroutine(CoroutineObject* co);
    void closeCoroutine(CoroutineObject* co);

    // Upvalue operations
    UpvalueObject* captureUpvalue(size_t stackIndex);
    void closeUpvalues(size_t lastStackIndex, CoroutineObject* co = nullptr, const Value& error = Value::nil());
    void closeTBCVariables(size_t lastStackIndex, CoroutineObject* co = nullptr, const Value& error = Value::nil());
    void setupRootUpvalues(ClosureObject* closure, const Value& env = Value::nil(), bool hasEnv = false);

    // File operations
    FileObject* openFile(const std::string& filename, const std::string& mode);
    FileObject* createFile(FILE* f, const std::string& mode);
    FileObject* popen(const std::string& command, const std::string& mode);
    void closeFile(FileObject* file);

    // Socket operations
    SocketObject* createSocket(socket_t fd);
    void closeSocket(SocketObject* socket);

    // Native function operations
    size_t registerNativeFunction(const std::string& name, NativeFunction func);
    NativeFunction getNativeFunction(size_t index);
    const std::string& getNativeFunctionName(size_t index) const;
    const void* getNativeFunctionPointer(size_t index) const;
    void addNativeToTable(TableObject* table, const char* name, NativeFunction func);
    void initStandardLibrary();
    void runInitializationFrames();
    void setGlobal(const std::string& name, const Value& value);
    Value getGlobal(const std::string& name) const;
    void registerModule(const std::string& name, TableObject* module);

    // Root chunk access (for native functions to access compile-time strings)
    const Chunk* rootChunk() const { return currentCoroutine_->rootChunk; }

    // Trace execution
    void setTraceExecution(bool enable) { traceExecution_ = enable; }
    bool getTraceExecution() const { return traceExecution_; }

    // Public stack operations (for native functions to use)
    void push(const Value& value);
    Value pop();
    Value peek(size_t distance = 0) const;
    void runtimeError(const std::string& message, int level = 1);
    void runtimeError(const Value& errorObj, int level = 1);
    std::string getVarInfo(size_t opIp, int operandIndex);
    std::string getCallVarInfo(size_t opIp, int argCount);

    struct CallingFuncInfo {
        bool isMethod = false;
        std::string name;
        std::string namewhat;
    };
    CallingFuncInfo getCallingFuncInfo();
    CallingFuncInfo getFrameFuncInfo(int frameIndex, CoroutineObject* co = nullptr);
    std::string findGlobalFuncName(const Value& funcVal);
    bool argError(int argNum, const std::string& extramsg, const char* funcNameFallback = nullptr);
    bool typeError(int argNum, const std::string& expectedType, const Value& actualVal, int totalArgCount, const char* funcNameFallback = nullptr);

    // Access to globals (for base library)
    std::unordered_map<std::string, Value>& globals() { return globals_; }

    // Access to current coroutine
    CoroutineObject* currentCoroutine() { return currentCoroutine_; }
    CoroutineObject* mainCoroutine() { return mainCoroutine_; }
    CallFrame* getFrame(int level);

    // JIT related
    JITCompiler* jit() { return jit_.get(); }
    void setJitEnabled(bool enable) { jitEnabled_ = enable; }
    bool isJitEnabled() const { return jitEnabled_; }

    // JIT callback helpers
    static void jitGetTable(VM* vm, uint32_t nextIp);
    static void jitSetTable(VM* vm, uint32_t nextIp);
    static void jitNewTable(VM* vm);
    static void jitGetUpvalue(VM* vm, uint32_t index);
    static void jitSetUpvalue(VM* vm, uint32_t index);
    static void jitConcat(VM* vm, uint32_t nextIp);
    static void jitCloseUpvalues(VM* vm, uint32_t stackIndex);
    static void jitClosure(VM* vm, uint32_t constantIndex, uint32_t bytecodeOffset);
    static void jitCall(VM* vm, uint32_t argCount, uint32_t retCount, uint32_t nextIp);
    static void jitReturnValue(VM* vm, uint32_t count);
    static void jitEnsureStack(VM* vm, uint32_t needed);

    static void jitGetGlobal(VM* vm, uint32_t nameIndex);
    static void jitSetGlobal(VM* vm, uint32_t nameIndex);
    static void jitGetTabUp(VM* vm, uint32_t upIndex, uint32_t keyIndex, uint32_t nextIp);
    static void jitSetTabUp(VM* vm, uint32_t upIndex, uint32_t keyIndex, uint32_t nextIp);
    static void jitLen(VM* vm, uint32_t nextIp);
    
    static void jitAdd(VM* vm, uint32_t nextIp);
    static void jitSub(VM* vm, uint32_t nextIp);
    static void jitMul(VM* vm, uint32_t nextIp);
    static void jitDiv(VM* vm, uint32_t nextIp);
    static void jitMod(VM* vm, uint32_t nextIp);
    static void jitIDiv(VM* vm, uint32_t nextIp);
    static void jitPow(VM* vm, uint32_t nextIp);
    static void jitNeg(VM* vm, uint32_t nextIp);
    
    static void jitBand(VM* vm, uint32_t nextIp);
    static void jitBor(VM* vm, uint32_t nextIp);
    static void jitBxor(VM* vm, uint32_t nextIp);
    static void jitBnot(VM* vm, uint32_t nextIp);
    static void jitShl(VM* vm, uint32_t nextIp);
    static void jitShr(VM* vm, uint32_t nextIp);

    static void jitEq(VM* vm, uint32_t nextIp);
    static void jitLt(VM* vm, uint32_t nextIp);
    static void jitLe(VM* vm, uint32_t nextIp);


    // Registry for internal use (stable storage)
    void setRegistry(const std::string& key, const Value& value) { registry_[key] = value; }
    Value getRegistry(const std::string& key) const;
    const std::unordered_map<std::string, Value>& getRegistryMap() const { return registry_; }

    // Garbage collection
    enum class GCState {
        PAUSE,
        MARK,
        ATOMIC,
        SWEEP,
    };

    enum class GCMode {
        INCREMENTAL,
        GENERATIONAL,
    };

    size_t bytesAllocated() const { return bytesAllocated_; }
    void setMemoryLimit(size_t limit) { memoryLimit_ = limit; }
    void collectGarbage();
    void gcStep();
    void checkGC(size_t additionalBytes = 0);

    const std::string& sourceName() const { return sourceName_; }
    void setSourceName(const std::string& name) { sourceName_ = name; }

    GCMode gcMode() const { return gcMode_; }
    void setGCMode(GCMode mode) { gcMode_ = mode; }
    GCState gcState() const { return gcState_; }
    void setGCState(GCState state) { gcState_ = state; }
    GCObject* gcObjects() const { return gcObjects_; }
    bool gcEnabled() const { return gcEnabled_; }
    void setGCEnabled(bool enabled) { gcEnabled_ = enabled; }
    bool warnEnabled() const { return warnEnabled_; }
    void setWarnEnabled(bool enabled) { warnEnabled_ = enabled; }
    void close();
    bool isClosing() const { return isClosing_; }
    bool isRunningFinalizers() const { return isRunningFinalizers_; }
    void setupSigintHandler();
    const std::string& lastErrorMessage() const { return lastErrorMessage_; }
    const Value& lastErrorObject() const { return lastErrorObject_; }
    void setLastErrorObject(const Value& val) { lastErrorObject_ = val; }
    void markRoots();
    void markValue(const Value& value);
    void markObject(GCObject* object);
    void grayObject(GCObject* object);
    void sweep();
    void runFinalizers(bool force = false);
    void freeObject(GCObject* object);
    void addObject(GCObject* object);
    void collectWeakTables();
    void processWeakTables();
    void clearWeakValues();
    void clearWeakKeys();
    void removeUnmarkedWeakEntries();

    // Write barriers
    void writeBarrier(GCObject* object, const Value& value);
    void writeBarrier(GCObject* object, GCObject* value);
    void writeBarrierBackward(GCObject* object, GCObject* value);

    // Metamethod helper
    Value getMetamethod(const Value& obj, const std::string& method);
    Value getTable(const Value& tableVal, const Value& key);
    bool callBinaryMetamethod(const Value& a, const Value& b, const std::string& method);
    bool callValue(int argCount, int retCount, bool isTailCall = false, const char* metamethodName = nullptr, int extraArgs = 0);
    void callHook(const char* event, int line = -1, int ftransfer = 0, int ntransfer = 0);
    std::string getStringValue(const Value& value);
    std::string typeName(const Value& val);
    const void* valueToPointer(const Value& val) const;
    bool toLString(const Value& val, std::string& out);

    // Global metatables
    void setTypeMetatable(Value::Type type, const Value& mt);
    Value getTypeMetatable(Value::Type type) const;

    uint64_t* rngState() { return rngState_; }

    lua_State* currentL() const { return currentL_; }
    void setCurrentL(lua_State* L) { currentL_ = L; }

    Value makeInteger(int64_t val);
    bool toInteger(const Value& val, int64_t& outInt);
    bool toIntegerNoString(const Value& val, int64_t& outInt);
    static bool stringToInteger(const std::string& str, int64_t& outInt);
    static bool stringToNumber(const std::string& str, double& outNum, bool& isInt);
    static bool stringToNumber(const std::string& str, double& outNum, int64_t& outInt, bool& isInt);
    Value less(const Value& a, const Value& b);
    Value lessEqual(const Value& a, const Value& b);
    bool isHandlingError() const { return isHandlingError_; }

private:
    lua_State* currentL_ = nullptr;
    // GC-aware object allocation helper
    template<typename T, typename... Args>
    T* allocateObject(Args&&... args) {
        // First check with emergency GC if needed
        checkGC(sizeof(T)); 
        
        try {
            T* obj = new T(std::forward<Args>(args)...);
            addObject(obj);
            return obj;
        } catch (const std::bad_alloc&) {
            // Hard failure from OS - try one last ditch GC
            collectGarbage();
            try {
                T* obj = new T(std::forward<Args>(args)...);
                addObject(obj);
                return obj;
            } catch (const std::bad_alloc&) {
                runtimeError("not enough memory (hard allocation failure)");
                return nullptr;
            }
        }
    }

    // Coercion and parsing
    bool coerceToNumber(Value& val);

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
    bool isHandlingError_;        // TRUE if we're currently processing an error
    bool isRunningErrorHandler_ = false; // TRUE if we are currently executing a message handler
    bool isHandlingStackError_ = false;  // TRUE if currently handling a stack overflow error
    int errorHandlerDepth_ = 0;
    std::string lastErrorMessage_; // Last runtime error message
    Value lastErrorObject_ = Value::nil();
    std::vector<Value> errorHandlers_; // Active error handlers for xpcall / lua_pcall
    bool stdlibInitialized_;      // Whether standard library has been initialized
    uint64_t rngState_[4] = {0, 0, 0, 0};

    // JIT related
    std::unique_ptr<JITCompiler> jit_;
    bool jitEnabled_;
    bool isJitExecuting_ = false;

    // Garbage collection
    GCState gcState_;             // Current incremental GC state
    GCMode gcMode_ = GCMode::INCREMENTAL; // Current GC mode
    GCObject* gcObjects_;         // Linked list of all GC objects
    GCObject* toBeFinalized_;     // Linked list of objects to be finalized
    std::vector<GCObject*> grayStack_; // Worklist for marking
    std::vector<GCObject*> rememberedSet_; // Old objects pointing to young objects (Generational GC)
    size_t bytesAllocated_;       // Total bytes allocated
    size_t nextGC_;               // Threshold for next GC
    size_t memoryLimit_;          // Maximum bytes allowed before Emergency GC
    bool gcEnabled_;              // Can disable GC for debugging
    bool warnEnabled_;            // Lua 5.4 warning state
    bool isRunningFinalizers_ = false;
    bool isClosing_ = false;      // VM close in progress
public:
    volatile sig_atomic_t interrupted_ = 0; // SIGINT flag
private:
    std::vector<class TableObject*> weakTables_;

    std::unordered_map<std::string, Value> registry_; // Internal registry
    std::unordered_map<std::string, Value> globals_;  // Global variables
    std::string sourceName_ = "chunk"; // Current source name
    std::vector<FunctionObject*> functions_;  // Function pool (owned)
    std::vector<StringObject*> strings_;  // Compile-time string pool (owned)
    std::vector<Value> rootedConstants_;  // Interned constants rooted for GC
    std::unordered_map<std::string, StringObject*> runtimeStrings_; // Runtime string interning
    struct NativeFunctionInfo {
        std::string name;
        NativeFunction func;
    };
    std::vector<NativeFunctionInfo> nativeFunctions_;  // Native function table

    // Get current call frame
    CallFrame& currentFrame();
    const CallFrame& currentFrame() const;

    // Helper to read next byte
    uint8_t readByte();

    // Helper to read constant
    Value readConstant();
    Value getConstant(size_t index);

    // Trace execution (for debugging)
    void traceExecution();
};

#endif // LUA_VM_HPP
