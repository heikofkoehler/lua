#ifndef LUA_GC_HPP
#define LUA_GC_HPP

#include <cstddef>

// Base class for all garbage-collected objects
// Uses intrusive linked list for tracking all allocated objects
class GCObject {
public:
    enum class Type {
        STRING,
        TABLE,
        CLOSURE,
        UPVALUE,
        FILE,
        SOCKET,
        COROUTINE
    };

    GCObject(Type type) : type_(type), isMarked_(false), next_(nullptr) {}
    virtual ~GCObject() = default;

    // Mark this object as reachable
    void mark() { isMarked_ = true; }

    // Check if object is marked
    bool isMarked() const { return isMarked_; }

    // Reset mark bit (for next GC cycle)
    void unmark() { isMarked_ = false; }

    // Get object type
    Type type() const { return type_; }

    // Linked list management
    GCObject* next() const { return next_; }
    GCObject*& nextRef() { return next_; }
    void setNext(GCObject* next) { next_ = next; }

    // Recursively mark objects referenced by this object
    virtual void markReferences() = 0;

private:
    Type type_;
    bool isMarked_;
    GCObject* next_;  // Intrusive linked list
};

#endif // LUA_GC_HPP
