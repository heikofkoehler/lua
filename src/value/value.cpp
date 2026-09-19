#include "value/value.hpp"
#include "value/function.hpp"
#include "value/string.hpp"
#include "value/table.hpp"
#include "value/closure.hpp"
#include "value/file.hpp"
#include "value/socket.hpp"
#include "value/coroutine.hpp"
#include "value/userdata.hpp"
#include "compiler/chunk.hpp"
#include <sstream>
#include <iomanip>
#include <iostream>
#include <cstdio>
#include <cstdlib>

Value Value::fromObj(GCObject* obj) {
    if (!obj) return nil();
    switch (obj->type()) {
        case GCObject::Type::STRING: return runtimeString(static_cast<StringObject*>(obj));
        case GCObject::Type::TABLE: return table(static_cast<TableObject*>(obj));
        case GCObject::Type::CLOSURE: return closure(static_cast<ClosureObject*>(obj));
        case GCObject::Type::UPVALUE: return nil(); // Upvalues are internal
        case GCObject::Type::FILE: return file(static_cast<FileObject*>(obj));
        case GCObject::Type::SOCKET: return socket(static_cast<SocketObject*>(obj));
        case GCObject::Type::USERDATA: return userdata(static_cast<UserdataObject*>(obj));
        case GCObject::Type::COROUTINE: return thread(static_cast<CoroutineObject*>(obj));
        case GCObject::Type::INT64: return fromInt64(static_cast<Int64Object*>(obj));
        default: return nil();
    }
}

std::string Value::toString() const {
    std::ostringstream oss;
    print(oss);
    return oss.str();
}

std::string Value::typeToString() const {
    switch (type()) {
        case Type::NUMBER: 
        case Type::INTEGER:
        case Type::INT64: return "number";
        case Type::BOOL: return "boolean";
        case Type::NIL: return "nil";
        case Type::STRING: return "string";
        case Type::TABLE: return "table";
        case Type::FUNCTION:
        case Type::CLOSURE:
        case Type::NATIVE_FUNCTION:
        case Type::C_FUNCTION: return "function";
        case Type::THREAD: return "thread";
        case Type::USERDATA: return "userdata";
        case Type::FILE:
        case Type::SOCKET: return "userdata";
        default: return "unknown";
    }
}

void Value::print(std::ostream& os) const {
    switch (type()) {
        case Type::NIL: os << "nil"; break;
        case Type::BOOL: os << (asBool() ? "true" : "false"); break;
        case Type::INTEGER: 
        case Type::INT64:
            if (isInt64() && !isRuntimeInt64()) {
                os << "<int64:" << asInt64Index() << ">";
            } else {
                os << asInteger(); 
            }
            break;
        case Type::NUMBER: {
            double num = asNumber();
            if (std::isinf(num)) {
                if (num < 0) os << "-";
                os << "inf";
            } else if (std::isnan(num)) {
                os << "nan";
            } else {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%.15g", num);
                if (std::strtod(buf, nullptr) != num) {
                    std::snprintf(buf, sizeof(buf), "%.17g", num);
                }
                std::string res(buf);
                if (res.find('.') == std::string::npos && res.find('e') == std::string::npos && res.find('E') == std::string::npos) {
                    res += ".0";
                }
                os << res;
            }
            break;
        }
        case Type::FUNCTION: os << "<function:" << asFunctionIndex() << ">"; break;
        case Type::STRING: 
            if (isRuntimeString()) {
                os.write(asStringObj()->chars(), asStringObj()->length()); 
            } else {
                os << "<string:" << asStringIndex() << ">"; 
            }
            break;
        case Type::TABLE: os << "table: " << asTableObj(); break;
        case Type::CLOSURE: os << "function: " << asClosureObj(); break;
        case Type::FILE: os << "file: " << asFileObj(); break;
        case Type::SOCKET: os << "socket: " << asSocketObj(); break;
        case Type::NATIVE_FUNCTION: 
            os << "function: 0x" << std::hex << (0x10000000ULL | asNativeFunctionIndex()) << std::dec; 
            break;
        case Type::C_FUNCTION: 
            os << "function: " << asCFunction(); 
            break;
        case Type::THREAD: os << "thread: " << asThreadObj(); break;
        case Type::USERDATA: os << "userdata: " << asUserdataObj(); break;
        case Type::UPVALUE: os << "upvalue: " << asObj(); break;
    }
}

