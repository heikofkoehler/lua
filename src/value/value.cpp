#include "value/value.hpp"
#include "value/function.hpp"
#include "value/string.hpp"
#include "compiler/chunk.hpp"
#include <sstream>
#include <iomanip>
#include <iostream>

std::string Value::toString() const {
    std::ostringstream oss;
    print(oss);
    return oss.str();
}

void Value::print(std::ostream& os) const {
    switch (type()) {
        case Type::NIL:
            os << "nil";
            break;

        case Type::BOOL:
            os << (asBool() ? "true" : "false");
            break;

        case Type::NUMBER: {
            double num = asNumber();
            // Check if it's an integer value
            if (std::floor(num) == num && !std::isinf(num) && !std::isnan(num)) {
                os << static_cast<int64_t>(num);
            } else {
                os << std::setprecision(14) << num;
            }
            break;
        }

        case Type::FUNCTION: {
            size_t funcIndex = asFunctionIndex();
            os << "<function:" << funcIndex << ">";
            break;
        }

        case Type::STRING: {
            size_t stringIndex = asStringIndex();
            os << "<string:" << stringIndex << ">";
            break;
        }

        case Type::TABLE: {
            size_t tableIndex = asTableIndex();
            os << "<table:" << tableIndex << ">";
            break;
        }

        case Type::CLOSURE: {
            size_t closureIndex = asClosureIndex();
            os << "<closure:" << closureIndex << ">";
            break;
        }

        case Type::FILE: {
            size_t fileIndex = asFileIndex();
            os << "<file:" << fileIndex << ">";
            break;
        }

        case Type::SOCKET: {
            size_t socketIndex = asSocketIndex();
            os << "<socket:" << socketIndex << ">";
            break;
        }

        case Type::NATIVE_FUNCTION: {
            os << "<native function>";
            break;
        }

        case Type::THREAD: {
            size_t threadIndex = asThreadIndex();
            os << "<thread:" << threadIndex << ">";
            break;
        }
    }
}

void Value::serialize(std::ostream& os, const Chunk* chunk) const {
    uint8_t t = static_cast<uint8_t>(type());
    os.write(reinterpret_cast<const char*>(&t), sizeof(t));
    
    switch (type()) {
        case Type::NIL:
            break;
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
            throw std::runtime_error("Cannot serialize value type: " + typeToString());
    }
}

Value Value::deserialize(std::istream& is, Chunk* chunk) {
    uint8_t t;
    is.read(reinterpret_cast<char*>(&t), sizeof(t));
    Type type = static_cast<Type>(t);
    
    switch (type) {
        case Type::NIL:
            return Value::nil();
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
