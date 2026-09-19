#include "vm/vm.hpp"
#include "value/table.hpp"
#include "value/string.hpp"
#include <string>
#include <vector>
#include <limits>
#include <cmath>

namespace {

// Helper to decode UTF-8 matching Lua 5.5
// In strict mode (!lax):
// - max codepoint is 0x10FFFF
// - surrogates (0xD800..0xDFFF) are rejected
// - overlong sequences are rejected
// In lax mode:
// - supports up to 6 bytes (codepoint <= 0x7FFFFFFF)
// - allows surrogates and overlong sequences
static const char* utf8_decode(const char* s, const char* e, uint32_t* cp, bool lax) {
    if (s >= e) return nullptr;
    unsigned char c = static_cast<unsigned char>(*s);
    if (c < 0x80) {
        *cp = c;
        return s + 1;
    }

    if (c < 0xC0) return nullptr; // Invalid leading byte (continuation byte)

    int count;
    uint32_t res;
    uint32_t min_cp;

    if (c < 0xE0) {
        count = 1;
        res = c & 0x1F;
        min_cp = 0x80;
    } else if (c < 0xF0) {
        count = 2;
        res = c & 0x0F;
        min_cp = 0x800;
    } else if (c < 0xF8) {
        count = 3;
        res = c & 0x07;
        min_cp = 0x10000;
    } else if (lax && c < 0xFC) {
        count = 4;
        res = c & 0x03;
        min_cp = 0x200000;
    } else if (lax && c < 0xFE) {
        count = 5;
        res = c & 0x01;
        min_cp = 0x4000000;
    } else {
        return nullptr;
    }

    if (s + 1 + count > e) return nullptr;

    for (int i = 1; i <= count; i++) {
        unsigned char cc = static_cast<unsigned char>(s[i]);
        if ((cc & 0xC0) != 0x80) return nullptr;
        res = (res << 6) | (cc & 0x3F);
    }

    if (!lax) {
        if (res < min_cp) return nullptr;
        if (res >= 0xD800 && res <= 0xDFFF) return nullptr;
        if (res > 0x10FFFF) return nullptr;
    } else {
        if (res > 0x7FFFFFFF) return nullptr;
    }

    *cp = res;
    return s + 1 + count;
}

// Helper to encode UTF-8 up to 0x7FFFFFFF (up to 6 bytes)
static std::string utf8_encode(uint32_t cp) {
    std::string res;
    if (cp < 0x80) {
        res.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        res.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        res.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        res.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        res.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        res.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x200000) {
        res.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        res.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        res.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        res.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x4000000) {
        res.push_back(static_cast<char>(0xF8 | (cp >> 24)));
        res.push_back(static_cast<char>(0x80 | ((cp >> 18) & 0x3F)));
        res.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        res.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        res.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        res.push_back(static_cast<char>(0xFC | (cp >> 30)));
        res.push_back(static_cast<char>(0x80 | ((cp >> 24) & 0x3F)));
        res.push_back(static_cast<char>(0x80 | ((cp >> 18) & 0x3F)));
        res.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        res.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        res.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    return res;
}

bool native_utf8_char(VM* vm, int argCount) {
    std::string result;
    for (int i = 0; i < argCount; i++) {
        Value v = vm->peek(argCount - 1 - i);
        if (!v.isInteger() && !v.isNumber()) {
            vm->runtimeError("bad argument to 'utf8.char' (number expected)");
            return false;
        }
        int64_t cp = v.isInteger() ? v.asInteger() : static_cast<int64_t>(v.asNumber());
        if (cp < 0 || cp > 0x7FFFFFFF) {
            vm->runtimeError("value out of range");
            return false;
        }
        result += utf8_encode(static_cast<uint32_t>(cp));
    }
    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(Value::runtimeString(vm->internString(result)));
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_utf8_len(VM* vm, int argCount) {
    if (argCount < 1) {
        vm->runtimeError("bad argument #1 to 'utf8.len' (string expected)");
        return false;
    }
    Value sVal = vm->peek(argCount - 1);
    if (!sVal.isString()) {
        vm->runtimeError("bad argument #1 to 'utf8.len' (string expected)");
        return false;
    }
    std::string s_str = vm->getStringValue(sVal);
    size_t len = s_str.length();

    int64_t posi = 1;
    if (argCount >= 2 && !vm->peek(argCount - 2).isNil()) {
        Value iv = vm->peek(argCount - 2);
        if (!iv.isInteger() && !iv.isNumber()) {
            vm->runtimeError("bad argument #2 to 'utf8.len' (number expected)");
            return false;
        }
        posi = iv.isInteger() ? iv.asInteger() : static_cast<int64_t>(iv.asNumber());
    }

    int64_t posj = -1;
    if (argCount >= 3 && !vm->peek(argCount - 3).isNil()) {
        Value jv = vm->peek(argCount - 3);
        if (!jv.isInteger() && !jv.isNumber()) {
            vm->runtimeError("bad argument #3 to 'utf8.len' (number expected)");
            return false;
        }
        posj = jv.isInteger() ? jv.asInteger() : static_cast<int64_t>(jv.asNumber());
    }

    bool lax = false;
    if (argCount >= 4) {
        lax = vm->peek(argCount - 4).isTruthy();
    }

    if (posi < 0) posi = static_cast<int64_t>(len) + posi + 1;
    if (posj < 0) posj = static_cast<int64_t>(len) + posj + 1;

    if (posi < 1 || --posi > static_cast<int64_t>(len)) {
        vm->runtimeError("initial position out of bounds");
        return false;
    }
    if (--posj >= static_cast<int64_t>(len)) {
        vm->runtimeError("final position out of bounds");
        return false;
    }

    const char* s = s_str.c_str();
    const char* se = s + len;
    int64_t n = 0;

    while (posi <= posj) {
        uint32_t cp;
        const char* s1 = utf8_decode(s + posi, se, &cp, lax);
        if (!s1) {
            for (int k = 0; k < argCount; k++) vm->pop();
            vm->push(Value::nil());
            vm->push(vm->makeInteger(posi + 1));
            vm->currentCoroutine()->lastResultCount = 2;
            return true;
        }
        posi = s1 - s;
        n++;
    }

    for (int k = 0; k < argCount; k++) vm->pop();
    vm->push(vm->makeInteger(n));
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_utf8_codepoint(VM* vm, int argCount) {
    if (argCount < 1) {
        vm->runtimeError("bad argument #1 to 'utf8.codepoint' (string expected)");
        return false;
    }
    Value sVal = vm->peek(argCount - 1);
    if (!sVal.isString()) {
        vm->runtimeError("bad argument #1 to 'utf8.codepoint' (string expected)");
        return false;
    }
    std::string s_str = vm->getStringValue(sVal);
    size_t len = s_str.length();

    int64_t i = 1;
    if (argCount >= 2 && !vm->peek(argCount - 2).isNil()) {
        Value iv = vm->peek(argCount - 2);
        if (!iv.isInteger() && !iv.isNumber()) {
            vm->runtimeError("bad argument #2 to 'utf8.codepoint' (number expected)");
            return false;
        }
        i = iv.isInteger() ? iv.asInteger() : static_cast<int64_t>(iv.asNumber());
    }

    int64_t j = i;
    if (argCount >= 3 && !vm->peek(argCount - 3).isNil()) {
        Value jv = vm->peek(argCount - 3);
        if (!jv.isInteger() && !jv.isNumber()) {
            vm->runtimeError("bad argument #3 to 'utf8.codepoint' (number expected)");
            return false;
        }
        j = jv.isInteger() ? jv.asInteger() : static_cast<int64_t>(jv.asNumber());
    }

    bool lax = false;
    if (argCount >= 4) {
        lax = vm->peek(argCount - 4).isTruthy();
    }

    if (i < 0) i = static_cast<int64_t>(len) + i + 1;
    if (j < 0) j = static_cast<int64_t>(len) + j + 1;

    if (i > j) {
        for (int k = 0; k < argCount; k++) vm->pop();
        vm->currentCoroutine()->lastResultCount = 0;
        return true;
    }

    if (i < 1 || i > static_cast<int64_t>(len) || j > static_cast<int64_t>(len)) {
        vm->runtimeError("out of bounds");
        return false;
    }

    const char* s_start = s_str.c_str();
    const char* s_end = s_start + len;
    const char* p = s_start + i - 1;
    const char* limit = s_start + j;

    std::vector<uint32_t> cps;
    while (p < limit) {
        uint32_t cp;
        const char* next = utf8_decode(p, s_end, &cp, lax);
        if (!next) {
            vm->runtimeError("invalid UTF-8 code");
            return false;
        }
        cps.push_back(cp);
        p = next;
    }

    for (int k = 0; k < argCount; k++) vm->pop();
    for (uint32_t cp : cps) {
        vm->push(vm->makeInteger(cp));
    }
    vm->currentCoroutine()->lastResultCount = cps.size();
    return true;
}

bool native_utf8_offset(VM* vm, int argCount) {
    if (argCount < 2) {
        vm->runtimeError("bad argument #2 to 'utf8.offset' (number expected)");
        return false;
    }
    Value sVal = vm->peek(argCount - 1);
    if (!sVal.isString()) {
        vm->runtimeError("bad argument #1 to 'utf8.offset' (string expected)");
        return false;
    }
    std::string s_str = vm->getStringValue(sVal);
    size_t len = s_str.length();

    Value nVal = vm->peek(argCount - 2);
    if (!nVal.isInteger() && !nVal.isNumber()) {
        vm->runtimeError("bad argument #2 to 'utf8.offset' (number expected)");
        return false;
    }
    int64_t n = nVal.isInteger() ? nVal.asInteger() : static_cast<int64_t>(nVal.asNumber());

    int64_t posi = (n >= 0) ? 1 : static_cast<int64_t>(len) + 1;
    if (argCount >= 3 && !vm->peek(argCount - 3).isNil()) {
        Value iv = vm->peek(argCount - 3);
        if (!iv.isInteger() && !iv.isNumber()) {
            vm->runtimeError("bad argument #3 to 'utf8.offset' (number expected)");
            return false;
        }
        posi = iv.isInteger() ? iv.asInteger() : static_cast<int64_t>(iv.asNumber());
    }

    if (posi < 0) posi = static_cast<int64_t>(len) + posi + 1;

    if (posi < 1 || posi > static_cast<int64_t>(len) + 1) {
        vm->runtimeError("position out of bounds");
        return false;
    }

    const char* s = s_str.c_str();
    const char* se = s + len;

    // 0-based position
    posi--;

    auto iscontp = [](const char* p) {
        return (static_cast<unsigned char>(*p) & 0xC0) == 0x80;
    };

    if (n == 0) {
        while (posi > 0 && iscontp(s + posi)) posi--;
    } else {
        if (posi < static_cast<int64_t>(len) && iscontp(s + posi)) {
            vm->runtimeError("initial position is a continuation byte");
            return false;
        }
        if (n < 0) {
            while (n < 0 && posi > 0) {
                do {
                    posi--;
                } while (posi > 0 && iscontp(s + posi));
                n++;
            }
            if (iscontp(s + posi)) {
                vm->runtimeError("initial position is a continuation byte");
                return false;
            }
        } else {
            n--; // do not move for 1st character
            while (n > 0 && posi < static_cast<int64_t>(len)) {
                do {
                    posi++;
                } while (posi < static_cast<int64_t>(len) && iscontp(s + posi));
                n--;
            }
        }
    }

    if (n == 0) {
        for (int k = 0; k < argCount; k++) vm->pop();
        if (posi >= static_cast<int64_t>(len)) {
            vm->push(vm->makeInteger(len + 1));
            vm->push(vm->makeInteger(len + 1));
        } else {
            const char* q = s + posi + 1;
            while (q < se && iscontp(q)) q++;
            vm->push(vm->makeInteger(posi + 1));
            vm->push(vm->makeInteger(q - s));
        }
        vm->currentCoroutine()->lastResultCount = 2;
        return true;
    } else {
        for (int k = 0; k < argCount; k++) vm->pop();
        vm->push(Value::nil());
        vm->currentCoroutine()->lastResultCount = 1;
        return true;
    }
}

static size_t g_utf8_iter_idx = 0;
static size_t g_utf8_iter_lax_idx = 0;

static bool native_utf8_iter_impl(VM* vm, int argCount, bool lax) {
    if (argCount < 1 || !vm->peek(argCount - 1).isString()) {
        vm->runtimeError("bad argument #1 to 'utf8.codes' iterator (string expected)");
        return false;
    }
    std::string s_str = vm->getStringValue(vm->peek(argCount - 1));
    size_t len = s_str.length();

    uint64_t n = 0;
    if (argCount >= 2 && !vm->peek(argCount - 2).isNil()) {
        Value pv = vm->peek(argCount - 2);
        if (!pv.isInteger() && !pv.isNumber()) {
            vm->runtimeError("bad argument #2 to 'utf8.codes' iterator (number expected)");
            return false;
        }
        n = static_cast<uint64_t>(pv.isInteger() ? pv.asInteger() : static_cast<int64_t>(pv.asNumber()));
    }

    const char* s = s_str.c_str();
    const char* se = s + len;

    if (n < len) {
        while (n < len && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) {
            n++;
        }
    }

    if (n >= len) {
        for (int k = 0; k < argCount; k++) vm->pop();
        vm->push(Value::nil());
        vm->currentCoroutine()->lastResultCount = 1;
        return true;
    }

    uint32_t code = 0;
    const char* next = utf8_decode(s + n, se, &code, lax);
    if (!next || (next < se && (static_cast<unsigned char>(*next) & 0xC0) == 0x80)) {
        vm->runtimeError("invalid UTF-8 code");
        return false;
    }

    for (int k = 0; k < argCount; k++) vm->pop();
    vm->push(vm->makeInteger(n + 1));
    vm->push(vm->makeInteger(code));
    vm->currentCoroutine()->lastResultCount = 2;
    return true;
}

bool native_utf8_iter(VM* vm, int argCount) {
    return native_utf8_iter_impl(vm, argCount, false);
}

bool native_utf8_iter_lax(VM* vm, int argCount) {
    return native_utf8_iter_impl(vm, argCount, true);
}

bool native_utf8_codes(VM* vm, int argCount) {
    if (argCount < 1) {
        vm->runtimeError("bad argument #1 to 'utf8.codes' (string expected)");
        return false;
    }
    Value sVal = vm->peek(argCount - 1);
    if (!sVal.isString()) {
        vm->runtimeError("bad argument #1 to 'utf8.codes' (string expected)");
        return false;
    }
    std::string s_str = vm->getStringValue(sVal);
    if (!s_str.empty() && (static_cast<unsigned char>(s_str[0]) & 0xC0) == 0x80) {
        vm->runtimeError("invalid UTF-8 code");
        return false;
    }

    bool lax = false;
    if (argCount >= 2) {
        lax = vm->peek(argCount - 2).isTruthy();
    }

    if (g_utf8_iter_idx == 0) {
        g_utf8_iter_idx = vm->registerNativeFunction("__utf8_iter", native_utf8_iter);
    }
    if (g_utf8_iter_lax_idx == 0) {
        g_utf8_iter_lax_idx = vm->registerNativeFunction("__utf8_iter_lax", native_utf8_iter_lax);
    }

    for (int k = 0; k < argCount; k++) vm->pop();
    vm->push(Value::nativeFunction(lax ? g_utf8_iter_lax_idx : g_utf8_iter_idx));
    vm->push(sVal);
    vm->push(Value::integer(0));
    vm->currentCoroutine()->lastResultCount = 3;
    return true;
}

} // anonymous namespace

void registerUTF8Library(VM* vm, TableObject* utf8Table) {
    vm->addNativeToTable(utf8Table, "char", native_utf8_char);
    vm->addNativeToTable(utf8Table, "len", native_utf8_len);
    vm->addNativeToTable(utf8Table, "codepoint", native_utf8_codepoint);
    vm->addNativeToTable(utf8Table, "offset", native_utf8_offset);
    vm->addNativeToTable(utf8Table, "codes", native_utf8_codes);

    const char pattern[] = "[\0-\x7F\xC2-\xFD][\x80-\xBF]*";
    utf8Table->set("charpattern", Value::runtimeString(vm->internString(pattern, sizeof(pattern) - 1)));
}
