#ifndef LUA_VALUE_HPP
#define LUA_VALUE_HPP

#include "common/common.hpp"
#include <cstring>
#include <cmath>

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

// NaN-boxing implementation
// Uses 64-bit representation to store all value types efficiently
//
// IEEE 754 double-precision format:
// Sign (1 bit) | Exponent (11 bits) | Mantissa (52 bits)
//
// NaN values have exponent = 0x7FF and non-zero mantissa
// We use quiet NaN (mantissa bit 51 set) with additional tag bits in the mantissa.
//
// Bits 48-51 are the type tag (4 bits).
// Bits 0-47 are the payload (pointer or index).

class Value {
public:
    enum class Type {
        NIL,
        BOOL,
        NUMBER, // Float
        INTEGER,
        FUNCTION, // Compile-time function index
        STRING,   // Compile-time string index
        TABLE,    // Pointer to TableObject
        CLOSURE,  // Pointer to ClosureObject
        FILE,     // Pointer to FileObject
        SOCKET,   // Pointer to SocketObject
        RUNTIME_STRING, // Pointer to StringObject
        NATIVE_FUNCTION, // Native function index
        THREAD,   // Pointer to CoroutineObject
        USERDATA  // Pointer to UserdataObject
    };

    static constexpr int NUM_TYPES = 13;

private:
    explicit Value(uint64_t bits) : bits_(bits) {}

public:
    // Default constructor (creates nil value)
    Value() : bits_(QNAN | TAG_NIL) {}

    // Factory methods
    static Value nil() {
        return Value(QNAN | TAG_NIL);
    }

    static Value boolean(bool value) {
        return Value(QNAN | TAG_BOOL | (value ? 1 : 0));
    }

    static Value number(double value) {
        // Automatically convert to integer if it's an exact integer fitting in 48 bits
        double intPart;
        if (std::modf(value, &intPart) == 0.0 && 
            value >= -140737488355328.0 && value <= 140737488355327.0) {
            return integer(static_cast<int64_t>(value));
        }
        
        Value v(0);
        std::memcpy(&v.bits_, &value, sizeof(double));
        return v;
    }

    static Value integer(int64_t value) {
        // Store 48-bit integer
        return Value(QNAN | TAG_INTEGER | (static_cast<uint64_t>(value) & PAYLOAD_MASK));
    }

    static Value function(size_t funcIndex) {
        return Value(QNAN | TAG_FUNCTION | funcIndex);
    }

    static Value string(size_t stringIndex) {
        return Value(QNAN | TAG_STRING | stringIndex);
    }

    static Value runtimeString(StringObject* str) {
        return pointerValue(TAG_RUNTIME_STRING, str);
    }

    static Value table(TableObject* table) {
        return pointerValue(TAG_TABLE, table);
    }

    static Value closure(ClosureObject* closure) {
        return pointerValue(TAG_CLOSURE, closure);
    }

    static Value file(FileObject* file) {
        return pointerValue(TAG_FILE, file);
    }

    static Value socket(SocketObject* socket) {
        return pointerValue(TAG_SOCKET, socket);
    }

    static Value nativeFunction(size_t funcIndex) {
        return Value(QNAN | TAG_NATIVE_FUNCTION | funcIndex);
    }

    static Value thread(CoroutineObject* thread) {
        return pointerValue(TAG_THREAD, thread);
    }

    static Value userdata(UserdataObject* userdata) {
        return pointerValue(TAG_USERDATA, userdata);
    }

    // Type checking
    bool isNil() const { return bits_ == (QNAN | TAG_NIL); }
    bool isBool() const { return (bits_ & (QNAN | TAG_MASK)) == (QNAN | TAG_BOOL); }
    bool isInteger() const { return (bits_ & (QNAN | TAG_MASK)) == (QNAN | TAG_INTEGER); }
    bool isNumber() const { 
        if (isInteger()) return true; // Integers are numbers in Lua
        if ((bits_ & 0x7FF0000000000000ULL) != 0x7FF0000000000000ULL) return true;
        return (bits_ & TAG_MASK) == 0; // Infinity has 0 tag
    }
    bool isFloat() const {
        return isNumber() && !isInteger();
    }
    bool isFunctionObject() const { return (bits_ & (QNAN | TAG_MASK)) == (QNAN | TAG_FUNCTION); }
    
