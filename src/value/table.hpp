#ifndef LUA_TABLE_HPP
#define LUA_TABLE_HPP

#include "vm/gc.hpp"
#include "value/value.hpp"
#include <unordered_map>
#include <functional>

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
        return a.equals(b);
    }
};

// Table object: Lua's associative array (hash map)
class TableObject : public GCObject {
public:
    TableObject() : GCObject(GCObject::Type::TABLE) {}
    ~TableObject() = default;

    // Table operations
    void set(const Value& key, const Value& value) {
        if (key.isNil()) {
            // Cannot use nil as a key in Lua
            return;
        }
        if (value.isNil()) {
            // Setting to nil removes the key
            map_.erase(key);
        } else {
            map_[key] = value;
        }
    }

    void set(const std::string& key, const Value& value);

    Value get(const Value& key) const {
        auto it = map_.find(key);
        if (it != map_.end()) {
            return it->second;
        }
        if (key.isString()) {
            return getByString(key);
        }
        return Value::nil();  // Key not found returns nil
    }

    Value get(const std::string& key) const;

    bool has(const Value& key) const {
        if (map_.find(key) != map_.end()) return true;
        if (key.isString()) {
            return !getByString(key).isNil();
        }
        return false;
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
    std::pair<Value, Value> next(const Value& key) const;

    // For iteration (if needed later)
    const std::unordered_map<Value, Value, ValueHash, ValueEqual>& data() const {
        return map_;
    }

    // Metatable operations
    void setMetatable(const Value& mt) { metatable_ = mt; }
    Value getMetatable() const { return metatable_; }

    // GC interface: mark all keys and values
    void markReferences() override;

    size_t size() const override {
        // Approximate size: object + entries * (key + value + node overhead)
        return sizeof(TableObject) + map_.size() * (sizeof(Value) * 2 + 16);
    }

private:
    std::unordered_map<Value, Value, ValueHash, ValueEqual> map_;
    Value metatable_ = Value::nil();

    Value getByString(const Value& key) const;
};

#endif // LUA_TABLE_HPP
