#ifndef LUA_UPVALUE_HPP
#define LUA_UPVALUE_HPP

#include "vm/gc.hpp"
#include "value/value.hpp"

// UpvalueObject: Captures a variable from an enclosing scope
//
// Upvalues have two states:
// - Open: Points to a stack location in a specific coroutine
// - Closed: Contains copied value (variable has been popped from stack)

class UpvalueObject : public GCObject {
public:
    // Constructor for open upvalue
    UpvalueObject(class CoroutineObject* owner, size_t stackIndex)
        : GCObject(GCObject::Type::UPVALUE), 
          owner_(owner), stackIndex_(stackIndex), 
          closed_(Value::nil()), isClosed_(false) {}

    // Constructor for closed upvalue
    explicit UpvalueObject(const Value& value)
        : GCObject(GCObject::Type::UPVALUE), 
          owner_(nullptr), stackIndex_(0), 
          closed_(value), isClosed_(true) {}

    // Get the value (from owner's stack if open, from closed_ if closed)
    Value get(const std::vector<Value>& currentStack) const;

    // Set the value (to owner's stack if open, to closed_ if closed)
    void set(std::vector<Value>& currentStack, const Value& value);

    // Close the upvalue (copy value from owner's stack to closed_)
    void close(const std::vector<Value>& currentStack);

    // Query state
    bool isClosed() const { return isClosed_; }
    size_t stackIndex() const { return stackIndex_; }
    const Value& closedValue() const { return closed_; }
    class CoroutineObject* owner() const { return owner_; }

    // GC interface
    void markReferences() override;

    size_t size() const override {
        return sizeof(UpvalueObject);
    }

private:
    class CoroutineObject* owner_; // Coroutine that owns the stack slot (if open)
    size_t stackIndex_;            // Index in owner's stack (for open upvalues)
    Value closed_;                 // Closed value (when no longer on stack)
    bool isClosed_;                // true = closed, false = open
};

#endif // LUA_UPVALUE_HPP
