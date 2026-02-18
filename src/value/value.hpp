#ifndef LUA_VALUE_HPP
#define LUA_VALUE_HPP

#include "common/common.hpp"
#include <cstring>
#include <cmath>

// Forward declaration
class FunctionObject;

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
        FUNCTION
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

    static Value function(FunctionObject* func) {
        // Encode pointer in lower 48 bits with function tag
        uint64_t ptr = reinterpret_cast<uint64_t>(func);
        return Value(QNAN | TAG_FUNCTION | (ptr & 0x0000FFFFFFFFFFFFULL));
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

    Type type() const {
        if (isNumber()) return Type::NUMBER;
        if (isBool()) return Type::BOOL;
        if (isFunctionObject()) return Type::FUNCTION;
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

    FunctionObject* asFunctionObject() const {
        if (!isFunctionObject()) {
            throw RuntimeError("Value is not a function");
        }
        // Extract pointer (remove QNAN and TAG bits)
        uint64_t ptr = bits_ & ~(QNAN | TAG_MASK);
        return reinterpret_cast<FunctionObject*>(ptr);
    }

    // Equality
    bool operator==(const Value& other) const {
        // Special handling for NaN
        if (isNumber() && other.isNumber()) {
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

    // Print to stream
    void print(std::ostream& os) const;

private:
    uint64_t bits_;

    // NaN-boxing constants
    static constexpr uint64_t QNAN = 0x7FFC000000000000ULL;
    static constexpr uint64_t TAG_NIL = 1;
    static constexpr uint64_t TAG_BOOL = 2;
    static constexpr uint64_t TAG_FUNCTION = 3;
    static constexpr uint64_t TAG_MASK = 7;
};

// Stream operator
inline std::ostream& operator<<(std::ostream& os, const Value& value) {
    value.print(os);
    return os;
}

#endif // LUA_VALUE_HPP
