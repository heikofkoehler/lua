#ifndef LUA_INT64_HPP
#define LUA_INT64_HPP

#include "vm/gc.hpp"
#include <cstdint>

class Int64Object : public GCObject {
public:
    int64_t value;

    explicit Int64Object(int64_t val)
        : GCObject(GCObject::Type::INT64), value(val) {}

    void markReferences() override {}
    size_t size() const override { return sizeof(Int64Object); }
};

#endif // LUA_INT64_HPP
