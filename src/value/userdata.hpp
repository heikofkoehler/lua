#ifndef LUA_USERDATA_HPP
#define LUA_USERDATA_HPP

#include "vm/gc.hpp"
#include "value/value.hpp"

// Userdata object: wrapper for C++ pointers with optional metatable
class UserdataObject : public GCObject {
public:
    UserdataObject(void* data) 
        : GCObject(GCObject::Type::USERDATA), data_(data), metatable_(Value::nil()) {}
        
    ~UserdataObject() = default; // Destructor doesn't free the raw pointer automatically by default

    void* data() const { return data_; }
    void setData(void* data) { data_ = data; }

    Value metatable() const { return metatable_; }
    void setMetatable(const Value& mt);

    // GC interface
    void markReferences() override;

    size_t size() const override {
        return sizeof(UserdataObject);
    }

private:
    void* data_;
    Value metatable_;
};

#endif // LUA_USERDATA_HPP
