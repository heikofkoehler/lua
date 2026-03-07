#ifndef LUA_VALUE_HPP
#define LUA_VALUE_HPP

#include "common/common.hpp"
#include <cstring>
#include <cmath>
#include <cstdint>

// Forward declarations
class FunctionObject;
class Chunk;
class GCObject;
class StringObject;
class TableObject;
class ClosureObject;
class FileObject;
class SocketObject;
class CoroutineObject;
class UserdataObject;

/*
 * NaN-Boxing Value Representation (64-bit)
 * ---------------------------------------
 * Values are stored as 64-bit doubles.
 * If the value is not a NaN, it's a standard Lua number (double).
 * If it is a NaN, the lower bits store the type and payload.
 *
 * We use a Quiet NaN with the sign bit set (0xFFF0...) as our base.
 *
 * Tags:
 * 0xFFF1: Nil
 * 0xFFF2: Boolean
 * 0xFFF3: Integer (32-bit in lower 32 bits)
 * 0xFFF4: String (Index in lower 32 bits)
 * 0xFFF5: Table (Pointer in lower 48 bits)
 * 0xFFF6: Closure (Pointer)
 * 0xFFF7: Function (Pointer)
 * 0xFFF8: Native Function (Index)
 * 0xFFF9: Userdata (Pointer)
 * 0xFFFA: Thread/Coroutine (Pointer)
 * 0xFFFB: Upvalue (Pointer)
 * 0xFFFC: C Function (Pointer)
 */

class Value {
public:
    enum class Type : uint16_t {
        NIL             = 0xFFF1,
        BOOL            = 0xFFF2,
        INTEGER         = 0xFFF3,
        STRING          = 0xFFF4,
        TABLE           = 0xFFF5,
        CLOSURE         = 0xFFF6,
        FUNCTION        = 0xFFF7,
        NATIVE_FUNCTION = 0xFFF8,
        USERDATA        = 0xFFF9,
        THREAD          = 0xFFFA,
        UPVALUE         = 0xFFFB,
        C_FUNCTION      = 0xFFFC,
        FILE            = 0xFFFD,
        SOCKET          = 0xFFFE,
        
        // Pseudo-types for type checking
        NUMBER          = 0x0000, 
    };

    static constexpr int NUM_TYPES = 17;

private:
    uint64_t bits_;

    static constexpr uint64_t QNAN = 0x7FF0000000000000ULL;
    static constexpr uint64_t SIGN_BIT = 0x8000000000000000ULL;
    static constexpr uint64_t TAG_MASK = 0xFFFF000000000000ULL;

    constexpr explicit Value(uint64_t bits) : bits_(bits) {}

    static constexpr uint64_t encodeTag(Type type) {
        return QNAN | SIGN_BIT | (static_cast<uint64_t>(type) << 48);
    }

public:
    constexpr Value() : bits_(encodeTag(Type::NIL)) {}

    static constexpr Value nil() {
        return Value(encodeTag(Type::NIL));
    }

    static constexpr Value boolean(bool value) {
        return Value(encodeTag(Type::BOOL) | (value ? 1 : 0));
    }

    static Value number(double value) {
        uint64_t bits;
        std::memcpy(&bits, &value, sizeof(double));
        return Value(bits);
    }

    static constexpr Value integer(int64_t value) {
        // We store integers as 32-bit for simplicity in this NaN-box scheme
        // or we could use the full 48 bits if needed. Let's use 32-bit.
        return Value(encodeTag(Type::INTEGER) | (static_cast<uint64_t>(static_cast<uint32_t>(value))));
    }

    static constexpr Value function(size_t funcIndex) {
        return Value(encodeTag(Type::FUNCTION) | static_cast<uint64_t>(funcIndex));
    }

    static constexpr Value string(size_t stringIndex) {
        return Value(encodeTag(Type::STRING) | static_cast<uint64_t>(stringIndex));
    }

    static Value runtimeString(StringObject* str) {
        return Value(encodeTag(Type::STRING) | reinterpret_cast<uint64_t>(str));
    }

    static Value table(TableObject* table) {
        return Value(encodeTag(Type::TABLE) | reinterpret_cast<uint64_t>(table));
    }

    static Value closure(ClosureObject* closure) {
        return Value(encodeTag(Type::CLOSURE) | reinterpret_cast<uint64_t>(closure));
    }

    static Value file(FileObject* file) {
        return Value(encodeTag(Type::FILE) | reinterpret_cast<uint64_t>(file));
    }

    static Value socket(SocketObject* socket) {
        return Value(encodeTag(Type::SOCKET) | reinterpret_cast<uint64_t>(socket));
    }

    static Value userdata(UserdataObject* udata) {
        return Value(encodeTag(Type::USERDATA) | reinterpret_cast<uint64_t>(udata));
    }

    static Value thread(CoroutineObject* coroutine) {
        return Value(encodeTag(Type::THREAD) | reinterpret_cast<uint64_t>(coroutine));
    }

    static Value nativeFunction(size_t index) {
        return Value(encodeTag(Type::NATIVE_FUNCTION) | static_cast<uint64_t>(index));
    }

    static Value cFunction(void* f) {
        return Value(encodeTag(Type::C_FUNCTION) | reinterpret_cast<uint64_t>(f));
    }

    static Value fromObj(GCObject* obj);