    bool isString() const {
        uint64_t tag = bits_ & (QNAN | TAG_MASK);
        return tag == (QNAN | TAG_STRING) || tag == (QNAN | TAG_RUNTIME_STRING);
    }
    
    bool isRuntimeString() const { return (bits_ & (QNAN | TAG_MASK)) == (QNAN | TAG_RUNTIME_STRING); }
    bool isTable() const { return (bits_ & (QNAN | TAG_MASK)) == (QNAN | TAG_TABLE); }
    bool isClosure() const { return (bits_ & (QNAN | TAG_MASK)) == (QNAN | TAG_CLOSURE); }
    bool isFile() const { return (bits_ & (QNAN | TAG_MASK)) == (QNAN | TAG_FILE); }
    bool isSocket() const { return (bits_ & (QNAN | TAG_MASK)) == (QNAN | TAG_SOCKET); }
    bool isNativeFunction() const { return (bits_ & (QNAN | TAG_MASK)) == (QNAN | TAG_NATIVE_FUNCTION); }
    bool isThread() const { return (bits_ & (QNAN | TAG_MASK)) == (QNAN | TAG_THREAD); }
    bool isUserdata() const { return (bits_ & (QNAN | TAG_MASK)) == (QNAN | TAG_USERDATA); }
    
    bool isFunction() const {
        return isFunctionObject() || isClosure() || isNativeFunction();
    }

    bool isObj() const {
        if (isNumber()) return false;
        uint64_t tag = bits_ & (QNAN | TAG_MASK);
        return tag == (QNAN | TAG_TABLE) || tag == (QNAN | TAG_CLOSURE) || tag == (QNAN | TAG_FILE) || 
               tag == (QNAN | TAG_SOCKET) || tag == (QNAN | TAG_RUNTIME_STRING) || tag == (QNAN | TAG_THREAD) ||
               tag == (QNAN | TAG_USERDATA);
    }

    Type type() const {
        if (isNumber()) return Type::NUMBER;
        if (isNil()) return Type::NIL;
        if (isBool()) return Type::BOOL;
        if (isFunctionObject()) return Type::FUNCTION;
        if (isRuntimeString()) return Type::RUNTIME_STRING;
        if (isString()) return Type::STRING;
        if (isTable()) return Type::TABLE;
        if (isClosure()) return Type::CLOSURE;
        if (isFile()) return Type::FILE;
        if (isSocket()) return Type::SOCKET;
        if (isNativeFunction()) return Type::NATIVE_FUNCTION;
        if (isThread()) return Type::THREAD;
        if (isUserdata()) return Type::USERDATA;
        return Type::NIL;
    }

    // Value extraction
    bool asBool() const { return (bits_ & 1) != 0; }

    int64_t asInteger() const {
        if (isInteger()) {
            uint64_t payload = bits_ & PAYLOAD_MASK;
            // Sign extend the 48-bit integer
            if (payload & 0x0000800000000000ULL) {
                return static_cast<int64_t>(payload | 0xFFFF000000000000ULL);
            }
            return static_cast<int64_t>(payload);
        }
        return static_cast<int64_t>(asNumber());
    }

    double asNumber() const {
        if (isInteger()) {
            return static_cast<double>(asInteger());
        }
        double result;
        std::memcpy(&result, &bits_, sizeof(double));
        return result;
    }

    size_t asFunctionIndex() const { return static_cast<size_t>(bits_ & PAYLOAD_MASK); }
    size_t asStringIndex() const { return static_cast<size_t>(bits_ & PAYLOAD_MASK); }
    size_t asNativeFunctionIndex() const { return static_cast<size_t>(bits_ & PAYLOAD_MASK); }

