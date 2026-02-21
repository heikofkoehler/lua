#ifndef LUA_UPVALUE_HPP
#define LUA_UPVALUE_HPP

#include "vm/gc.hpp"
#include "value/value.hpp"
#include <vector>

// UpvalueObject: Captures a variable from an enclosing scope
//
// Upvalues have two states:
// - Open: Points to a stack location (variable still on stack)
// - Closed: Contains copied value (variable has been popped from stack)
//
// When a scope exits, open upvalues are "closed" - their values are copied
// from the stack to heap storage, allowing closures to outlive their parent functions.

class UpvalueObject : public GCObject {
public:
    // Constructor for open upvalue (points to stack)
    explicit UpvalueObject(size_t stackIndex)
        : GCObject(GCObject::Type::UPVALUE), stackIndex_(stackIndex), closed_(Value::nil()), isClosed_(false) {}

    // Get the value (from stack if open, from closed_ if closed)
    Value get(const std::vector<Value>& stack) const {
        if (isClosed_) {
            return closed_;
        }
        return stack[stackIndex_];
    }

    // Set the value (to stack if open, to closed_ if closed)
    void set(std::vector<Value>& stack, const Value& value) {
        if (isClosed_) {
            closed_ = value;
        } else {
            stack[stackIndex_] = value;
        }
    }

    // Close the upvalue (copy stack value to closed_)
    void close(const std::vector<Value>& stack) {
        if (!isClosed_) {
            closed_ = stack[stackIndex_];
            isClosed_ = true;
        }
    }

    // Query state
    bool isClosed() const { return isClosed_; }
    size_t stackIndex() const { return stackIndex_; }

    // GC interface: mark closed value if upvalue is closed
    void markReferences() override;

private:
    size_t stackIndex_;  // Index in VM stack (for open upvalues)
    Value closed_;       // Closed value (when no longer on stack)
    bool isClosed_;      // true = closed, false = open
};

#endif // LUA_UPVALUE_HPP
