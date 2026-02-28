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

    // GC interface
    void markReferences() override;

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
