#include "value/table.hpp"
#include "vm/vm.hpp"

void TableObject::set(const Value& key, const Value& value) {
    if (key.isNil() || (key.isFloat() && std::isnan(key.asNumber()))) {
        // Cannot use nil or NaN as a key in Lua
        return;
    }
    
    if (VM::currentVM && !value.isNil()) {
        VM::currentVM->writeBarrier(this, key);
        VM::currentVM->writeBarrier(this, value);
    }
    lastLen_ = 0;

    int64_t idx;
    if (toTableIndex(key, idx) && idx >= 1) {
        size_t uidx = static_cast<size_t>(idx);
        if (uidx <= array_.size()) {
            array_[uidx - 1] = value;
            if (!map_.empty()) {
                auto it = map_.find(key);
                if (it != map_.end()) map_.erase(it);
            }
            return;
        }
        // Appending next element
        if (uidx == array_.size() + 1 && !value.isNil()) {
            array_.push_back(value);
            if (!map_.empty()) {
                auto it = map_.find(key);
                if (it != map_.end()) map_.erase(it);
            }
            return;
        }
        // Within preallocated array capacity
        if (uidx <= array_.capacity() && !value.isNil()) {
            array_.resize(uidx, Value::nil());
            array_[uidx - 1] = value;
            if (!map_.empty()) {
                auto it = map_.find(key);
                if (it != map_.end()) map_.erase(it);
            }
            return;
        }
        // Growth heuristic for dense arrays
        if (uidx <= array_.size() * 2 + 8 && uidx <= 1048576 && !value.isNil()) {
            array_.resize(uidx, Value::nil());
            array_[uidx - 1] = value;
            if (!map_.empty()) {
                auto it = map_.find(key);
                if (it != map_.end()) map_.erase(it);
            }
            return;
        }
    }

    if (value.isNil()) {
        auto it = map_.find(key);
        if (it != map_.end()) {
            it->second = Value::nil();
        }
    } else {
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
        map_[Value::runtimeString(str)] = value;
    }
}

void TableObject::erase(const Value& key) {
    if (key.isNil() || (key.isFloat() && std::isnan(key.asNumber()))) {
        return;
    }
    lastLen_ = 0;
    int64_t idx;
    if (toTableIndex(key, idx) && idx >= 1 && static_cast<size_t>(idx) <= array_.size()) {
        array_[static_cast<size_t>(idx) - 1] = Value::nil();
        return;
    }
    auto it = map_.find(key);
    if (it != map_.end()) {
        map_.erase(it);
        return;
    }
    if (key.isString()) {
        for (auto sit = map_.begin(); sit != map_.end(); ++sit) {
            if (sit->first == key) {
                map_.erase(sit);
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
    while (!array_.empty() && array_.back().isNil()) {
        array_.pop_back();
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
        // Return first non-nil element from array_
        for (size_t i = 0; i < array_.size(); ++i) {
            if (!array_[i].isNil()) {
                return {Value::integer(static_cast<int64_t>(i + 1)), array_[i]};
            }
        }
        // If array_ has no non-nil elements, check map_
        for (auto it = map_.begin(); it != map_.end(); ++it) {
            if (!it->second.isNil()) {
                return {it->first, it->second};
            }
        }
        return {Value::nil(), Value::nil()};
    }

    int64_t idx;
    if (toTableIndex(key, idx) && idx >= 1 && static_cast<size_t>(idx) <= array_.size()) {
        // Advance in array_
        for (size_t i = static_cast<size_t>(idx); i < array_.size(); ++i) {
            if (!array_[i].isNil()) {
                return {Value::integer(static_cast<int64_t>(i + 1)), array_[i]};
            }
        }
        // Exhausted array_, move to map_
        for (auto it = map_.begin(); it != map_.end(); ++it) {
            if (!it->second.isNil()) {
                return {it->first, it->second};
            }
        }
        return {Value::nil(), Value::nil()};
    }

    // Key is in map_
    auto it = map_.find(key);
    if (it == map_.end() && key.isString()) {
        for (auto sit = map_.begin(); sit != map_.end(); ++sit) {
            if (sit->first == key) {
                it = sit;
                break;
            }
        }
    }

    if (it == map_.end()) {
        keyFound = false;
        return {Value::nil(), Value::nil()};
    }

    ++it;
    while (it != map_.end() && it->second.isNil()) {
        ++it;
    }
    if (it == map_.end()) {
        return {Value::nil(), Value::nil()};
    }
    return {it->first, it->second};
}

void TableObject::markReferences() {
    // Table marking is handled by blackenObject in incremental GC
}

size_t TableObject::length() const {
    size_t aLen = array_.size();

    // Check array_ first
    if (aLen > 0) {
        if (!array_[aLen - 1].isNil()) {
            // Check if map_[aLen + 1] is non-nil
            int64_t nextIdx = static_cast<int64_t>(aLen + 1);
            auto it = map_.find(Value::integer(nextIdx));
            if (it == map_.end() || it->second.isNil()) {
                lastLen_ = aLen;
                return aLen;
            }
            // Continuation into map_
            size_t maxLimit = aLen + map_.size() + 1;
            size_t low = aLen;
            size_t high = std::min(low + 2, maxLimit);
            auto isNonNil = [this](size_t idx) -> bool {
                if (idx <= array_.size()) return !array_[idx - 1].isNil();
                int64_t sidx = static_cast<int64_t>(idx);
                auto it = map_.find(Value::integer(sidx));
                return it != map_.end() && !it->second.isNil();
            };
            while (isNonNil(high)) {
                low = high;
                if (!isNonNil(low + 1)) {
                    lastLen_ = low;
                    return low;
                }
                if (high >= maxLimit) break;
                if (high > maxLimit / 2) high = maxLimit;
                else high *= 2;
            }
            while (high - low > 1) {
                size_t mid = low + (high - low) / 2;
                if (isNonNil(mid)) low = mid;
                else high = mid;
            }
            lastLen_ = low;
            return low;
        }

        if (array_[0].isNil()) {
            lastLen_ = 0;
            return 0;
        }

        // Binary search directly in array_
        size_t low = 1;
        size_t high = aLen;
        while (high - low > 1) {
            size_t mid = low + (high - low) / 2;
            if (!array_[mid - 1].isNil()) {
                low = mid;
            } else {
                high = mid;
            }
        }
        lastLen_ = low;
        return low;
    }

    // array_ is empty, check map_
    if (map_.empty()) {
        lastLen_ = 0;
        return 0;
    }

    auto isNonNil = [this](size_t idx) -> bool {
        auto it = map_.find(Value::integer(static_cast<int64_t>(idx)));
        return it != map_.end() && !it->second.isNil();
    };

    if (!isNonNil(1)) {
        lastLen_ = 0;
        return 0;
    }

    size_t maxLimit = map_.size() + 1;
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
