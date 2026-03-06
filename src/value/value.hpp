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

class Value {
public:
    enum class Type : uint8_t {
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
        C_FUNCTION,      // lua_CFunction pointer
        THREAD,   // Pointer to CoroutineObject
        USERDATA  // Pointer to UserdataObject
    };

    static constexpr int NUM_TYPES = 15;

private:
    Type type_;
    union {
        bool boolean_;
        double number_;
        int64_t integer_;
        size_t index_;
        GCObject* obj_;
        void* cfunc_; // for lua_CFunction
    };

public:
    Value() : type_(Type::NIL) { integer_ = 0; }

    static Value nil() {
        return Value();
    }

    static Value cFunction(void* f) {
        Value v;
        v.type_ = Type::C_FUNCTION;
        v.cfunc_ = f;
        return v;
    }

    static Value boolean(bool value) {
        Value v;
        v.type_ = Type::BOOL;
        v.boolean_ = value;
        return v;
    }

    static Value number(double value) {
        Value v;
        v.type_ = Type::NUMBER;
        v.number_ = value;
        return v;
    }

    static Value integer(int64_t value) {
        Value v;
        v.type_ = Type::INTEGER;
        v.integer_ = value;
        return v;
    }

    static Value function(size_t funcIndex) {
        Value v;
        v.type_ = Type::FUNCTION;
        v.index_ = funcIndex;
        return v;
    }

    static Value string(size_t stringIndex) {
        Value v;
        v.type_ = Type::STRING;
        v.index_ = stringIndex;
        return v;
    }

    static Value runtimeString(StringObject* str) {
        Value v;
        v.type_ = Type::RUNTIME_STRING;
        v.obj_ = reinterpret_cast<GCObject*>(str);
        return v;
    }

    static Value table(TableObject* table) {
        Value v;
        v.type_ = Type::TABLE;
        v.obj_ = reinterpret_cast<GCObject*>(table);
        return v;
    }

    static Value closure(ClosureObject* closure) {
        Value v;
        v.type_ = Type::CLOSURE;
        v.obj_ = reinterpret_cast<GCObject*>(closure);
        return v;
    }

    static Value file(FileObject* file) {
        Value v;
        v.type_ = Type::FILE;
        v.obj_ = reinterpret_cast<GCObject*>(file);
        return v;
    }

    static Value socket(SocketObject* socket) {
        Value v;
        v.type_ = Type::SOCKET;
        v.obj_ = reinterpret_cast<GCObject*>(socket);
        return v;
    }

    static Value nativeFunction(size_t funcIndex) {
        Value v;
        v.type_ = Type::NATIVE_FUNCTION;
        v.index_ = funcIndex;
        return v;
    }

    static Value thread(CoroutineObject* thread) {
        Value v;
        v.type_ = Type::THREAD;
        v.obj_ = reinterpret_cast<GCObject*>(thread);
        return v;
    }

    static Value userdata(UserdataObject* userdata) {
        Value v;
        v.type_ = Type::USERDATA;
        v.obj_ = reinterpret_cast<GCObject*>(userdata);
        return v;
    }

    static Value fromObj(GCObject* obj);

    // Type checking
    bool isNil() const { return type_ == Type::NIL; }
    bool isBool() const { return type_ == Type::BOOL; }
    bool isInteger() const { return type_ == Type::INTEGER; }
    bool isFloat() const { return type_ == Type::NUMBER; }
    bool isNumber() const { return type_ == Type::NUMBER || type_ == Type::INTEGER; }
    
    bool isFunctionObject() const { return type_ == Type::FUNCTION; }
    bool isString() const { return type_ == Type::STRING || type_ == Type::RUNTIME_STRING; }
    
    bool isRuntimeString() const { return type_ == Type::RUNTIME_STRING; }
    bool isTable() const { return type_ == Type::TABLE; }
    bool isClosure() const { return type_ == Type::CLOSURE; }
    bool isFile() const { return type_ == Type::FILE; }
    bool isSocket() const { return type_ == Type::SOCKET; }
    bool isNativeFunction() const { return type_ == Type::NATIVE_FUNCTION; }
    bool isCFunction() const { return type_ == Type::C_FUNCTION; }
    bool isThread() const { return type_ == Type::THREAD; }
    bool isUserdata() const { return type_ == Type::USERDATA; }

    bool isFunction() const {
        return isFunctionObject() || isClosure() || isNativeFunction() || isCFunction();
    }

    bool isObj() const {
        return type_ == Type::TABLE || type_ == Type::CLOSURE || type_ == Type::FILE || 
               type_ == Type::SOCKET || type_ == Type::RUNTIME_STRING || type_ == Type::THREAD ||
               type_ == Type::USERDATA;
    }

    Type type() const { return type_; }

    // Value extraction
    bool asBool() const { return boolean_; }

    int64_t asInteger() const {
        if (isInteger()) return integer_;
        return static_cast<int64_t>(number_);
    }

    double asNumber() const {
        if (isInteger()) return static_cast<double>(integer_);
        return number_;
    }

    size_t asFunctionIndex() const { return index_; }
    size_t asStringIndex() const { return index_; }
    size_t asNativeFunctionIndex() const { return index_; }
    void* asCFunction() const { return cfunc_; }
    
    GCObject* asObj() const { return obj_; }
    StringObject* asStringObj() const { return reinterpret_cast<StringObject*>(obj_); }
    TableObject* asTableObj() const { return reinterpret_cast<TableObject*>(obj_); }
    ClosureObject* asClosureObj() const { return reinterpret_cast<ClosureObject*>(obj_); }
    FileObject* asFileObj() const { return reinterpret_cast<FileObject*>(obj_); }
    SocketObject* asSocketObj() const { return reinterpret_cast<SocketObject*>(obj_); }
    CoroutineObject* asThreadObj() const { return reinterpret_cast<CoroutineObject*>(obj_); }
    UserdataObject* asUserdataObj() const { return reinterpret_cast<UserdataObject*>(obj_); }

    // Operations
    bool isFalsey() const;
    bool isTruthy() const { return !isFalsey(); }
    bool operator==(const Value& other) const;
    bool operator!=(const Value& other) const { return !(*this == other); }
    bool isStringEqual(const std::string& str) const;
    size_t hash() const;

    // String conversion
    std::string toString() const;
    std::string typeToString() const;

    void print(std::ostream& os) const;

    // Serialization
    void serialize(std::ostream& os, const Chunk* chunk) const;
    static Value deserialize(std::istream& is, Chunk* chunk);
};

inline std::ostream& operator<<(std::ostream& os, const Value& value) {
    value.print(os);
    return os;
}

#endif // LUA_VALUE_HPP
