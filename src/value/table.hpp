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
        if (v.isNil()) {
            return 0;
        } else if (v.isBool()) {
            return v.asBool() ? 1 : 2;
        } else if (v.isNumber()) {
            return std::hash<double>()(v.asNumber());
        } else if (v.isString()) {
            return std::hash<size_t>()(v.asStringIndex());
        } else if (v.isFunctionObject()) {
            return std::hash<size_t>()(v.asFunctionIndex());
        }
        return 0;
    }
};

// Equality for Value keys
struct ValueEqual {
    bool operator()(const Value& a, const Value& b) const {
        return a == b;
    }
};

// Forward declaration
class VM;

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

    Value get(const Value& key) const {
        auto it = map_.find(key);
        if (it != map_.end()) {
            return it->second;
        }
        return Value::nil();  // Key not found returns nil
    }

    bool has(const Value& key) const {
        return map_.find(key) != map_.end();
    }

    size_t size() const {
        return map_.size();
    }

    // Iteration support
    // Returns pair<key, value>. If key is nil, returns first pair.
    // If next pair doesn't exist (end of iteration), returns pair<nil, nil>.
    std::pair<Value, Value> next(const Value& key) const {
        if (key.isNil()) {
            // Return first element
            if (map_.empty()) {
                return {Value::nil(), Value::nil()};
            }
            auto it = map_.begin();
            return {it->first, it->second};
        } else {
            // Find key and return next
            auto it = map_.find(key);
            if (it == map_.end()) {
                // Key not found or invalid
                return {Value::nil(), Value::nil()};
            }
            ++it;
            if (it == map_.end()) {
                // End of table
                return {Value::nil(), Value::nil()};
            }
            return {it->first, it->second};
        }
    }

    // For iteration (if needed later)
    const std::unordered_map<Value, Value, ValueHash, ValueEqual>& data() const {
        return map_;
    }

    // Metatable operations
    void setMetatable(const Value& mt) { metatable_ = mt; }
    Value getMetatable() const { return metatable_; }

    // GC interface: mark all keys and values
    void markReferences() override;

private:
    std::unordered_map<Value, Value, ValueHash, ValueEqual> map_;
    Value metatable_ = Value::nil();
};

#endif // LUA_TABLE_HPP
