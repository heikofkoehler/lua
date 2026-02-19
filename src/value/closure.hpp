#ifndef LUA_CLOSURE_HPP
#define LUA_CLOSURE_HPP

#include "value/function.hpp"
#include <vector>
#include <cstddef>

// ClosureObject: A function with captured upvalues
//
// Represents a runtime closure - a function together with its captured
// variables from enclosing scopes. The closure stores indices into the
// VM's upvalue pool, allowing multiple closures to share upvalues.

class ClosureObject {
public:
    // Create closure for a function with specified upvalue count
    ClosureObject(FunctionObject* function, size_t upvalueCount)
        : function_(function), upvalues_(upvalueCount, SIZE_MAX) {}

    // Get the underlying function
    FunctionObject* function() const { return function_; }

    // Get number of upvalues
    size_t upvalueCount() const { return upvalues_.size(); }

    // Set upvalue at index (stores index into VM's upvalue pool)
    void setUpvalue(size_t index, size_t upvalueIndex) {
        if (index < upvalues_.size()) {
            upvalues_[index] = upvalueIndex;
        }
    }

    // Get upvalue index at position
    size_t getUpvalue(size_t index) const {
        if (index < upvalues_.size()) {
            return upvalues_[index];
        }
        return SIZE_MAX;
    }

private:
    FunctionObject* function_;           // Not owned (owned by Chunk)
    std::vector<size_t> upvalues_;      // Indices into VM's upvalue pool
};

#endif // LUA_CLOSURE_HPP
