#ifndef LUA_GC_HPP
#define LUA_GC_HPP

#include <cstddef>

// Base class for all garbage-collected objects
// Uses intrusive linked list for tracking all allocated objects
class GCObject {
public:
    enum class Type {
        STRING,
        TABLE,
        CLOSURE,
        UPVALUE,
        FILE,
        SOCKET,
        COROUTINE,
        USERDATA
    };

    enum class Color {
        WHITE,
        GRAY,
        BLACK
    };

    GCObject(Type type) : type_(type), color_(Color::WHITE), next_(nullptr) {}
    virtual ~GCObject() = default;

    // Tri-color marking colors
    Color color() const { return color_; }
    void setColor(Color color) { color_ = color; }

    // Compatibility helpers
    bool isMarked() const { return color_ != Color::WHITE; }
    void mark() { color_ = Color::BLACK; }
    void unmark() { color_ = Color::WHITE; }

    // Get object type
    Type type() const { return type_; }

    // Linked list management
    GCObject* next() const { return next_; }
    GCObject*& nextRef() { return next_; }
    void setNext(GCObject* next) { next_ = next; }

    // Recursively mark objects referenced by this object
    virtual void markReferences() = 0;

    // Get approximate memory size of this object
    virtual size_t size() const = 0;

private:
    Type type_;
    Color color_;
    GCObject* next_;  // Intrusive linked list
};

#endif // LUA_GC_HPP
