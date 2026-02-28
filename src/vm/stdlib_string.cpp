#include "vm/vm.hpp"
#include "value/string.hpp"
#include "value/table.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <vector>

namespace {

bool native_string_format(VM* vm, int argCount) {
    if (argCount < 1) {
        vm->runtimeError("string.format expects at least 1 argument");
        return false;
    }

    Value fmtVal = vm->peek(argCount - 1);
    if (!fmtVal.isString()) {
        vm->runtimeError("string.format expects string as first argument");
        return false;
    }

    std::string fmt = vm->getStringValue(fmtVal);
    std::string result;
    int argIndex = 0; // index of the argument to use (0 = first arg AFTER format)

    for (size_t i = 0; i < fmt.length(); i++) {
        if (fmt[i] == '%' && i + 1 < fmt.length() && fmt[i+1] != '%') {
            i++;
            char spec = fmt[i];
            
            if (argIndex >= argCount - 1) {
                vm->runtimeError("bad argument to 'format' (no value)");
                return false;
            }
            
            Value val = vm->peek(argCount - 2 - argIndex);
            argIndex++;
            char buffer[1024];

            switch (spec) {
                case 's':
                    snprintf(buffer, sizeof(buffer), "%s", vm->getStringValue(val).c_str());
                    break;
                case 'd':
                    snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(val.asNumber()));
                    break;
                case 'f':
                    snprintf(buffer, sizeof(buffer), "%f", val.asNumber());
                    break;
                case 'x':
                    snprintf(buffer, sizeof(buffer), "%x", static_cast<int>(val.asNumber()));
                    break;
                default:
                    snprintf(buffer, sizeof(buffer), "%%%c", spec);
                    break;
            }
            result += buffer;
        } else if (fmt[i] == '%' && i + 1 < fmt.length() && fmt[i+1] == '%') {
            result += '%';
            i++;
        } else {
            result += fmt[i];
        }
    }

    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(Value::runtimeString(vm->internString(result)));
    return true;
}

bool native_string_len(VM* vm, int argCount) {
    if (argCount != 1) {
        vm->runtimeError("string.len expects 1 argument");
        return false;
    }
    Value strVal = vm->peek(0);
    if (!strVal.isString()) {
        vm->runtimeError("string.len expects string argument");
        return false;
    }
    
    size_t len = vm->getStringValue(strVal).length();
    vm->pop();
    vm->push(Value::number(static_cast<double>(len)));
    return true;
}

bool native_string_sub(VM* vm, int argCount) {
    if (argCount < 2 || argCount > 3) {
        vm->runtimeError("string.sub expects 2 or 3 arguments");
        return false;
    }

    Value endVal = (argCount == 3) ? vm->peek(0) : Value::nil();
    Value startVal = (argCount >= 2) ? vm->peek(argCount - 2) : Value::nil();
    Value strVal = vm->peek(argCount - 1);

    if (!strVal.isString()) {
        vm->runtimeError("string.sub expects string as first argument");
        return false;
    }
    if (!startVal.isNumber()) {
        vm->runtimeError("string.sub expects number as second argument");
        return false;
    }

    std::string s = vm->getStringValue(strVal);
    int start = static_cast<int>(startVal.asNumber());
    int end = endVal.isNil() ? s.length() : static_cast<int>(endVal.asNumber());

    if (start < 0) start = s.length() + start + 1;
    if (end < 0) end = s.length() + end + 1;

    start = std::max(1, std::min(start, static_cast<int>(s.length()) + 1));
    end = std::max(0, std::min(end, static_cast<int>(s.length())));

    std::string res;
    if (start <= end) {
        res = s.substr(start - 1, end - start + 1);
    }

    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(Value::runtimeString(vm->internString(res)));
    return true;
}

bool native_string_upper(VM* vm, int argCount) {
    if (argCount != 1) {
        vm->runtimeError("string.upper expects 1 argument");
        return false;
    }
    Value strVal = vm->peek(0);
    if (!strVal.isString()) {
        vm->runtimeError("string.upper expects string argument");
        return false;
    }
    
    std::string s = vm->getStringValue(strVal);
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    
    vm->pop();
    vm->push(Value::runtimeString(vm->internString(s)));
    return true;
}

bool native_string_lower(VM* vm, int argCount) {
    if (argCount != 1) {
        vm->runtimeError("string.lower expects 1 argument");
        return false;
    }
    Value strVal = vm->peek(0);
    if (!strVal.isString()) {
        vm->runtimeError("string.lower expects string argument");
        return false;
    }
    
    std::string s = vm->getStringValue(strVal);
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    
    vm->pop();
    vm->push(Value::runtimeString(vm->internString(s)));
    return true;
}

bool native_string_reverse(VM* vm, int argCount) {
    if (argCount != 1) {
        vm->runtimeError("string.reverse expects 1 argument");
        return false;
    }
    Value strVal = vm->peek(0);
    if (!strVal.isString()) {
        vm->runtimeError("string.reverse expects string argument");
        return false;
    }
    
    std::string s = vm->getStringValue(strVal);
    std::reverse(s.begin(), s.end());
    
    vm->pop();
    vm->push(Value::runtimeString(vm->internString(s)));
    return true;
}

