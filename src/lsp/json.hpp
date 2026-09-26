#ifndef LUA_LSP_JSON_HPP
#define LUA_LSP_JSON_HPP

#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <stdexcept>
#include <cstdint>
#include <cctype>
#include <iomanip>

namespace lsp {

enum class JsonType {
    Null,
    Boolean,
    Number,
    String,
    Array,
    Object
};

class JsonValue {
public:
    JsonType type = JsonType::Null;

    bool boolVal = false;
    double numVal = 0.0;
    int64_t intVal = 0;
    bool isInteger = false;
    std::string strVal;
    std::vector<JsonValue> arrVal;
    std::vector<std::pair<std::string, JsonValue>> objVal;

    JsonValue() : type(JsonType::Null) {}
    JsonValue(std::nullptr_t) : type(JsonType::Null) {}
    JsonValue(bool b) : type(JsonType::Boolean), boolVal(b) {}
    JsonValue(int i) : type(JsonType::Number), numVal(i), intVal(i), isInteger(true) {}
    JsonValue(int64_t i) : type(JsonType::Number), numVal(static_cast<double>(i)), intVal(i), isInteger(true) {}
    JsonValue(size_t s) : type(JsonType::Number), numVal(static_cast<double>(s)), intVal(static_cast<int64_t>(s)), isInteger(true) {}
    JsonValue(double d) : type(JsonType::Number), numVal(d), intVal(static_cast<int64_t>(d)), isInteger(false) {}
    JsonValue(const char* s) : type(JsonType::String), strVal(s ? s : "") {}
    JsonValue(const std::string& s) : type(JsonType::String), strVal(s) {}
    JsonValue(std::vector<JsonValue> a) : type(JsonType::Array), arrVal(std::move(a)) {}

    static JsonValue object() {
        JsonValue v;
        v.type = JsonType::Object;
        return v;
    }

    static JsonValue object(std::initializer_list<std::pair<std::string, JsonValue>> pairs) {
        JsonValue v;
        v.type = JsonType::Object;
        for (const auto& p : pairs) {
            v.objVal.push_back(p);
        }
        return v;
    }

    static JsonValue array() {
        JsonValue v;
        v.type = JsonType::Array;
        return v;
    }

    static JsonValue array(std::initializer_list<JsonValue> values) {
        JsonValue v;
        v.type = JsonType::Array;
        v.arrVal = values;
        return v;
    }

    bool isNull() const { return type == JsonType::Null; }
    bool isBool() const { return type == JsonType::Boolean; }
    bool isNumber() const { return type == JsonType::Number; }
    bool isString() const { return type == JsonType::String; }
    bool isArray() const { return type == JsonType::Array; }
    bool isObject() const { return type == JsonType::Object; }

    bool asBool() const { return boolVal; }
    int64_t asInt64() const { return intVal; }
    int asInt() const { return static_cast<int>(intVal); }
    double asDouble() const { return numVal; }
    const std::string& asString() const { return strVal; }
    const std::vector<JsonValue>& asArray() const { return arrVal; }
    std::vector<JsonValue>& asArray() { return arrVal; }
    const std::vector<std::pair<std::string, JsonValue>>& asObject() const { return objVal; }

    bool has(const std::string& key) const {
        if (!isObject()) return false;
        for (const auto& kv : objVal) {
            if (kv.first == key) return true;
        }
        return false;
    }

    const JsonValue& get(const std::string& key, const JsonValue& defaultVal = JsonValue()) const {
        if (!isObject()) return defaultVal;
        for (const auto& kv : objVal) {
            if (kv.first == key) return kv.second;
        }
        return defaultVal;
    }

    JsonValue& operator[](const std::string& key) {
        if (!isObject()) {
            type = JsonType::Object;
            objVal.clear();
        }
        for (auto& kv : objVal) {
            if (kv.first == key) return kv.second;
        }
        objVal.push_back({key, JsonValue()});
        return objVal.back().second;
    }

    const JsonValue& operator[](const std::string& key) const {
        return get(key);
    }

    JsonValue& operator[](size_t idx) {
        if (!isArray()) {
            type = JsonType::Array;
            arrVal.clear();
        }
        if (idx >= arrVal.size()) {
            arrVal.resize(idx + 1);
        }
        return arrVal[idx];
    }

