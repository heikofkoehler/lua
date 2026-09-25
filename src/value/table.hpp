#ifndef LUA_TABLE_HPP
#define LUA_TABLE_HPP

#include "vm/gc.hpp"
#include "value/value.hpp"
#include <unordered_map>
#include <functional>
#include <cmath>

// Forward declaration
class TableObject;

// Hash function for Value keys
struct ValueHash {
    size_t operator()(const Value& v) const {
        return v.hash();
    }
};

// Equality for Value keys
struct ValueEqual {
    bool operator()(const Value& a, const Value& b) const {
        return a == b;
    }
};

// Helper to extract integer index from Value
static inline bool toTableIndex(const Value& key, int64_t& idx) {
    if (key.isInteger()) {
        idx = key.asInteger();
        return true;
    }
    if (key.isFloat()) {
        double n = key.asNumber();
        double intPart;
        if (std::isfinite(n) && std::modf(n, &intPart) == 0.0 &&
            n >= -9223372036854775808.0 && n < 9223372036854775808.0) {
            idx = static_cast<int64_t>(n);
            return true;
        }
    }
    return false;
}

// Table object: Lua's associative array (hybrid flat array + hash map)
class TableObject : public GCObject {
public:
    TableObject(size_t nseq = 0, size_t nrec = 0)
        : GCObject(GCObject::Type::TABLE), capacity_(nseq + nrec), arrayCapacity_(nseq) {
        if (nseq > 0) {
            array_.resize(nseq, Value::nil());
        }
        if (nrec > 0) {
            map_.reserve(nrec);
        }
    }
    ~TableObject() = default;

    // Table operations
    void set(const Value& key, const Value& value);

    void set(const std::string& key, const Value& value);

    void erase(const Value& key);
    void cleanNilEntries();

    Value get(const Value& key) const {
        if (key.isNil() || (key.isFloat() && std::isnan(key.asNumber()))) {
            return Value::nil();
        }
        int64_t idx;
        if (toTableIndex(key, idx)) {
            if (idx >= 1 && static_cast<size_t>(idx) <= array_.size()) {
                return array_[static_cast<size_t>(idx) - 1];
            }
        }
        auto it = map_.find(key);
        if (it != map_.end()) {
            return it->second;
        }
        return Value::nil();  // Key not found returns nil
    }

    Value get(const std::string& key) const;

    bool has(const Value& key) const {
        if (key.isNil() || (key.isFloat() && std::isnan(key.asNumber()))) {
            return false;
        }
        int64_t idx;
        if (toTableIndex(key, idx)) {
            if (idx >= 1 && static_cast<size_t>(idx) <= array_.size()) {
                return !array_[static_cast<size_t>(idx) - 1].isNil();
            }
        }
        auto it = map_.find(key);
        return it != map_.end() && !it->second.isNil();
    }

    size_t length() const;

    // Iteration support
    std::pair<Value, Value> next(const Value& key, bool& keyFound) const;
    std::pair<Value, Value> next(const Value& key) const {
        bool dummy;
        return next(key, dummy);
    }

    // Direct storage accessors for GC
    const std::vector<Value>& array() const { return array_; }
    void setArrayElement(size_t index, const Value& val) {
        if (index < array_.size()) array_[index] = val;
    }
    const std::unordered_map<Value, Value, ValueHash, ValueEqual>& map() const { return map_; }
    const std::unordered_map<Value, Value, ValueHash, ValueEqual>& data() const { return map_; }

    // Metatable operations
    void setMetatable(const Value& mt);
    Value getMetatable() const { return metatable_; }

    // GC interface: mark all keys and values
    void markReferences() override;

    size_t size() const override {
        size_t arrayMem = array_.capacity() * sizeof(Value);
        size_t cap = std::max(map_.size(), capacity_);
        return sizeof(TableObject) + arrayMem + cap * (sizeof(Value) * 2 + 16);
    }

    size_t capacity() const { return capacity_; }
    size_t arrayCapacity() const { return arrayCapacity_; }

private:
    std::vector<Value> array_;
    std::unordered_map<Value, Value, ValueHash, ValueEqual> map_;
    Value metatable_ = Value::nil();
    size_t capacity_ = 0;
    size_t arrayCapacity_ = 0;
    mutable size_t lastLen_ = 0;

    Value getByString(const Value& key) const;
};

#endif // LUA_TABLE_HPP
