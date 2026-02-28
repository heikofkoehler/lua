#ifndef LUA_COROUTINE_HPP
#define LUA_COROUTINE_HPP

#include "vm/gc.hpp"
#include "value/value.hpp"
#include "compiler/chunk.hpp"
#include <vector>

// Forward declaration
class ClosureObject;
class UpvalueObject;

struct CallFrame {
    ClosureObject* closure;     // Closure being executed (function + upvalues)
    const Chunk* chunk;         // Chunk being executed in this frame
    const Chunk* callerChunk;   // Chunk to return to
    size_t ip;                  // Instruction pointer to return to
    size_t stackBase;           // Where this frame's locals start on value stack
    uint8_t retCount;           // Number of return values expected (0 = all, 1+ = that many)
    std::vector<Value> varargs; // Varargs passed to this function
    bool isPcall = false;       // TRUE if this frame is a pcall boundary
    bool isHook = false;        // TRUE if this frame is a debug hook
};

class CoroutineObject : public GCObject {
public:
    enum class Status {
        RUNNING,
        SUSPENDED,
        NORMAL,
        DEAD
    };

    CoroutineObject()
        : GCObject(GCObject::Type::COROUTINE), 
          chunk(nullptr), rootChunk(nullptr), 
          status(Status::SUSPENDED), yieldCount(0), retCount(0), lastResultCount(0), caller(nullptr) {
        stack.reserve(256);
        frames.reserve(64);
    }

    // Coroutine state
    std::vector<Value> stack;
    std::vector<CallFrame> frames;
    const Chunk* chunk;
    const Chunk* rootChunk;
    std::vector<UpvalueObject*> openUpvalues;
    Status status;
    size_t yieldCount;
    uint8_t retCount; // Number of values expected to be returned by yield
    size_t lastResultCount; // Number of results from last multires call
    std::vector<Value> yieldedValues;
    CoroutineObject* caller; // The coroutine that resumed this one

    // Hooking support
    Value hook = Value::nil();
    int hookMask = 0;
    int hookCount = 0;
    int baseHookCount = 0;
    bool inHook = false; // Prevent recursive hook calls
    int lastLine = -1;   // For line hooks

    // Hook masks
    static constexpr int MASK_CALL = 1 << 0;
    static constexpr int MASK_RET  = 1 << 1;
    static constexpr int MASK_LINE = 1 << 2;
    static constexpr int MASK_COUNT = 1 << 3;

    // GC interface
    void markReferences() override;

    size_t size() const override {
        return sizeof(CoroutineObject) + 
               stack.capacity() * sizeof(Value) + 
               frames.capacity() * sizeof(CallFrame) + 
               openUpvalues.capacity() * sizeof(UpvalueObject*) + 
               yieldedValues.capacity() * sizeof(Value);
    }

    const char* statusToString() const {
        switch (status) {
            case Status::RUNNING: return "running";
            case Status::SUSPENDED: return "suspended";
            case Status::NORMAL: return "normal";
            case Status::DEAD: return "dead";
            default: return "unknown";
        }
    }
};

#endif // LUA_COROUTINE_HPP