    const JsonValue& operator[](size_t idx) const {
        static const JsonValue nullVal;
        if (!isArray() || idx >= arrVal.size()) return nullVal;
        return arrVal[idx];
    }

    void push_back(const JsonValue& v) {
        if (!isArray()) {
            type = JsonType::Array;
            arrVal.clear();
        }
        arrVal.push_back(v);
    }

    size_t size() const {
        if (isArray()) return arrVal.size();
        if (isObject()) return objVal.size();
        return 0;
    }

    std::string serialize() const {
        std::ostringstream ss;
        serializeTo(ss);
        return ss.str();
    }

    void serializeTo(std::ostream& os) const {
        switch (type) {
            case JsonType::Null:
                os << "null";
                break;
            case JsonType::Boolean:
                os << (boolVal ? "true" : "false");
                break;
            case JsonType::Number:
                if (isInteger) {
                    os << intVal;
                } else {
                    os << std::setprecision(16) << numVal;
                }
                break;
            case JsonType::String:
                escapeString(strVal, os);
                break;
            case JsonType::Array: {
                os << "[";
                for (size_t i = 0; i < arrVal.size(); ++i) {
                    if (i > 0) os << ",";
                    arrVal[i].serializeTo(os);
                }
                os << "]";
                break;
            }
            case JsonType::Object: {
                os << "{";
                for (size_t i = 0; i < objVal.size(); ++i) {
                    if (i > 0) os << ",";
                    escapeString(objVal[i].first, os);
                    os << ":";
                    objVal[i].second.serializeTo(os);
                }
                os << "}";
                break;
            }
        }
    }

    static JsonValue parse(const std::string& input, std::string* error = nullptr) {
        size_t idx = 0;
        skipWs(input, idx);
        try {
            JsonValue val = parseValue(input, idx);
            skipWs(input, idx);
            return val;
        } catch (const std::exception& e) {
            if (error) *error = e.what();
            return JsonValue();
        }
    }

private:
    static void escapeString(const std::string& s, std::ostream& os) {
        os << '"';
        for (char c : s) {
            switch (c) {
                case '"':  os << "\\\""; break;
                case '\\': os << "\\\\"; break;
                case '\b': os << "\\b"; break;
                case '\f': os << "\\f"; break;
                case '\n': os << "\\n"; break;
                case '\r': os << "\\r"; break;
                case '\t': os << "\\t"; break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) {
                        os << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                           << static_cast<int>(static_cast<unsigned char>(c)) << std::dec;
                    } else {
                        os << c;
                    }
                    break;
            }
        }
        os << '"';
    }

    static void skipWs(const std::string& s, size_t& idx) {
        while (idx < s.size() && (s[idx] == ' ' || s[idx] == '\t' || s[idx] == '\r' || s[idx] == '\n')) {
            idx++;
        }
    }

    static JsonValue parseValue(const std::string& s, size_t& idx) {
        skipWs(s, idx);
        if (idx >= s.size()) throw std::runtime_error("Unexpected end of JSON input");

        char c = s[idx];
        if (c == 'n') return parseNull(s, idx);
        if (c == 't' || c == 'f') return parseBool(s, idx);
        if (c == '"') return parseString(s, idx);
        if (c == '[') return parseArray(s, idx);
        if (c == '{') return parseObject(s, idx);
        if (c == '-' || (c >= '0' && c <= '9')) return parseNumber(s, idx);

        throw std::runtime_error(std::string("Unexpected character in JSON: ") + c);
    }

    static JsonValue parseNull(const std::string& s, size_t& idx) {
        if (s.compare(idx, 4, "null") == 0) {
            idx += 4;
            return JsonValue();
        }
        throw std::runtime_error("Expected 'null'");
    }

    static JsonValue parseBool(const std::string& s, size_t& idx) {
        if (s.compare(idx, 4, "true") == 0) {
            idx += 4;
            return JsonValue(true);
        }
        if (s.compare(idx, 5, "false") == 0) {
            idx += 5;
            return JsonValue(false);
        }
        throw std::runtime_error("Expected boolean");
    }