    GCObject* asObj() const { return reinterpret_cast<GCObject*>(bits_ & PAYLOAD_MASK); }
    StringObject* asStringObj() const { return reinterpret_cast<StringObject*>(bits_ & PAYLOAD_MASK); }
    TableObject* asTableObj() const { return reinterpret_cast<TableObject*>(bits_ & PAYLOAD_MASK); }
    ClosureObject* asClosureObj() const { return reinterpret_cast<ClosureObject*>(bits_ & PAYLOAD_MASK); }
    FileObject* asFileObj() const { return reinterpret_cast<FileObject*>(bits_ & PAYLOAD_MASK); }
    SocketObject* asSocketObj() const { return reinterpret_cast<SocketObject*>(bits_ & PAYLOAD_MASK); }
    CoroutineObject* asThreadObj() const { return reinterpret_cast<CoroutineObject*>(bits_ & PAYLOAD_MASK); }
    UserdataObject* asUserdataObj() const { return reinterpret_cast<UserdataObject*>(bits_ & PAYLOAD_MASK); }

    // Equality and Hashing
    bool operator==(const Value& other) const;
    bool operator!=(const Value& other) const { return !(*this == other); }
    bool equals(const Value& other) const { return *this == other; }
    bool isStringEqual(const std::string& str) const;
    size_t hash() const;

    // Truthiness
    bool isFalsey() const { return isNil() || (isBool() && !asBool()); }
    bool isTruthy() const { return !isFalsey(); }

    // String representation
    std::string toString() const;
    std::string typeToString() const;

    void print(std::ostream& os) const;

    // Serialization
    void serialize(std::ostream& os, const Chunk* chunk) const;
    static Value deserialize(std::istream& is, Chunk* chunk);

    uint64_t bits() const { return bits_; }

private:
    uint64_t bits_;

    static Value pointerValue(uint64_t tag, void* ptr) {
        return Value(QNAN | tag | (reinterpret_cast<uint64_t>(ptr) & PAYLOAD_MASK));
    }

    // NaN-boxing constants (4-bit tags in bits 48-51)
    static constexpr uint64_t QNAN         = 0x7FF0000000000000ULL;
    static constexpr uint64_t TAG_NIL      = 0x0001000000000000ULL;
    static constexpr uint64_t TAG_BOOL     = 0x0002000000000000ULL;
    static constexpr uint64_t TAG_FUNCTION = 0x0003000000000000ULL;
    static constexpr uint64_t TAG_STRING   = 0x0004000000000000ULL;
    static constexpr uint64_t TAG_TABLE    = 0x0005000000000000ULL;
    static constexpr uint64_t TAG_CLOSURE  = 0x0006000000000000ULL;
    static constexpr uint64_t TAG_FILE     = 0x0007000000000000ULL;
    static constexpr uint64_t TAG_SOCKET   = 0x0009000000000000ULL; // Skip 8 (often Quiet NaN bit)
    static constexpr uint64_t TAG_RUNTIME_STRING = 0x000A000000000000ULL;
    static constexpr uint64_t TAG_NATIVE_FUNCTION = 0x000B000000000000ULL;
    static constexpr uint64_t TAG_THREAD   = 0x000C000000000000ULL;
    static constexpr uint64_t TAG_INTEGER  = 0x000D000000000000ULL;
    static constexpr uint64_t TAG_USERDATA = 0x000E000000000000ULL;
    
    static constexpr uint64_t TAG_MASK     = 0x000F000000000000ULL;
    static constexpr uint64_t PAYLOAD_MASK = 0x0000FFFFFFFFFFFFULL;
};

inline std::ostream& operator<<(std::ostream& os, const Value& value) {
    value.print(os);
    return os;
}

#endif // LUA_VALUE_HPP
