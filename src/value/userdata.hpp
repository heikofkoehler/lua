#ifndef LUA_USERDATA_HPP
#define LUA_USERDATA_HPP

#include "vm/gc.hpp"
#include "value/value.hpp"
#include <vector>

// Userdata object: wrapper for C++ pointers with optional metatable
class UserdataObject : public GCObject {
public:
    UserdataObject(void* data, int numUserValues = 1, bool isLight = false, bool ownsMemory = false)
        : GCObject(GCObject::Type::USERDATA), data_(data), metatable_(Value::nil()), isLight_(isLight), ownsMemory_(ownsMemory) {
        userValues_.resize(numUserValues, Value::nil());
    }

    ~UserdataObject() {
        if (ownsMemory_ && data_) {
            std::free(data_);
            data_ = nullptr;
        }
    }

    void* data() const { return data_; }
    void setData(void* data) { data_ = data; }

    bool isLight() const { return isLight_; }
    void setIsLight(bool l) { isLight_ = l; }

    bool ownsMemory() const { return ownsMemory_; }
    void setOwnsMemory(bool o) { ownsMemory_ = o; }

    Value metatable() const { return metatable_; }
    void setMetatable(const Value& mt);

    int numUserValues() const { return static_cast<int>(userValues_.size()); }
    Value getUserValue(int index) const {
        if (index >= 0 && static_cast<size_t>(index) < userValues_.size()) return userValues_[index];
        return Value::nil();
    }
    void setUserValue(int index, const Value& val);

    // GC interface
    void markReferences() override;

    size_t size() const override {
        return sizeof(UserdataObject) + userValues_.capacity() * sizeof(Value);
    }

private:
    void* data_;
    Value metatable_;
    std::vector<Value> userValues_;
    bool isLight_ = false;
    bool ownsMemory_ = false;
};
#endif // LUA_USERDATA_HPP