    static JsonValue parseString(const std::string& s, size_t& idx) {
        idx++; // skip '"'
        std::string result;
        while (idx < s.size()) {
            char c = s[idx++];
            if (c == '"') return JsonValue(result);
            if (c == '\\') {
                if (idx >= s.size()) throw std::runtime_error("Unfinished string escape");
                char esc = s[idx++];
                switch (esc) {
                    case '"':  result.push_back('"'); break;
                    case '\\': result.push_back('\\'); break;
                    case '/':  result.push_back('/'); break;
                    case 'b':  result.push_back('\b'); break;
                    case 'f':  result.push_back('\f'); break;
                    case 'n':  result.push_back('\n'); break;
                    case 'r':  result.push_back('\r'); break;
                    case 't':  result.push_back('\t'); break;
                    case 'u': {
                        if (idx + 4 > s.size()) throw std::runtime_error("Invalid unicode escape");
                        std::string hexStr = s.substr(idx, 4);
                        idx += 4;
                        uint32_t cp = static_cast<uint32_t>(std::stoul(hexStr, nullptr, 16));
                        // Encode UTF-8
                        if (cp <= 0x7F) {
                            result.push_back(static_cast<char>(cp));
                        } else if (cp <= 0x7FF) {
                            result.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
                            result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                        } else {
                            result.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
                            result.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                            result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                        }
                        break;
                    }
                    default:
                        result.push_back(esc);
                        break;
                }
            } else {
                result.push_back(c);
            }
        }
        throw std::runtime_error("Unterminated string");
    }

    static JsonValue parseNumber(const std::string& s, size_t& idx) {
        size_t start = idx;
        if (s[idx] == '-') idx++;
        while (idx < s.size() && (s[idx] >= '0' && s[idx] <= '9')) idx++;

        bool hasFracOrExp = false;
        if (idx < s.size() && s[idx] == '.') {
            hasFracOrExp = true;
            idx++;
            while (idx < s.size() && (s[idx] >= '0' && s[idx] <= '9')) idx++;
        }
        if (idx < s.size() && (s[idx] == 'e' || s[idx] == 'E')) {
            hasFracOrExp = true;
            idx++;
            if (idx < s.size() && (s[idx] == '+' || s[idx] == '-')) idx++;
            while (idx < s.size() && (s[idx] >= '0' && s[idx] <= '9')) idx++;
        }

        std::string numStr = s.substr(start, idx - start);
        if (hasFracOrExp) {
            double d = std::stod(numStr);
            return JsonValue(d);
        } else {
            int64_t i = std::stoll(numStr);
            return JsonValue(i);
        }
    }

    static JsonValue parseArray(const std::string& s, size_t& idx) {
        idx++; // skip '['
        JsonValue arr = JsonValue::array();
        skipWs(s, idx);
        if (idx < s.size() && s[idx] == ']') {
            idx++;
            return arr;
        }

        while (idx < s.size()) {
            arr.push_back(parseValue(s, idx));
            skipWs(s, idx);
            if (idx >= s.size()) break;
            if (s[idx] == ']') {
                idx++;
                return arr;
            }
            if (s[idx] == ',') {
                idx++;
                skipWs(s, idx);
            } else {
                throw std::runtime_error("Expected ',' or ']' in array");
            }
        }
        throw std::runtime_error("Unterminated array");
    }

    static JsonValue parseObject(const std::string& s, size_t& idx) {
        idx++; // skip '{'
        JsonValue obj = JsonValue::object();
        skipWs(s, idx);
        if (idx < s.size() && s[idx] == '}') {
            idx++;
            return obj;
        }

        while (idx < s.size()) {
            skipWs(s, idx);
            if (idx >= s.size() || s[idx] != '"') {
                throw std::runtime_error("Expected string key in object");
            }
            JsonValue keyVal = parseString(s, idx);
            skipWs(s, idx);
            if (idx >= s.size() || s[idx] != ':') {
                throw std::runtime_error("Expected ':' after object key");
            }
            idx++; // skip ':'
            JsonValue val = parseValue(s, idx);
            obj.objVal.push_back({keyVal.asString(), std::move(val)});

            skipWs(s, idx);
            if (idx >= s.size()) break;
            if (s[idx] == '}') {
                idx++;
                return obj;
            }
            if (s[idx] == ',') {
                idx++;
                skipWs(s, idx);
            } else {
                throw std::runtime_error("Expected ',' or '}' in object");
            }
        }
        throw std::runtime_error("Unterminated object");
    }
};

} // namespace lsp

#endif // LUA_LSP_JSON_HPP
