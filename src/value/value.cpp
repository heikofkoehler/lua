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

std::string Value::toString() const {
    std::ostringstream oss;
    print(oss);
    return oss.str();
}

std::string Value::typeToString() const {
    switch (type()) {
        case Type::NUMBER: 
        case Type::INTEGER: return "number";
        case Type::BOOL: return "boolean";
        case Type::NIL: return "nil";
        case Type::STRING:
        case Type::RUNTIME_STRING: return "string";
        case Type::TABLE: return "table";
        case Type::FUNCTION:
        case Type::CLOSURE:
        case Type::NATIVE_FUNCTION: return "function";
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
        case Type::INTEGER: os << asInteger(); break;
        case Type::NUMBER: {
            double num = asNumber();
            if (std::floor(num) == num && !std::isinf(num) && !std::isnan(num)) {
                os << static_cast<int64_t>(num);
            } else {
                os << std::setprecision(14) << num;
            }
            break;
        }
        case Type::FUNCTION: os << "<function:" << asFunctionIndex() << ">"; break;
        case Type::STRING: os << "<string:" << asStringIndex() << ">"; break;
        case Type::RUNTIME_STRING: os << asStringObj()->chars(); break;
        case Type::TABLE: os << "table: " << asTableObj(); break;
        case Type::CLOSURE: os << "function: " << asClosureObj(); break;
        case Type::FILE: os << "file: " << asFileObj(); break;
        case Type::SOCKET: os << "socket: " << asSocketObj(); break;
        case Type::NATIVE_FUNCTION: os << "<native function>"; break;
        case Type::THREAD: os << "thread: " << asThreadObj(); break;
        case Type::USERDATA: os << "userdata: " << asUserdataObj(); break;
    }
}

bool Value::operator==(const Value& other) const {
    if (isNumber() && other.isNumber()) {
        if (isInteger() && other.isInteger()) {
            return asInteger() == other.asInteger();
        }
        double a = asNumber();
        double b = other.asNumber();
        if (std::isnan(a) && std::isnan(b)) return false;
        return a == b;
    }
    
    if (type() != other.type()) {
        return false;
    }

    if (isRuntimeString()) {
        return asStringObj()->equals(other.asStringObj());
    }

    return bits_ == other.bits_;
}

bool Value::isStringEqual(const std::string& str) const {
    if (isRuntimeString()) {
        return std::string(asStringObj()->chars()) == str;
    }
    return false; // Compile-time strings must be interned first
}

size_t Value::hash() const {
    if (isInteger()) return std::hash<int64_t>()(asInteger());
    if (isNumber()) return std::hash<double>()(asNumber());
    if (isRuntimeString()) return std::hash<std::string>()(asStringObj()->chars());
    return std::hash<uint64_t>()(bits_);
}

void Value::serialize(std::ostream& os, const Chunk* chunk) const {
    uint8_t t = static_cast<uint8_t>(type());
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
        case Type::INTEGER: {
            int64_t n = asInteger();
            os.write(reinterpret_cast<const char*>(&n), sizeof(n));
            break;
        }
        case Type::STRING: {
            StringObject* str = chunk->getString(asStringIndex());
            uint32_t len = static_cast<uint32_t>(str->length());
            os.write(reinterpret_cast<const char*>(&len), sizeof(len));
            os.write(str->chars(), len);
            break;
        }
        case Type::FUNCTION: {
            FunctionObject* func = chunk->getFunction(asFunctionIndex());
            func->serialize(os);
            break;
        }
        default:
            throw std::runtime_error("Cannot serialize dynamic type: " + typeToString());
    }
}

Value Value::deserialize(std::istream& is, Chunk* chunk) {
    uint8_t t;
    is.read(reinterpret_cast<char*>(&t), sizeof(t));
    Type type = static_cast<Type>(t);
    
    switch (type) {
        case Type::NIL: return Value::nil();
        case Type::BOOL: {
            uint8_t b;
            is.read(reinterpret_cast<char*>(&b), sizeof(b));
            return Value::boolean(b != 0);
        }
        case Type::NUMBER: {
            double n;
            is.read(reinterpret_cast<char*>(&n), sizeof(n));
            return Value::number(n);
        }
        case Type::INTEGER: {
            int64_t n;
            is.read(reinterpret_cast<char*>(&n), sizeof(n));
            return Value::integer(n);
        }
        case Type::STRING: {
            uint32_t len;
            is.read(reinterpret_cast<char*>(&len), sizeof(len));
            std::string s(len, '\0');
            is.read(&s[0], len);
            size_t idx = chunk->addString(s);
            return Value::string(idx);
        }
        case Type::FUNCTION: {
            auto func = FunctionObject::deserialize(is);
            size_t idx = chunk->addFunction(func.release());
            return Value::function(idx);
        }
        default:
            throw std::runtime_error("Cannot deserialize unknown value type");
    }
}
