#include "value/table.hpp"
#include "vm/vm.hpp"

void TableObject::set(const Value& key, const Value& value) {
    if (key.isNil() || (key.isFloat() && std::isnan(key.asNumber()))) {
        // Cannot use nil or NaN as a key in Lua
        return;
    }
    
    if (value.isNil()) {
        auto it = map_.find(key);
        if (it != map_.end()) {
            it->second = Value::nil();
        }
    } else {
        if (VM::currentVM) {
            VM::currentVM->writeBarrier(this, key);
            VM::currentVM->writeBarrier(this, value);
        }
        map_[key] = value;
    }
}

void TableObject::set(const std::string& key, const Value& value) {
    // Find existing string key by content
    for (auto it = map_.begin(); it != map_.end(); ++it) {
        if (it->first.isStringEqual(key)) {
            if (value.isNil()) {
                it->second = Value::nil();
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
        if (pair.first == key) {
            return pair.second;
        }
    }
    return Value::nil();
}

std::pair<Value, Value> TableObject::next(const Value& key, bool& keyFound) const {
    keyFound = true;
    if (key.isNil()) {
        // Return first non-nil element
        for (auto it = map_.begin(); it != map_.end(); ++it) {
            if (!it->second.isNil()) {
                return {it->first, it->second};
            }
        }
        return {Value::nil(), Value::nil()};
    } else {
        // Find key and return next
        auto it = map_.find(key);
        if (it == map_.end()) {
            // Try string-based lookup if not found by exact value
            if (key.isString()) {
                for (auto sit = map_.begin(); sit != map_.end(); ++sit) {
                    if (sit->first == key) {
                        it = sit;
                        break;
                    }
                }
            }
        }
        
        if (it == map_.end()) {
            // Key not found in table -> invalid key
            keyFound = false;
            return {Value::nil(), Value::nil()};
        }
        
        ++it;
        while (it != map_.end() && it->second.isNil()) {
            ++it;
        }
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
