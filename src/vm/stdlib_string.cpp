#include "vm/vm.hpp"
#include "value/string.hpp"
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

    // Arguments are on stack: [..., format, arg1, arg2, ...]
    // peek(argCount - 1) is format string
    Value fmtVal = vm->peek(argCount - 1);
    if (!fmtVal.isString()) {
        vm->runtimeError("string.format expects string as first argument");
        return false;
    }

    std::string fmt = vm->getStringValue(fmtVal);
    std::string result;
    int currentArg = argCount - 2; // Index into stack from top (0 is last arg)

    for (size_t i = 0; i < fmt.length(); i++) {
        if (fmt[i] == '%' && i + 1 < fmt.length() && fmt[i+1] != '%') {
            i++;
            char spec = fmt[i];
            
            if (currentArg < 0) {
                vm->runtimeError("bad argument to 'format' (no value)");
                return false;
            }
            
            Value val = vm->peek(currentArg--);
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

    // Pop arguments
    for (int i = 0; i < argCount; i++) vm->pop();

    vm->push(Value::runtimeString(vm->internString(result)));
    return true;
}

bool native_string_len(VM* vm, int argCount) {
    if (argCount != 1) {
        vm->runtimeError("string.len expects 1 argument");
        return false;
    }
    Value strVal = vm->pop();
    if (!strVal.isString() && !strVal.isRuntimeString()) {
        vm->runtimeError("string.len expects string argument");
        return false;
    }
    // Check runtime string first (isString() returns true for both types)
    StringObject* str = strVal.isRuntimeString()
        ? vm->getString(strVal.asStringIndex())
        : vm->rootChunk()->getString(strVal.asStringIndex());
    vm->push(Value::number(str->length()));
    return true;
}

bool native_string_sub(VM* vm, int argCount) {
    if (argCount < 2 || argCount > 3) {
        vm->runtimeError("string.sub expects 2 or 3 arguments");
        return false;
    }

    Value endVal = (argCount == 3) ? vm->pop() : Value::nil();
    Value startVal = vm->pop();
    Value strVal = vm->pop();

    if (!strVal.isString() && !strVal.isRuntimeString()) {
        vm->runtimeError("string.sub expects string as first argument");
        return false;
    }
    if (!startVal.isNumber()) {
        vm->runtimeError("string.sub expects number as second argument");
        return false;
    }

    // Check runtime string first (isString() returns true for both types)
    StringObject* str = strVal.isRuntimeString()
        ? vm->getString(strVal.asStringIndex())
        : vm->rootChunk()->getString(strVal.asStringIndex());

    int start = static_cast<int>(startVal.asNumber());
    int end = endVal.isNil() ? str->length() : static_cast<int>(endVal.asNumber());

    // Lua uses 1-based indexing
    // Negative indices count from end
    if (start < 0) start = str->length() + start + 1;
    if (end < 0) end = str->length() + end + 1;

    // Clamp to valid range
    start = std::max(1, std::min(start, static_cast<int>(str->length()) + 1));
    end = std::max(0, std::min(end, static_cast<int>(str->length())));

    if (start > end) {
        vm->push(Value::runtimeString(vm->internString("")));
    } else {
        std::string substr(str->chars() + start - 1, end - start + 1);
        vm->push(Value::runtimeString(vm->internString(substr)));
    }

    return true;
}

bool native_string_upper(VM* vm, int argCount) {
    if (argCount != 1) {
        vm->runtimeError("string.upper expects 1 argument");
        return false;
    }
    Value strVal = vm->pop();
    if (!strVal.isString() && !strVal.isRuntimeString()) {
        vm->runtimeError("string.upper expects string argument");
        return false;
    }
    // Check runtime string first (isString() returns true for both types)
    StringObject* str = strVal.isRuntimeString()
        ? vm->getString(strVal.asStringIndex())
        : vm->rootChunk()->getString(strVal.asStringIndex());

    std::string upper(str->chars(), str->length());
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    vm->push(Value::runtimeString(vm->internString(upper)));
    return true;
}

bool native_string_lower(VM* vm, int argCount) {
    if (argCount != 1) {
        vm->runtimeError("string.lower expects 1 argument");
        return false;
    }
    Value strVal = vm->pop();
    if (!strVal.isString() && !strVal.isRuntimeString()) {
        vm->runtimeError("string.lower expects string argument");
        return false;
    }
    // Check runtime string first (isString() returns true for both types)
    StringObject* str = strVal.isRuntimeString()
        ? vm->getString(strVal.asStringIndex())
        : vm->rootChunk()->getString(strVal.asStringIndex());

    std::string lower(str->chars(), str->length());
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    vm->push(Value::runtimeString(vm->internString(lower)));
    return true;
}

bool native_string_reverse(VM* vm, int argCount) {
    if (argCount != 1) {
        vm->runtimeError("string.reverse expects 1 argument");
        return false;
    }
    Value strVal = vm->pop();
    if (!strVal.isString() && !strVal.isRuntimeString()) {
        vm->runtimeError("string.reverse expects string argument");
        return false;
    }
    // Check runtime string first (isString() returns true for both types)
    StringObject* str = strVal.isRuntimeString()
        ? vm->getString(strVal.asStringIndex())
        : vm->rootChunk()->getString(strVal.asStringIndex());

    std::string reversed(str->chars(), str->length());
    std::reverse(reversed.begin(), reversed.end());
    vm->push(Value::runtimeString(vm->internString(reversed)));
    return true;
}

bool native_string_byte(VM* vm, int argCount) {
    if (argCount < 1 || argCount > 2) {
        vm->runtimeError("string.byte expects 1 or 2 arguments");
        return false;
    }

    Value posVal = (argCount == 2) ? vm->pop() : Value::number(1);
    Value strVal = vm->pop();

    if (!strVal.isString() && !strVal.isRuntimeString()) {
        vm->runtimeError("string.byte expects string argument");
        return false;
    }

    // Check runtime string first (isString() returns true for both types)
    StringObject* str = strVal.isRuntimeString()
        ? vm->getString(strVal.asStringIndex())
        : vm->rootChunk()->getString(strVal.asStringIndex());

    int pos = static_cast<int>(posVal.asNumber());
    if (pos < 1 || pos > static_cast<int>(str->length())) {
        vm->push(Value::nil());
    } else {
        vm->push(Value::number(static_cast<unsigned char>(str->chars()[pos - 1])));
    }
    return true;
}

bool native_string_char(VM* vm, int argCount) {
    std::string result;
    result.reserve(argCount);

    // Pop arguments in reverse order
    for (int i = 0; i < argCount; i++) {
        Value val = vm->pop();
        if (!val.isNumber()) {
            vm->runtimeError("string.char expects number arguments");
            return false;
        }
        result.push_back(static_cast<char>(val.asNumber()));
    }

    // Reverse since we popped in reverse order
    std::reverse(result.begin(), result.end());

    vm->push(Value::runtimeString(vm->internString(result)));
    return true;
}

bool native_string_find(VM* vm, int argCount) {
    if (argCount < 2 || argCount > 3) {
        vm->runtimeError("string.find expects 2 or 3 arguments");
        return false;
    }
    Value startVal = (argCount == 3) ? vm->pop() : Value::number(1);
    Value patternVal = vm->pop();
    Value strVal = vm->pop();

    if ((!strVal.isString() && !strVal.isRuntimeString()) || 
        (!patternVal.isString() && !patternVal.isRuntimeString())) {
        vm->runtimeError("string.find expects string arguments");
        return false;
    }
    if (!startVal.isNumber()) {
        vm->runtimeError("string.find expects number as third argument");
        return false;
    }

    std::string s = vm->getStringValue(strVal);
    std::string p = vm->getStringValue(patternVal);
    int start = static_cast<int>(startVal.asNumber());

    // Lua uses 1-based indexing
    if (start < 1) start = 1;
    if (start > static_cast<int>(s.length()) + 1) {
        vm->push(Value::nil());
        return true;
    }

    size_t pos = s.find(p, start - 1);
    if (pos == std::string::npos) {
        vm->push(Value::nil());
    } else {
        vm->push(Value::number(pos + 1));
        vm->push(Value::number(pos + p.length()));
    }
    return true;
}


bool native_string_gsub(VM* vm, int argCount) {
    if (argCount < 3) {
        vm->runtimeError("string.gsub expects at least 3 arguments");
        return false;
    }
    Value replVal = vm->pop();
    Value patternVal = vm->pop();
    Value strVal = vm->pop();

    if ((!strVal.isString() && !strVal.isRuntimeString()) || 
        (!patternVal.isString() && !patternVal.isRuntimeString()) ||
        (!replVal.isString() && !replVal.isRuntimeString())) {
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

    vm->push(Value::runtimeString(vm->internString(result)));
    vm->push(Value::number(count));
    return true;
}

} // anonymous namespace

void registerStringLibrary(VM* vm, TableObject* stringTable) {
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
}

