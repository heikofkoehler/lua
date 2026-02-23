#ifndef LUA_COROUTINE_HPP
#define LUA_COROUTINE_HPP

#include "vm/gc.hpp"
#include "value/value.hpp"
#include "compiler/chunk.hpp"
#include <vector>

// Forward declaration
struct CallFrame;
class UpvalueObject;

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
          ip(0), chunk(nullptr), rootChunk(nullptr), 
          status(Status::SUSPENDED), yieldCount(0), caller(nullptr) {
        stack.reserve(256);
        frames.reserve(64);
    }

    // Coroutine state
    std::vector<Value> stack;
    std::vector<CallFrame> frames;
    size_t ip;
    const Chunk* chunk;
    const Chunk* rootChunk;
    std::vector<UpvalueObject*> openUpvalues;
    Status status;
    size_t yieldCount;
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