bool Value::isFalsey() const {
    return isNil() || (isBool() && !asBool());
}

bool Value::operator==(const Value& other) const {
    if (isInt64() && !isRuntimeInt64()) {
        return other.isInt64() && !other.isRuntimeInt64() && bits_ == other.bits_;
    }
    if (other.isInt64() && !other.isRuntimeInt64()) {
        return false;
    }
    if (isInteger() && other.isInteger()) {
        return asInteger() == other.asInteger();
    }
    if (isInteger() && other.isFloat()) {
        double d = other.asNumber();
        if (std::isnan(d) || d < -9223372036854775808.0 || d >= 9223372036854775808.0) return false;
        double intpart;
        if (std::modf(d, &intpart) == 0.0) {
            return asInteger() == static_cast<int64_t>(d);
        }
        return false;
    }
    if (isFloat() && other.isInteger()) {
        double d = asNumber();
        if (std::isnan(d) || d < -9223372036854775808.0 || d >= 9223372036854775808.0) return false;
        double intpart;
        if (std::modf(d, &intpart) == 0.0) {
            return static_cast<int64_t>(d) == other.asInteger();
        }
        return false;
    }
    if (isNumber() && other.isNumber()) {
        return asNumber() == other.asNumber();
    }
    
    if (type() != other.type()) {
        return false;
    }

    if (isString()) {
        if (isRuntimeString() && other.isRuntimeString()) {
            return asStringObj()->equals(other.asStringObj());
        }
        return bits_ == other.bits_; // If both compile time
    }

    switch (type()) {
        case Type::NIL: return true;
        case Type::BOOL: return asBool() == other.asBool();
        case Type::STRING: {
            if (asObj() == other.asObj()) return true;
            if (isRuntimeString() && other.isRuntimeString()) {
                return asStringObj()->equals(other.asStringObj());
            }
            return false;
        }
        case Type::FUNCTION: return asFunctionIndex() == other.asFunctionIndex();
        case Type::NATIVE_FUNCTION: return asNativeFunctionIndex() == other.asNativeFunctionIndex();
        case Type::C_FUNCTION: return asCFunction() == other.asCFunction();
        default: return asObj() == other.asObj();
    }
}

bool Value::isStringEqual(const std::string& str) const {
    if (isRuntimeString()) {
        StringObject* obj = asStringObj();
        return obj->length() == str.length() && 
               std::memcmp(obj->chars(), str.data(), str.length()) == 0;
    }
    return false; // Compile-time strings must be interned first
}

size_t Value::hash() const {
    if (isInt64() && !isRuntimeInt64()) {
        return std::hash<size_t>()(asInt64Index());
    }
    if (isInteger()) {
        return std::hash<int64_t>()(asInteger());
    }
    if (isFloat()) {
        double n = asNumber();
        double intPart;
        if (std::isfinite(n) && std::modf(n, &intPart) == 0.0 &&
            n >= -9223372036854775808.0 && n < 9223372036854775808.0) {
            return std::hash<int64_t>()(static_cast<int64_t>(n));
        }
        return std::hash<double>()(n);
    }

    switch (type()) {
        case Type::BOOL: return std::hash<bool>()(asBool());
        case Type::STRING: {
            if (isRuntimeString()) {
                return asStringObj()->hash();
            }
            return std::hash<size_t>()(asStringIndex());
        }
        case Type::NATIVE_FUNCTION: return std::hash<size_t>()(asNativeFunctionIndex());
        case Type::C_FUNCTION: return std::hash<void*>()(asCFunction());
        case Type::FUNCTION: return std::hash<size_t>()(asFunctionIndex());
        case Type::NIL: return 0;
        default: return std::hash<void*>()(reinterpret_cast<void*>(asObj()));
    }
}

