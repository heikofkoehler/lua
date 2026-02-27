#ifndef LUA_VALUE_HPP
#define LUA_VALUE_HPP

#include "common/common.hpp"
#include <cstring>
#include <cmath>

// Forward declaration
class FunctionObject;
class Chunk;

// NaN-boxing implementation
// Uses 64-bit representation to store all value types efficiently
//
// IEEE 754 double-precision format:
// Sign (1 bit) | Exponent (11 bits) | Mantissa (52 bits)
//
// NaN values have exponent = 0x7FF and non-zero mantissa
// We use quiet NaN (mantissa bit 51 set) with additional tag bits
//
// Encoding:
// - Numbers: stored directly as IEEE 754 doubles
// - Other types: quiet NaN with type tags in lower bits

class Value {
public:
    enum class Type {
        NIL,
        BOOL,
        NUMBER,
        FUNCTION,
        STRING,
        TABLE,
        CLOSURE,
        FILE,
        SOCKET,
        NATIVE_FUNCTION,
        THREAD
    };

    // Constructors (private, use factory methods)
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
        Value v(0);
        std::memcpy(&v.bits_, &value, sizeof(double));
        return v;
    }

    static Value function(size_t funcIndex) {
        // Encode function index (not pointer!)
        // Index fits easily in lower 48 bits
        return Value(QNAN | TAG_FUNCTION | (funcIndex << 4));
    }

    static Value string(size_t stringIndex) {
        // Encode string index (not pointer!)
        // Index fits easily in lower 48 bits
        // This is for compile-time strings from chunk
        return Value(QNAN | TAG_STRING | (stringIndex << 4));
    }

    static Value runtimeString(size_t stringIndex) {
        // Encode runtime string index (not pointer!)
        // This is for runtime strings from VM pool (e.g., from IO)
        return Value(QNAN | TAG_RUNTIME_STRING | (stringIndex << 4));
    }

    static Value table(size_t tableIndex) {
        // Encode table index (not pointer!)
        // Index fits easily in lower 48 bits
        return Value(QNAN | TAG_TABLE | (tableIndex << 4));
    }

    static Value closure(size_t closureIndex) {
        // Encode closure index (not pointer!)
        // Index fits easily in lower 48 bits
        return Value(QNAN | TAG_CLOSURE | (closureIndex << 4));
    }

    static Value file(size_t fileIndex) {
        // Encode file index (not pointer!)
        // Index fits easily in lower 48 bits
        return Value(QNAN | TAG_FILE | (fileIndex << 4));
    }

    static Value socket(size_t socketIndex) {
        // Encode socket index (not pointer!)
        // Index fits easily in lower 48 bits
        return Value(QNAN | TAG_SOCKET | (socketIndex << 4));
    }

    static Value nativeFunction(size_t funcIndex) {
        // Encode native function index (not pointer!)
        // Index fits easily in lower 48 bits
        return Value(QNAN | TAG_NATIVE_FUNCTION | (funcIndex << 4));
    }

    static Value thread(size_t threadIndex) {
        // Encode thread index
        return Value(QNAN | TAG_THREAD | (threadIndex << 4));
    }

    // Type checking
    bool isNil() const {
        return (bits_ & (QNAN | TAG_MASK)) == (QNAN | TAG_NIL);
    }

    bool isBool() const {
        return (bits_ & (QNAN | (TAG_MASK & ~1ULL))) == (QNAN | TAG_BOOL);
    }

    bool isNumber() const {
        return (bits_ & QNAN) != QNAN;
    }

    bool isFunctionObject() const {
        return (bits_ & (QNAN | TAG_MASK)) == (QNAN | TAG_FUNCTION);
    }

    bool isString() const {
        uint64_t tag = bits_ & (QNAN | TAG_MASK);
        return tag == (QNAN | TAG_STRING) || tag == (QNAN | TAG_RUNTIME_STRING);
    }

    bool isRuntimeString() const {
        return (bits_ & (QNAN | TAG_MASK)) == (QNAN | TAG_RUNTIME_STRING);
    }

    bool isTable() const {
        return (bits_ & (QNAN | TAG_MASK)) == (QNAN | TAG_TABLE);
    }

    bool isClosure() const {
        return (bits_ & (QNAN | TAG_MASK)) == (QNAN | TAG_CLOSURE);
    }

    bool isFile() const {
        return (bits_ & (QNAN | TAG_MASK)) == (QNAN | TAG_FILE);
    }

    bool isSocket() const {
        return (bits_ & (QNAN | TAG_MASK)) == (QNAN | TAG_SOCKET);
    }

    bool isNativeFunction() const {
        return (bits_ & (QNAN | TAG_MASK)) == (QNAN | TAG_NATIVE_FUNCTION);
    }

    bool isThread() const {
        return (bits_ & (QNAN | TAG_MASK)) == (QNAN | TAG_THREAD);
    }
    
    bool isFunction() const {
        return isFunctionObject() || isClosure() || isNativeFunction();
    }

    Type type() const {
        if (isNumber()) return Type::NUMBER;
        if (isBool()) return Type::BOOL;
        if (isFunctionObject()) return Type::FUNCTION;
        if (isString()) return Type::STRING;
        if (isTable()) return Type::TABLE;
        if (isClosure()) return Type::CLOSURE;
        if (isFile()) return Type::FILE;
        if (isSocket()) return Type::SOCKET;
        if (isNativeFunction()) return Type::NATIVE_FUNCTION;
        if (isThread()) return Type::THREAD;
        return Type::NIL;
    }

    // Value extraction
    bool asBool() const {
        if (!isBool()) {
            throw RuntimeError("Value is not a boolean");
        }
        return (bits_ & 1) != 0;
    }

    double asNumber() const {
        if (!isNumber()) {
            throw RuntimeError("Value is not a number");
        }
        double result;
        std::memcpy(&result, &bits_, sizeof(double));
        return result;
    }

    size_t asFunctionIndex() const {
        if (!isFunctionObject()) {
            throw RuntimeError("Value is not a function");
        }
        // Extract function index (shift right by 4 to undo the encoding)
        uint64_t index = (bits_ & 0x0000FFFFFFFFFFF0ULL) >> 4;
        return static_cast<size_t>(index);
    }

    size_t asStringIndex() const {
        if (!isString()) {
            throw RuntimeError("Value is not a string");
        }
        // Extract string index (shift right by 4 to undo the encoding)
        uint64_t index = (bits_ & 0x0000FFFFFFFFFFF0ULL) >> 4;
        return static_cast<size_t>(index);
    }

    size_t asTableIndex() const {
        if (!isTable()) {
            throw RuntimeError("Value is not a table");
        }
        // Extract table index (shift right by 4 to undo the encoding)
        uint64_t index = (bits_ & 0x0000FFFFFFFFFFF0ULL) >> 4;
        return static_cast<size_t>(index);
    }

    size_t asClosureIndex() const {
        if (!isClosure()) {
            throw RuntimeError("Value is not a closure");
        }
        // Extract closure index (shift right by 4 to undo the encoding)
        uint64_t index = (bits_ & 0x0000FFFFFFFFFFF0ULL) >> 4;
        return static_cast<size_t>(index);
    }

    size_t asFileIndex() const {
        if (!isFile()) {
            throw RuntimeError("Value is not a file");
        }
        // Extract file index (shift right by 4 to undo the encoding)
        uint64_t index = (bits_ & 0x0000FFFFFFFFFFF0ULL) >> 4;
        return static_cast<size_t>(index);
    }

    size_t asSocketIndex() const {
        if (!isSocket()) {
            throw RuntimeError("Value is not a socket");
        }
        // Extract socket index (shift right by 4 to undo the encoding)
        uint64_t index = (bits_ & 0x0000FFFFFFFFFFF0ULL) >> 4;
        return static_cast<size_t>(index);
    }

    size_t asNativeFunctionIndex() const {
        if (!isNativeFunction()) {
            throw RuntimeError("Value is not a native function");
        }
        // Extract native function index (shift right by 4 to undo the encoding)
        uint64_t index = (bits_ & 0x0000FFFFFFFFFFF0ULL) >> 4;
        return static_cast<size_t>(index);
    }

    size_t asThreadIndex() const {
        if (!isThread()) {
            throw RuntimeError("Value is not a thread");
        }
        uint64_t index = (bits_ & 0x0000FFFFFFFFFFF0ULL) >> 4;
        return static_cast<size_t>(index);
    }

    // Equality
    bool operator==(const Value& other) const {
        if (type() != other.type()) return false;
        
        if (isNumber()) {
            double a = asNumber();
            double b = other.asNumber();
            // NaN != NaN in Lua
            if (std::isnan(a) && std::isnan(b)) return false;
            return a == b;
        }
        return bits_ == other.bits_;
    }

    bool operator!=(const Value& other) const {
        return !(*this == other);
    }

    // Truthiness (for logical operations)
    bool isFalsey() const {
        return isNil() || (isBool() && !asBool());
    }

    bool isTruthy() const {
        return !isFalsey();
    }

    // String representation
    std::string toString() const;
    
    // Type name representation
    std::string typeToString() const {
        if (isNumber()) return "number";
        if (isBool()) return "boolean";
        if (isNil()) return "nil";
        if (isString()) return "string";
        if (isTable()) return "table";
        if (isNativeFunction() || isClosure() || isFunctionObject()) return "function";
        if (isThread()) return "thread";
        if (isFile()) return "userdata"; // Lua calls file userdata usually
        if (isSocket()) return "userdata";
        return "unknown";
    }

    // Print to stream
    void print(std::ostream& os) const;

    // Serialization
    void serialize(std::ostream& os, const Chunk* chunk) const;
    static Value deserialize(std::istream& is, Chunk* chunk);

    uint64_t bits() const { return bits_; }

    void normalize() {
        if ((bits_ & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL) {
            // If it's a NaN, normalize it to a specific NaN bit pattern
            // that is NOT in our tagged range.
            // Our tagged range is [0xFFF1, 0xFFFF] in the top 16 bits.
            // IEEE 754 NaNs are [0x7FF1, 0x7FFF] or [0xFFF1, 0xFFFF].
            // Wait, our tags are in the LOW bits!
            // That's much safer.
            
            // To be absolutely sure, let's normalize all NaNs to a single bit pattern.
            if ((bits_ & 0x000FFFFFFFFFFFFFULL) != 0) {
                bits_ = 0x7FF8000000000000ULL;
            }
        }
    }

private:
    uint64_t bits_;

    // NaN-boxing constants
    static constexpr uint64_t QNAN = 0x7FFC000000000000ULL;
    static constexpr uint64_t TAG_NIL = 1;
    static constexpr uint64_t TAG_BOOL = 2;
    static constexpr uint64_t TAG_FUNCTION = 3;
    static constexpr uint64_t TAG_STRING = 4;  // Compile-time string (from chunk)
    static constexpr uint64_t TAG_TABLE = 5;
    static constexpr uint64_t TAG_CLOSURE = 6;
    static constexpr uint64_t TAG_FILE = 7;
    static constexpr uint64_t TAG_SOCKET = 8;
    static constexpr uint64_t TAG_RUNTIME_STRING = 9;  // Runtime string (from VM pool)
    static constexpr uint64_t TAG_NATIVE_FUNCTION = 10;  // Native function (C++ function pointer)
    static constexpr uint64_t TAG_THREAD = 11;           // Coroutine object
    static constexpr uint64_t TAG_MASK = 15;  // 4 bits
};

// Stream operator
inline std::ostream& operator<<(std::ostream& os, const Value& value) {
    value.print(os);
    return os;
}

#endif // LUA_VALUE_HPP
