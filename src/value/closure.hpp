#ifndef LUA_CLOSURE_HPP
#define LUA_CLOSURE_HPP

#include "vm/gc.hpp"
#include "value/function.hpp"
#include <vector>
#include <cstddef>

class UpvalueObject;

// ClosureObject: A function with captured upvalues
class ClosureObject : public GCObject {
public:
    // Create closure for a function with specified upvalue count
    ClosureObject(FunctionObject* function, size_t upvalueCount)
        : GCObject(GCObject::Type::CLOSURE), function_(function), upvalues_(upvalueCount, nullptr) {}

    // Get the underlying function
    FunctionObject* function() const { return function_; }

    // Get number of upvalues
    size_t upvalueCount() const { return upvalues_.size(); }

    // Set upvalue pointer
    void setUpvalue(size_t index, UpvalueObject* upvalue);

    // Get upvalue object
    UpvalueObject* getUpvalueObj(size_t index) const {
        if (index < upvalues_.size()) {
            return upvalues_[index];
        }
        return nullptr;
    }

    // GC interface: mark references
    void markReferences() override;

    size_t size() const override {
        return sizeof(ClosureObject) + upvalues_.size() * sizeof(UpvalueObject*);
    }

private:
    FunctionObject* function_;           // Not owned (owned by Chunk)
    std::vector<UpvalueObject*> upvalues_; // Captured variables (heap-allocated)
};

#endif // LUA_CLOSURE_HPP
