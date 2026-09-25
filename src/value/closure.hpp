#ifndef LUA_CLOSURE_HPP
#define LUA_CLOSURE_HPP

#include "vm/gc.hpp"
#include "value/function.hpp"
#include "value/value.hpp"
#include <vector>
#include <cstddef>

class UpvalueObject;
class VM;
struct lua_State;
using NativeFunction = bool (*)(VM* vm, int argCount);
using lua_CFunction = int (*)(lua_State* L);

// ClosureObject: A function with captured upvalues (Lua or C)
class ClosureObject : public GCObject {
public:
    // Create closure for a Lua function with specified upvalue count
    ClosureObject(FunctionObject* function, size_t upvalueCount)
        : GCObject(GCObject::Type::CLOSURE), function_(function), isC_(false),
          nativeFunc_(nullptr), cFunc_(nullptr), upvalues_(upvalueCount, nullptr) {}

    // Create C closure with specified NativeFunction and upvalues
    ClosureObject(NativeFunction nativeFunc, const std::vector<Value>& upvalues)
        : GCObject(GCObject::Type::CLOSURE), function_(nullptr), isC_(true),
          nativeFunc_(nativeFunc), cFunc_(nullptr), cUpvalues_(upvalues) {}

    // Create C closure with specified lua_CFunction and upvalues
    ClosureObject(lua_CFunction cFunc, const std::vector<Value>& upvalues)
        : GCObject(GCObject::Type::CLOSURE), function_(nullptr), isC_(true),
          nativeFunc_(nullptr), cFunc_(cFunc), cUpvalues_(upvalues) {}

    bool isC() const { return isC_; }
    NativeFunction nativeFunc() const { return nativeFunc_; }
    lua_CFunction cFunc() const { return cFunc_; }

    // Get the underlying function (null for C closures)
    FunctionObject* function() const { return function_; }

    // Get number of upvalues
    size_t upvalueCount() const { return isC_ ? cUpvalues_.size() : upvalues_.size(); }

    // Set upvalue pointer (Lua closure)
    void setUpvalue(size_t index, UpvalueObject* upvalue);

    // Get upvalue object (Lua closure)
    UpvalueObject* getUpvalueObj(size_t index) const {
        if (!isC_ && index < upvalues_.size()) {
            return upvalues_[index];
        }
        return nullptr;
    }

    // Get upvalue value (C closure)
    Value getCUpvalue(size_t index) const {
        if (isC_ && index < cUpvalues_.size()) {
            return cUpvalues_[index];
        }
        return Value::nil();
    }

    const Value* getCUpvaluePtr(size_t index) const {
        if (isC_ && index < cUpvalues_.size()) {
            return &cUpvalues_[index];
        }
        return nullptr;
    }

    // Set upvalue value (C closure)
    void setCUpvalue(size_t index, const Value& val) {
        if (isC_ && index < cUpvalues_.size()) {
            cUpvalues_[index] = val;
        }
    }

    // GC interface: mark references
    void markReferences() override;

    size_t size() const override {
        return sizeof(ClosureObject) + 
               (isC_ ? cUpvalues_.size() * sizeof(Value) : upvalues_.size() * sizeof(UpvalueObject*));
    }

private:
    FunctionObject* function_ = nullptr;   // Not owned (owned by Chunk)
    bool isC_ = false;
    NativeFunction nativeFunc_ = nullptr;
    lua_CFunction cFunc_ = nullptr;
    std::vector<UpvalueObject*> upvalues_; // Captured variables (heap-allocated)
    std::vector<Value> cUpvalues_;         // Captured values for C closures
};

#endif // LUA_CLOSURE_HPP
