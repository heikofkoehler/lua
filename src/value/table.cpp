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
            lastLen_ = 0;
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

void TableObject::erase(const Value& key) {
    if (key.isNil() || (key.isFloat() && std::isnan(key.asNumber()))) {
        return;
    }
    auto it = map_.find(key);
    if (it != map_.end()) {
        map_.erase(it);
        lastLen_ = 0;
        return;
    }
    if (key.isString()) {
        for (auto sit = map_.begin(); sit != map_.end(); ++sit) {
            if (sit->first == key) {
                map_.erase(sit);
                lastLen_ = 0;
                return;
            }
        }
    }
}

void TableObject::cleanNilEntries() {
    auto it = map_.begin();
    while (it != map_.end()) {
        if (it->second.isNil()) {
            it = map_.erase(it);
        } else {
            ++it;
        }
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

size_t TableObject::length() const {
    if (map_.empty()) {
        lastLen_ = 0;
        return 0;
    }

    auto isNonNil = [this](size_t idx) -> bool {
        auto it = map_.find(Value::integer(static_cast<int64_t>(idx)));
        return it != map_.end() && !it->second.isNil();
    };

    // Fast check: if t[1] is nil, length is 0
    if (!isNonNil(1)) {
        lastLen_ = 0;
        return 0;
    }

    // Fast path: cached length
    if (lastLen_ > 0) {
        if (isNonNil(lastLen_) && !isNonNil(lastLen_ + 1)) {
            return lastLen_;
        }
        if (isNonNil(lastLen_ + 1) && !isNonNil(lastLen_ + 2)) {
            lastLen_++;
            return lastLen_;
        }
    }

    size_t maxLimit = map_.size() + 1;
    // Exponential search
    size_t low = 1;
    size_t high = std::min(size_t(2), maxLimit);
    while (isNonNil(high)) {
        low = high;
        if (!isNonNil(low + 1)) {
            lastLen_ = low;
            return low;
        }
        if (high >= maxLimit) {
            break;
        }
        if (high > maxLimit / 2) {
            high = maxLimit;
        } else {
            high *= 2;
        }
    }

    // Binary search in (low, high]
    while (high - low > 1) {
        size_t mid = low + (high - low) / 2;
        if (isNonNil(mid)) {
            low = mid;
        } else {
            high = mid;
        }
    }

    lastLen_ = low;
    return low;
}

