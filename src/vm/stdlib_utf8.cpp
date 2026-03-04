#include "vm/vm.hpp"
#include "value/table.hpp"
#include "value/string.hpp"
#include <string>
#include <vector>

namespace {

// Helper to decode UTF-8
static const char* utf8_decode(const char* s, const char* e, uint32_t* cp) {
    unsigned char c = (unsigned char)*s;
    if (c < 0x80) {
        *cp = c;
        return s + 1;
    }
    if (c < 0xC0) return nullptr; // Invalid leading byte
    
    int len;
    uint32_t res;
    if (c < 0xE0) { len = 2; res = c & 0x1F; }
    else if (c < 0xF0) { len = 3; res = c & 0x0F; }
    else if (c < 0xF8) { len = 4; res = c & 0x07; }
    else return nullptr;

    if (s + len > e) return nullptr;

    for (int i = 1; i < len; i++) {
        c = (unsigned char)s[i];
        if ((c & 0xC0) != 0x80) return nullptr;
        res = (res << 6) | (c & 0x3F);
    }
    
    *cp = res;
    return s + len;
}

// Helper to encode UTF-8
static std::string utf8_encode(uint32_t cp) {
    std::string res;
    if (cp < 0x80) {
        res.push_back((char)cp);
    } else if (cp < 0x800) {
        res.push_back((char)(0xC0 | (cp >> 6)));
        res.push_back((char)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        res.push_back((char)(0xE0 | (cp >> 12)));
        res.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        res.push_back((char)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x110000) {
        res.push_back((char)(0xF0 | (cp >> 18)));
        res.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
        res.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        res.push_back((char)(0x80 | (cp & 0x3F)));
    }
    return res;
}

bool native_utf8_char(VM* vm, int argCount) {
    std::string result;
    for (int i = 0; i < argCount; i++) {
        Value v = vm->peek(argCount - 1 - i);
        if (!v.isNumber()) {
            vm->runtimeError("utf8.char expects number arguments");
            return false;
        }
        result += utf8_encode(static_cast<uint32_t>(v.asNumber()));
    }
    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(Value::runtimeString(vm->internString(result)));
    return true;
}

bool native_utf8_len(VM* vm, int argCount) {
    if (argCount < 1) { vm->runtimeError("utf8.len expects at least 1 argument"); return false; }
    std::string s_str = vm->getStringValue(vm->peek(argCount - 1));
    int i = (argCount >= 2) ? static_cast<int>(vm->peek(argCount - 2).asNumber()) : 1;
    int j = (argCount >= 3) ? static_cast<int>(vm->peek(argCount - 3).asNumber()) : -1;
    
    size_t len = s_str.length();
    if (i < 0) i = (int)len + i + 1;
    if (j < 0) j = (int)len + j + 1;
    if (i < 1) i = 1;
    if (j > (int)len) j = (int)len;

    int count = 0;
    const char* s = s_str.c_str() + i - 1;
    const char* e = s_str.c_str() + j;
    
    while (s < e) {
        uint32_t cp;
        const char* next = utf8_decode(s, e, &cp);
        if (!next) {
            for(int k=0; k<argCount; k++) vm->pop();
            vm->push(Value::nil());
            vm->push(Value::number(s - s_str.c_str() + 1));
            vm->currentCoroutine()->lastResultCount = 2;
            return true;
        }
        s = next;
        count++;
    }

    for (int k = 0; k < argCount; k++) vm->pop();
    vm->push(Value::number(count));
    return true;
}

bool native_utf8_codepoint(VM* vm, int argCount) {
    if (argCount < 1) { vm->runtimeError("utf8.codepoint expects at least 1 argument"); return false; }
    std::string s_str = vm->getStringValue(vm->peek(argCount - 1));
    int i = (argCount >= 2) ? static_cast<int>(vm->peek(argCount - 2).asNumber()) : 1;
    int j = (argCount >= 3) ? static_cast<int>(vm->peek(argCount - 3).asNumber()) : i;
    
    size_t len = s_str.length();
    if (i < 0) i = (int)len + i + 1;
    if (j < 0) j = (int)len + j + 1;
    if (i < 1) i = 1;
    
    if (i > j) {
        for(int k=0; k<argCount; k++) vm->pop();
        vm->currentCoroutine()->lastResultCount = 0;
        return true;
    }

    const char* s_start = s_str.c_str();
    const char* s_limit = s_start + len;
    const char* p = s_start + i - 1;
    const char* p_end = s_start + j - 1;

    if (p < s_start || p >= s_limit) {
        for(int k=0; k<argCount; k++) vm->pop();
        vm->currentCoroutine()->lastResultCount = 0;
        return true;
    }

    std::vector<uint32_t> cps;
    while (p <= p_end && p < s_limit) {
        uint32_t cp;
        const char* next = utf8_decode(p, s_limit, &cp);
        if (!next) {
            vm->runtimeError("invalid UTF-8 code");
            return false;
        }
        cps.push_back(cp);
        p = next;
    }

    for (int k = 0; k < argCount; k++) vm->pop();
    for (uint32_t cp : cps) {
        vm->push(Value::number(cp));
    }
    vm->currentCoroutine()->lastResultCount = cps.size();
    return true;
}

bool native_utf8_offset(VM* vm, int argCount) {
    if (argCount < 2) { vm->runtimeError("utf8.offset expects at least 2 arguments"); return false; }
    std::string s_str = vm->getStringValue(vm->peek(argCount - 1));
    int n = static_cast<int>(vm->peek(argCount - 2).asNumber());
    int i = (argCount >= 3) ? static_cast<int>(vm->peek(argCount - 3).asNumber()) : (n >= 0 ? 1 : (int)s_str.length() + 1);
    
    size_t len = s_str.length();
    if (i < 0) i = (int)len + i + 1;
    if (i < 1 || i > (int)len + 1) {
        vm->runtimeError("initial position out of bounds");
        return false;
    }

    const char* s_start = s_str.c_str();
    const char* s_end = s_start + len;
    const char* p = s_start + i - 1;

    if (n == 0) {
        // Back to start of current character
        while (p > s_start && (*(unsigned char*)p & 0xC0) == 0x80) p--;
        for(int k=0; k<argCount; k++) vm->pop();
        vm->push(Value::number(p - s_start + 1));
        return true;
    }

    if (n > 0) {
        n--; // Current position counts as 1 if at start of char
        if (p < s_end && (*(unsigned char*)p & 0xC0) == 0x80) {
             // If in middle of char, standard Lua behavior is slightly complex.
             // We'll just move to next start.
        }
        while (n > 0 && p < s_end) {
            p++;
            while (p < s_end && (*(unsigned char*)p & 0xC0) == 0x80) p++;
            n--;
        }
    } else {
        while (n < 0 && p > s_start) {
            p--;
            while (p > s_start && (*(unsigned char*)p & 0xC0) == 0x80) p--;
            n++;
        }
    }

    for (int k = 0; k < argCount; k++) vm->pop();
    if (n == 0) {
        vm->push(Value::number(p - s_start + 1));
    } else {
        vm->push(Value::nil());
    }
    return true;
}

bool native_utf8_codes(VM* vm, int argCount) {
    if (argCount < 1) { vm->runtimeError("utf8.codes expects 1 argument"); return false; }
    Value sVal = vm->peek(argCount - 1);
    for(int i=0; i<argCount; i++) vm->pop();

    const char* script = 
        "local s = ...\n"
        "return function(s, i)\n"
        "   i = i or 0\n"
        "   local next_i = i + 1\n"
        "   while next_i <= #s and (string.byte(s, next_i) & 0xC0) == 0x80 do\n"
        "       next_i = next_i + 1\n"
        "   end\n"
        "   if next_i > #s then return nil end\n"
        "   local cp = utf8.codepoint(s, next_i, next_i)\n"
        "   return next_i, cp\n"
        "end, s, 0\n";

    FunctionObject* func = vm->compileSource(script, "utf8.codes");
    if (!func) return false;
    ClosureObject* closure = vm->createClosure(func);
    vm->setupRootUpvalues(closure);
    
    vm->push(Value::closure(closure));
    vm->push(sVal);
    
    size_t baseFrames = vm->currentCoroutine()->frames.size();
    if (vm->callValue(1, 4)) { // Expect 3 results (iter, s, i)
        if (vm->currentCoroutine()->frames.size() > baseFrames) {
            vm->run(baseFrames);
        }
        // results are already on stack, lastResultCount should be 3
        return true;
    }
    return false;
}

} // anonymous namespace

void registerUTF8Library(VM* vm, TableObject* utf8Table) {
    vm->addNativeToTable(utf8Table, "char", native_utf8_char);
    vm->addNativeToTable(utf8Table, "len", native_utf8_len);
    vm->addNativeToTable(utf8Table, "codepoint", native_utf8_codepoint);
    vm->addNativeToTable(utf8Table, "offset", native_utf8_offset);
    vm->addNativeToTable(utf8Table, "codes", native_utf8_codes);
    
    // utf8.charpattern = "[\0-\x7F\xC2-\xF4][\x80-\xBF]*"
    const char pattern[] = "[\0-\x7F\xC2-\xF4][\x80-\xBF]*";
    utf8Table->set("charpattern", Value::runtimeString(vm->internString(pattern, sizeof(pattern) - 1)));
}
