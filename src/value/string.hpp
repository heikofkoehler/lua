#ifndef LUA_STRING_HPP
#define LUA_STRING_HPP

#include "vm/gc.hpp"
#include <string>
#include <cstdint>
#include <cstring>

// String object with hash for interning
class StringObject : public GCObject {
public:
    StringObject(const char* chars, size_t length)
        : GCObject(GCObject::Type::STRING), length_(length), hash_(0) {
        chars_ = new char[length + 1];
        std::memcpy(chars_, chars, length);
        chars_[length] = '\0';
        hash_ = computeHash();
    }

    StringObject(const std::string& str)
        : StringObject(str.c_str(), str.length()) {}

    ~StringObject() {
        delete[] chars_;
    }

    // Disable copy (strings are interned, so we only keep one copy)
    StringObject(const StringObject&) = delete;
    StringObject& operator=(const StringObject&) = delete;

    const char* chars() const { return chars_; }
    size_t length() const { return length_; }
    uint32_t hash() const { return hash_; }

    // Equality comparison
    bool equals(const StringObject* other) const {
        if (this == other) return true;
        if (length_ != other->length_) return false;
        return std::memcmp(chars_, other->chars_, length_) == 0;
    }

    bool equals(const char* chars, size_t length) const {
        if (length_ != length) return false;
        return std::memcmp(chars_, chars, length) == 0;
    }

    // GC interface: strings don't reference other objects
    void markReferences() override {}

private:
    char* chars_;
    size_t length_;
    uint32_t hash_;

    // FNV-1a hash function
    uint32_t computeHash() {
        uint32_t hash = 2166136261u;
        for (size_t i = 0; i < length_; i++) {
            hash ^= static_cast<uint8_t>(chars_[i]);
            hash *= 16777619u;
        }
        return hash;
    }
};

#endif // LUA_STRING_HPP