void Value::serialize(std::ostream& os, const Chunk* chunk, const std::string& parentSource) const {
    uint16_t t = static_cast<uint16_t>(type());
    os.write(reinterpret_cast<const char*>(&t), sizeof(t));
    
    switch (type()) {
        case Type::NIL: break;
        case Type::BOOL: {
            uint8_t b = asBool() ? 1 : 0;
            os.write(reinterpret_cast<const char*>(&b), sizeof(b));
            break;
        }
        case Type::NUMBER: {
            double n = asNumber();
            os.write(reinterpret_cast<const char*>(&n), sizeof(n));
            break;
        }
        case Type::INTEGER:
        case Type::INT64: {
            int64_t n = asInteger();
            os.write(reinterpret_cast<const char*>(&n), sizeof(n));
            break;
        }
        case Type::STRING: {
            const char* chars = nullptr;
            uint32_t len = 0;
            if (isRuntimeString()) {
                StringObject* str = asStringObj();
                chars = str->chars();
                len = static_cast<uint32_t>(str->length());
            } else {
                StringObject* str = chunk->getString(asStringIndex());
                chars = str->chars();
                len = static_cast<uint32_t>(str->length());
            }
            os.write(reinterpret_cast<const char*>(&len), sizeof(len));
            os.write(chars, len);
            break;
        }
        case Type::FUNCTION: {
            FunctionObject* func = chunk->getFunction(asFunctionIndex());
            func->serialize(os, parentSource);
            break;
        }
        default:
            throw std::runtime_error("Cannot serialize dynamic type: " + typeToString());
    }
}

Value Value::deserialize(std::istream& is, Chunk* chunk, const std::string& parentSource) {
    uint16_t t = 0;
    if (!is.read(reinterpret_cast<char*>(&t), sizeof(t)) || is.gcount() < static_cast<std::streamsize>(sizeof(t))) {
        throw TruncatedError("bad binary format (truncated chunk)");
    }
    Type type = static_cast<Type>(t);
    
    switch (type) {
        case Type::NIL: return Value::nil();
        case Type::BOOL: {
            uint8_t b = 0;
            if (!is.read(reinterpret_cast<char*>(&b), sizeof(b)) || is.gcount() < 1) {
                throw TruncatedError("bad binary format (truncated chunk)");
            }
            return Value::boolean(b != 0);
        }
        case Type::NUMBER: {
            double n = 0;
            if (!is.read(reinterpret_cast<char*>(&n), sizeof(n)) || is.gcount() < static_cast<std::streamsize>(sizeof(n))) {
                throw TruncatedError("bad binary format (truncated chunk)");
            }
            return Value::number(n);
        }
        case Type::INTEGER:
        case Type::INT64: {
            int64_t n = 0;
            if (!is.read(reinterpret_cast<char*>(&n), sizeof(n)) || is.gcount() < static_cast<std::streamsize>(sizeof(n))) {
                throw TruncatedError("bad binary format (truncated chunk)");
            }
            if (n >= -(1LL << 47) && n < (1LL << 47)) {
                return Value::integer(n);
            }
            size_t idx = chunk->addInt64(n);
            return Value::compileTimeInt64(idx);
        }
        case Type::STRING: {
            uint32_t len = 0;
            if (!is.read(reinterpret_cast<char*>(&len), sizeof(len)) || is.gcount() < static_cast<std::streamsize>(sizeof(len))) {
                throw TruncatedError("bad binary format (truncated chunk)");
            }
            std::string s(len, '\0');
            if (len > 0 && (!is.read(&s[0], len) || is.gcount() < static_cast<std::streamsize>(len))) {
                throw TruncatedError("bad binary format (truncated chunk)");
            }
            size_t idx = chunk->addString(s);
            return Value::string(idx);
        }
        case Type::FUNCTION: {
            auto func = FunctionObject::deserialize(is, parentSource);
            size_t idx = chunk->addFunction(func.release());
            return Value::function(idx);
        }
        default:
            throw std::runtime_error("Cannot deserialize unknown value type: " + std::to_string(t));
    }
}