bool native_string_byte(VM* vm, int argCount) {
    if (argCount < 1 || argCount > 2) {
        vm->runtimeError("string.byte expects 1 or 2 arguments");
        return false;
    }

    Value posVal = (argCount == 2) ? vm->peek(0) : Value::number(1);
    Value strVal = vm->peek(argCount - 1);

    if (!strVal.isString()) {
        vm->runtimeError("string.byte expects string argument");
        return false;
    }

    std::string s = vm->getStringValue(strVal);
    int pos = static_cast<int>(posVal.asNumber());
    
    Value res = Value::nil();
    if (pos >= 1 && pos <= static_cast<int>(s.length())) {
        res = Value::number(static_cast<unsigned char>(s[pos - 1]));
    }
    
    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(res);
    return true;
}

bool native_string_char(VM* vm, int argCount) {
    std::string result;
    result.reserve(argCount);

    for (int i = 0; i < argCount; i++) {
        Value val = vm->peek(argCount - 1 - i);
        if (!val.isNumber()) {
            vm->runtimeError("string.char expects number arguments");
            return false;
        }
        result.push_back(static_cast<char>(val.asNumber()));
    }

    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(Value::runtimeString(vm->internString(result)));
    return true;
}

bool native_string_find(VM* vm, int argCount) {
    if (argCount < 2 || argCount > 3) {
        vm->runtimeError("string.find expects 2 or 3 arguments");
        return false;
    }
    Value startVal = (argCount == 3) ? vm->peek(0) : Value::number(1);
    Value patternVal = vm->peek(argCount - 2);
    Value strVal = vm->peek(argCount - 1);

    if (!strVal.isString() || !patternVal.isString()) {
        vm->runtimeError("string.find expects string arguments");
        return false;
    }
    
    std::string s = vm->getStringValue(strVal);
    std::string p = vm->getStringValue(patternVal);
    int start = static_cast<int>(startVal.asNumber());

    if (start < 1) start = 1;
    
    Value res1 = Value::nil();
    Value res2 = Value::nil();
    bool found = false;

    if (start <= static_cast<int>(s.length()) + 1) {
        size_t pos = s.find(p, start - 1);
        if (pos != std::string::npos) {
            res1 = Value::number(pos + 1);
            res2 = Value::number(pos + p.length());
            found = true;
        }
    }

    for (int i = 0; i < argCount; i++) vm->pop();
    if (found) {
        vm->push(res1);
        vm->push(res2);
    } else {
        vm->push(Value::nil());
    }
    return true;
}

bool native_string_gsub(VM* vm, int argCount) {
    if (argCount < 3) {
        vm->runtimeError("string.gsub expects at least 3 arguments");
        return false;
    }
    Value replVal = vm->peek(0);
    Value patternVal = vm->peek(1);
    Value strVal = vm->peek(2);

    if (!strVal.isString() || !patternVal.isString() || !replVal.isString()) {
        vm->runtimeError("string.gsub expects string arguments");
        return false;
    }

    std::string s = vm->getStringValue(strVal);
    std::string p = vm->getStringValue(patternVal);
    std::string r = vm->getStringValue(replVal);

    std::string result = s;
    size_t pos = 0;
    int count = 0;
    while ((pos = result.find(p, pos)) != std::string::npos) {
        result.replace(pos, p.length(), r);
        pos += r.length();
        count++;
    }

    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(Value::runtimeString(vm->internString(result)));
    vm->push(Value::number(count));
    return true;
}

bool native_string_packsize(VM* vm, int argCount) {
    if (argCount < 1) {
        vm->runtimeError("string.packsize expects at least 1 argument");
        return false;
    }
    Value fmtVal = vm->peek(0);
    if (!fmtVal.isString()) {
        vm->runtimeError("string.packsize expects string format");
        return false;
    }
    
    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(Value::number(8));
    return true;
}

bool native_string_pack(VM* vm, int argCount) {
    for(int i=0; i<argCount; i++) vm->pop();
    vm->push(Value::runtimeString(vm->internString("packed_data_stub")));
    return true;
}

bool native_string_dump(VM* vm, int argCount) {
    for(int i=0; i<argCount; i++) vm->pop();
    vm->push(Value::runtimeString(vm->internString("function_bytecode_stub")));
    return true;
}

} // anonymous namespace

void registerStringLibrary(VM* vm, TableObject* stringTable) {
    // String library metatable for strings
    TableObject* stringMt = vm->createTable();
    
    // We set __index to the string table itself
    Value stringTableVal = Value::table(stringTable);
    stringMt->set("__index", stringTableVal);
    
    // Store in registry for getMetamethod to find it safely
    vm->setRegistry("string_table", stringTableVal);
    
    vm->setTypeMetatable(Value::Type::STRING, Value::table(stringMt));
    vm->setTypeMetatable(Value::Type::RUNTIME_STRING, Value::table(stringMt));

    vm->addNativeToTable(stringTable, "len", native_string_len);
    vm->addNativeToTable(stringTable, "sub", native_string_sub);
    vm->addNativeToTable(stringTable, "upper", native_string_upper);
    vm->addNativeToTable(stringTable, "lower", native_string_lower);
    vm->addNativeToTable(stringTable, "reverse", native_string_reverse);
    vm->addNativeToTable(stringTable, "byte", native_string_byte);
    vm->addNativeToTable(stringTable, "char", native_string_char);
    vm->addNativeToTable(stringTable, "find", native_string_find);
    vm->addNativeToTable(stringTable, "gsub", native_string_gsub);
    vm->addNativeToTable(stringTable, "format", native_string_format);
    vm->addNativeToTable(stringTable, "packsize", native_string_packsize);
    vm->addNativeToTable(stringTable, "pack", native_string_pack);
    vm->addNativeToTable(stringTable, "dump", native_string_dump);
}
