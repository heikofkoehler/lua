#ifndef LUA_VALUE_HPP
#define LUA_VALUE_HPP

#include "common/common.hpp"
#include <cstring>
#include <cmath>
#include <cstdint>
#include "value/int64.hpp"

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
class Int64Object;

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
        INT64           = 0xFFFF,
        
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
        // Canonicalize NaN: raw NaN payloads produced by FP arithmetic (e.g.
        // 0/0 yields 0xFFF8... on x86-64) can collide with the NaN-boxed tag
        // space (0xFFF8 is the NATIVE_FUNCTION tag), which would decode the
        // NaN as a function value. Funneling every NaN through one canonical
        // positive quiet NaN keeps all NaNs decodable as numbers on every
        // platform. This is the single choke point: VM arithmetic, the C API,
        // the parser, and chunk deserialization all construct doubles here.
        if (value != value) {
            return Value(0x7FF8000000000000ULL);
        }
        uint64_t bits;
        std::memcpy(&bits, &value, sizeof(double));
        return Value(bits);
    }

    static constexpr uint64_t FLAG_COMPILE_TIME = (1ULL << 47);

    static constexpr Value integer(int64_t value) {
        return Value(encodeTag(Type::INTEGER) | (static_cast<uint64_t>(value) & 0x0000FFFFFFFFFFFFULL));
    }

    static Value fromInt64(Int64Object* obj) {
        return Value(encodeTag(Type::INT64) | (reinterpret_cast<uint64_t>(obj) & 0x00007FFFFFFFFFFFULL));
    }

    static constexpr Value compileTimeInt64(size_t index) {
        return Value(encodeTag(Type::INT64) | FLAG_COMPILE_TIME | (static_cast<uint64_t>(index) & 0x00007FFFFFFFFFFFULL));
    }

    static constexpr Value function(size_t funcIndex) {
        return Value(encodeTag(Type::FUNCTION) | (static_cast<uint64_t>(funcIndex) & 0x00007FFFFFFFFFFFULL));
    }

    static constexpr Value string(size_t stringIndex) {
        return Value(encodeTag(Type::STRING) | FLAG_COMPILE_TIME | (static_cast<uint64_t>(stringIndex) & 0x00007FFFFFFFFFFFULL));
    }

    static Value runtimeString(StringObject* str) {
        return Value(encodeTag(Type::STRING) | (reinterpret_cast<uint64_t>(str) & 0x00007FFFFFFFFFFFULL));
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
    constexpr uint64_t bits() const { return bits_; }

    // Type checking
    bool isFloat() const { 
        return (bits_ >> 48) < 0xFFF1;
    }
    bool isNumber() const { return isFloat() || isInteger(); }
    bool isNil() const { return bits_ == encodeTag(Type::NIL); }
    bool isBool() const { return (bits_ & TAG_MASK) == encodeTag(Type::BOOL); }
    bool isInteger() const {
        uint64_t tag = bits_ & TAG_MASK;
        return tag == encodeTag(Type::INTEGER) || tag == encodeTag(Type::INT64);
    }
    bool isInt64() const { return (bits_ & TAG_MASK) == encodeTag(Type::INT64); }
    bool isRuntimeInt64() const { return isInt64() && !(bits_ & FLAG_COMPILE_TIME); }
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
    bool isRuntimeString() const { return isString() && !(bits_ & FLAG_COMPILE_TIME); }

    bool isFunction() const {
        return isFunctionObject() || isClosure() || isNativeFunction() || isCFunction();
    }

    bool isObj() const {
        // Most tagged types in our VM are GC objects if they aren't numbers, bools, nils or indices.
        return isTable() || isClosure() || isUserdata() || isThread() || isFile() || isSocket() || isRuntimeString() || isRuntimeInt64();
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
            return static_cast<double>(asInteger());
        }
        union { uint64_t b; double d; } u;
        u.b = bits_;
        return u.d;
    }

    int64_t asInteger() const {
        uint64_t tag = bits_ & TAG_MASK;
        if (tag == encodeTag(Type::INTEGER)) {
            uint64_t payload = bits_ & 0x0000FFFFFFFFFFFFULL;
            if (payload & (1ULL << 47)) {
                return static_cast<int64_t>(payload | 0xFFFF000000000000ULL);
            }
            return static_cast<int64_t>(payload);
        }
        if (tag == encodeTag(Type::INT64)) {
            if (isRuntimeInt64()) {
                return reinterpret_cast<const Int64Object*>(bits_ & 0xFFFFFFFFFFFFULL)->value;
            }
            return 0;
        }
        return static_cast<int64_t>(asNumber());
    }

    size_t asInt64Index() const { return static_cast<size_t>(bits_ & 0x00007FFFFFFFFFFFULL); }
    Int64Object* asInt64Obj() const { return reinterpret_cast<Int64Object*>(bits_ & 0x00007FFFFFFFFFFFULL); }

    size_t asFunctionIndex() const { return static_cast<size_t>(bits_ & 0x00007FFFFFFFFFFFULL); }
    size_t asStringIndex() const { return static_cast<size_t>(bits_ & 0x00007FFFFFFFFFFFULL); }
    size_t asNativeFunctionIndex() const { return static_cast<size_t>(bits_ & 0x00007FFFFFFFFFFFULL); }
    void* asCFunction() const { return reinterpret_cast<void*>(bits_ & 0x00007FFFFFFFFFFFULL); }

    GCObject* asObj() const { return reinterpret_cast<GCObject*>(bits_ & 0x00007FFFFFFFFFFFULL); }
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

    void serialize(std::ostream& os, const Chunk* chunk, const std::string& parentSource = "", bool strip = false) const;
    static Value deserialize(std::istream& is, Chunk* chunk, const std::string& parentSource = "");
};

static_assert(sizeof(Value) == 8, "sizeof(Value) must be 8 bytes");

inline std::ostream& operator<<(std::ostream& os, const Value& value) {
    value.print(os);
    return os;
}

namespace std {
template <>
struct hash<Value> {
    size_t operator()(const Value& v) const noexcept {
        return v.hash();
    }
};
}

#endif // LUA_VALUE_HPP
