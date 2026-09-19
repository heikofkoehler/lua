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

// Table object: Lua's associative array (hash map)
class TableObject : public GCObject {
public:
    TableObject(size_t nseq = 0, size_t nrec = 0)
        : GCObject(GCObject::Type::TABLE), capacity_(nseq + nrec) {
        if (capacity_ > 0) {
            map_.reserve(capacity_);
        }
    }
    ~TableObject() = default;

    // Table operations
    void set(const Value& key, const Value& value);

    void set(const std::string& key, const Value& value);

    Value get(const Value& key) const {
        if (key.isNil() || (key.isFloat() && std::isnan(key.asNumber()))) {
            return Value::nil();
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
        auto it = map_.find(key);
        return it != map_.end() && !it->second.isNil();
    }

    size_t length() const {
        size_t n = 0;
        while (true) {
            auto it = map_.find(Value::number(static_cast<double>(n + 1)));
            if (it == map_.end() || it->second.isNil()) {
                break;
            }
            n++;
        }
        return n;
    }

    // Iteration support
    // Returns pair<key, value>. If key is nil, returns first pair.
    // If next pair doesn't exist (end of iteration), returns pair<nil, nil>.
    std::pair<Value, Value> next(const Value& key, bool& keyFound) const;
    std::pair<Value, Value> next(const Value& key) const {
        bool dummy;
        return next(key, dummy);
    }

    // For iteration (if needed later)
    const std::unordered_map<Value, Value, ValueHash, ValueEqual>& data() const {
        return map_;
    }

    // Metatable operations
    void setMetatable(const Value& mt);
    Value getMetatable() const { return metatable_; }

    // GC interface: mark all keys and values
    void markReferences() override;

    size_t size() const override {
        // Approximate size: object + entries * (key + value + node overhead)
        size_t cap = std::max(map_.size(), capacity_);
        return sizeof(TableObject) + cap * (sizeof(Value) * 2 + 16);
    }

    size_t capacity() const { return capacity_; }

private:
    std::unordered_map<Value, Value, ValueHash, ValueEqual> map_;
    Value metatable_ = Value::nil();
    size_t capacity_ = 0;

    Value getByString(const Value& key) const;
};

#endif // LUA_TABLE_HPP
