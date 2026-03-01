#include "value/table.hpp"
#include "vm/vm.hpp"

void TableObject::set(const Value& key, const Value& value) {
    if (key.isNil()) {
        // Cannot use nil as a key in Lua
        return;
    }
    
    if (value.isNil()) {
        // Setting to nil removes the key
        map_.erase(key);
    } else {
        if (VM::currentVM) {
            VM::currentVM->writeBarrier(this, key);
            VM::currentVM->writeBarrier(this, value);
            
            // If adding a NEW key, memory will grow
            if (map_.find(key) == map_.end()) {
                // Check GC BEFORE growth. Use a larger estimate to account for map overhead.
                // VM::currentVM->checkGC(128); 
            }
        }
        map_[key] = value;
    }
}

void TableObject::set(const std::string& key, const Value& value) {
    // Find existing string key by content
    for (auto it = map_.begin(); it != map_.end(); ++it) {
        if (it->first.isStringEqual(key)) {
            if (value.isNil()) {
                map_.erase(it);
            } else {
                if (VM::currentVM) VM::currentVM->writeBarrier(this, value);
                it->second = value;
            }
            return;
        }
    }
    
    // Not found, add new entry if not nil
    if (!value.isNil() && VM::currentVM) {
        StringObject* str = VM::currentVM->internString(key);
        VM::currentVM->writeBarrier(this, str);
        VM::currentVM->writeBarrier(this, value);
        
        // Check GC BEFORE growth
        // VM::currentVM->checkGC(128);
        
        map_[Value::runtimeString(str)] = value;
    }
}

void TableObject::setMetatable(const Value& mt) {
    if (mt.isObj() && VM::currentVM) {
        VM::currentVM->writeBarrier(this, mt.asObj());
    }
    metatable_ = mt;
}

Value TableObject::get(const std::string& key) const {
    // Special lookup by string directly (handles interning issues)
    for (const auto& pair : map_) {
        if (pair.first.isStringEqual(key)) {
            return pair.second;
        }
    }
    return Value::nil();
}

Value TableObject::getByString(const Value& key) const {
    for (const auto& pair : map_) {
        if (pair.first.equals(key)) {
            return pair.second;
        }
    }
    return Value::nil();
}

std::pair<Value, Value> TableObject::next(const Value& key) const {
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
            // Try string-based lookup if not found by exact value
            if (key.isString()) {
                for (auto sit = map_.begin(); sit != map_.end(); ++sit) {
                    if (sit->first.equals(key)) {
                        it = sit;
                        break;
                    }
                }
            }
        }
        
        if (it == map_.end()) {
            // Key still not found or invalid
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

void TableObject::markReferences() {
    // Table marking is handled by blackenObject in incremental GC
}