    // Raw access
    uint64_t bits() const { return bits_; }

    // Type checking
    bool isFloat() const { 
        // Any value where the top 16 bits are NOT 0xFFF1 through 0xFFFF is a float.
        // Wait, what about encodeTag(Type::NIL) -> 0xFFF1...
        // The base tag is 0xFFF0...
        // Let's check if the top 16 bits are >= 0xFFF1. If so, it's a boxed value.
        // If not, it's a number (including NaN, Infinity, -Infinity, etc.)
        return (bits_ >> 48) < 0xFFF1;
    }
    bool isNumber() const { return isFloat() || isInteger(); }
    bool isNil() const { return bits_ == encodeTag(Type::NIL); }
    bool isBool() const { return (bits_ & TAG_MASK) == encodeTag(Type::BOOL); }
    bool isInteger() const { return (bits_ & TAG_MASK) == encodeTag(Type::INTEGER); }
    bool isString() const { return (bits_ & TAG_MASK) == encodeTag(Type::STRING); }
    bool isTable() const { return (bits_ & TAG_MASK) == encodeTag(Type::TABLE); }
    bool isClosure() const { return (bits_ & TAG_MASK) == encodeTag(Type::CLOSURE); }
    bool isUserdata() const { return (bits_ & TAG_MASK) == encodeTag(Type::USERDATA); }
    bool isFile() const { return (bits_ & TAG_MASK) == encodeTag(Type::FILE); }
    bool isSocket() const { return (bits_ & TAG_MASK) == encodeTag(Type::SOCKET); }
    bool isThread() const { return (bits_ & TAG_MASK) == encodeTag(Type::THREAD); }
    bool isNativeFunction() const { return (bits_ & TAG_MASK) == encodeTag(Type::NATIVE_FUNCTION); }
    bool isCFunction() const { return (bits_ & TAG_MASK) == encodeTag(Type::C_FUNCTION); }
    bool isFunctionObject() const { return (bits_ & TAG_MASK) == encodeTag(Type::FUNCTION); }
    bool isRuntimeString() const { return isString() && (bits_ & 0xFFFFFFFFFFFFULL) > 0x10000; }

    bool isFunction() const {
        return isFunctionObject() || isClosure() || isNativeFunction() || isCFunction();
    }

    bool isObj() const {
        // Most tagged types in our VM are GC objects if they aren't numbers, bools, nils or indices.
        return isTable() || isClosure() || isUserdata() || isThread() || isFile() || isSocket() || isRuntimeString();
    }

    Type type() const {
        if (isInteger()) return Type::INTEGER;
        if (isFloat()) return Type::NUMBER;
        return static_cast<Type>((bits_ & 0x000F000000000000ULL) >> 48 | 0xFFF0);
    }

    // Value extraction
    bool asBool() const { return (bits_ & 1) != 0; }
    
    double asNumber() const {
        if (isInteger()) {
            return static_cast<double>(static_cast<int32_t>(bits_ & 0xFFFFFFFFULL));
        }
        union { uint64_t b; double d; } u;
        u.b = bits_;
        return u.d;
    }

    int64_t asInteger() const {
        if (isInteger()) return static_cast<int64_t>(static_cast<int32_t>(bits_ & 0xFFFFFFFFULL));
        return static_cast<int64_t>(asNumber());
    }

    size_t asFunctionIndex() const { return static_cast<size_t>(bits_ & 0xFFFFFFFFFFFFULL); }
    size_t asStringIndex() const { return static_cast<size_t>(bits_ & 0xFFFFFFFFFFFFULL); }
    size_t asNativeFunctionIndex() const { return static_cast<size_t>(bits_ & 0xFFFFFFFFFFFFULL); }
    void* asCFunction() const { return reinterpret_cast<void*>(bits_ & 0xFFFFFFFFFFFFULL); }

    GCObject* asObj() const { return reinterpret_cast<GCObject*>(bits_ & 0xFFFFFFFFFFFFULL); }
    TableObject* asTableObj() const { return reinterpret_cast<TableObject*>(asObj()); }
    ClosureObject* asClosureObj() const { return reinterpret_cast<ClosureObject*>(asObj()); }
    UserdataObject* asUserdataObj() const { return reinterpret_cast<UserdataObject*>(asObj()); }
    CoroutineObject* asThreadObj() const { return reinterpret_cast<CoroutineObject*>(asObj()); }
    StringObject* asStringObj() const { return reinterpret_cast<StringObject*>(asObj()); }
    FileObject* asFileObj() const { return reinterpret_cast<FileObject*>(asObj()); }
    SocketObject* asSocketObj() const { return reinterpret_cast<SocketObject*>(asObj()); }

    // Standard methods
    bool isFalsey() const;
    bool isTruthy() const { return !isFalsey(); }
    bool operator==(const Value& other) const;
    bool operator!=(const Value& other) const { return !(*this == other); }
    bool isStringEqual(const std::string& str) const;
    size_t hash() const;

    std::string toString() const;
    std::string typeToString() const;
    void print(std::ostream& os) const;

    void serialize(std::ostream& os, const Chunk* chunk) const;
    static Value deserialize(std::istream& is, Chunk* chunk);
};

inline std::ostream& operator<<(std::ostream& os, const Value& value) {
    value.print(os);
    return os;
}

#endif // LUA_VALUE_HPP
