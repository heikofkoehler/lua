#include "vm/vm.hpp"
#include "value/string.hpp"
#include "value/table.hpp"
#include "value/closure.hpp"
#include "compiler/lexer.hpp"
#include "compiler/parser.hpp"
#include "compiler/codegen.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <vector>
#include <cstring>
#include <sstream>

namespace {

// Lua Pattern Matching Implementation
struct MatchState {
    VM* vm;
    const char* src_init;  // init of source string
    const char* src_end;   // end of source string
    const char* p_end;     // end of pattern
    int level;             // total number of captures
    struct {
        const char* init;
        ptrdiff_t len;
    } capture[32];        // LUA_MAXCAPTURES = 32
};

const char* match(MatchState* ms, const char* s, const char* p);

const char* match_class(char c, char cl) {
    bool res;
    switch (tolower(cl)) {
        case 'a': res = isalpha((unsigned char)c); break;
        case 'c': res = iscntrl((unsigned char)c); break;
        case 'd': res = isdigit((unsigned char)c); break;
        case 'g': res = isgraph((unsigned char)c); break;
        case 'l': res = islower((unsigned char)c); break;
        case 'p': res = ispunct((unsigned char)c); break;
        case 's': res = isspace((unsigned char)c); break;
        case 'u': res = isupper((unsigned char)c); break;
        case 'w': res = isalnum((unsigned char)c); break;
        case 'x': res = isxdigit((unsigned char)c); break;
        case 'z': res = (c == 0); break;
        default: return (cl == c) ? "" : nullptr;
    }
    return (isupper(cl) ? !res : res) ? "" : nullptr;
}

bool single_match(char c, const char* p, const char* ep) {
    switch (*p) {
        case '.': return true;
        case '%': return match_class(c, p[1]) != nullptr;
        case '[': {
            bool neg = (p[1] == '^');
            const char* curr = neg ? p + 2 : p + 1;
            bool found = false;
            while (curr < ep - 1) {
                if (*curr == '%') {
                    if (match_class(c, curr[1])) found = true;
                    curr += 2;
                } else if (curr + 2 < ep - 1 && curr[1] == '-') {
                    if ((unsigned char)curr[0] <= (unsigned char)c && (unsigned char)c <= (unsigned char)curr[2])
                        found = true;
                    curr += 3;
                } else {
                    if (c == *curr) found = true;
                    curr++;
                }
            }
            return neg ? !found : found;
        }
        default: return (unsigned char)c == (unsigned char)*p;
    }
}

const char* class_end(MatchState* ms, const char* p) {
    switch (*p++) {
        case '%':
            if (p == ms->p_end) ms->vm->runtimeError("malformed pattern (ends with '%')");
            return p + 1;
        case '[':
            if (*p == '^') p++;
            do {
                if (p == ms->p_end) ms->vm->runtimeError("malformed pattern (missing ']')");
                if (*(p++) == '%' && p < ms->p_end) p++;
            } while (*p != ']');
            return p + 1;
        default:
            return p;
    }
}

const char* match_quant(MatchState* ms, const char* s, const char* p, const char* ep) {
    char op = *ep;
    ptrdiff_t count = 0;
    while (s + count < ms->src_end && single_match(s[count], p, ep)) {
        count++;
    }
    
    switch (op) {
        case '?': {
            const char* res;
            if ((res = match(ms, s + 1, ep + 1))) return res;
            return match(ms, s, ep + 1);
        }
        case '+':
            if (count == 0) return nullptr;
            [[fallthrough]];
        case '*':
            while (count >= 0) {
                const char* res = match(ms, s + count, ep + 1);
                if (res) return res;
                count--;
            }
            return nullptr;
        case '-': // lazy *
            for (ptrdiff_t i = 0; i <= count; i++) {
                const char* res = match(ms, s + i, ep + 1);
                if (res) return res;
            }
            return nullptr;
        default:
            return nullptr;
    }
}

const char* start_capture(MatchState* ms, const char* s, const char* p) {
    int level = ms->level;
    if (level >= 32) ms->vm->runtimeError("too many captures");
    ms->capture[level].init = s;
    ms->capture[level].len = -1; 
    ms->level = level + 1;
    const char* res = match(ms, s, p);
    if (!res) ms->level--; 
    return res;
}

const char* end_capture(MatchState* ms, const char* s, const char* p) {
    int l;
    for (l = ms->level - 1; l >= 0; l--) {
        if (ms->capture[l].len == -1) break;
    }
    if (l < 0) ms->vm->runtimeError("invalid pattern capture");
    ms->capture[l].len = s - ms->capture[l].init;
    const char* res = match(ms, s, p);
    if (!res) ms->capture[l].len = -1;
    return res;
}

const char* match_capture(MatchState* ms, const char* s, int l) {
    l -= '1';
    if (l < 0 || l >= ms->level || ms->capture[l].len == -1)
        ms->vm->runtimeError("invalid capture index");
    ptrdiff_t len = ms->capture[l].len;
    if (ms->src_end - s >= len && memcmp(ms->capture[l].init, s, len) == 0)
        return s + len;
    return nullptr;
}

const char* match(MatchState* ms, const char* s, const char* p) {
    if (p == ms->p_end) return s;
    
    switch (*p) {
        case '(':
            if (*(p + 1) == ')') return match(ms, s, p + 2); // position capture
            return start_capture(ms, s, p + 1);
        case ')':
            return end_capture(ms, s, p + 1);
        case '%':
            if (isdigit((unsigned char)p[1])) {
                const char* res = match_capture(ms, s, p[1]);
                if (res) return match(ms, res, p + 2);
                return nullptr;
            }
            [[fallthrough]];
        case '$':
            if (p + 1 == ms->p_end) return (s == ms->src_end) ? s : nullptr;
            [[fallthrough]];
        default: {
            const char* ep = class_end(ms, p);
            bool m = (s < ms->src_end && single_match(*s, p, ep));
            if (ep < ms->p_end && strchr("*+-?", *ep)) {
                return match_quant(ms, s, p, ep);
            } else {
                return m ? match(ms, s + 1, ep) : nullptr;
            }
        }
    }
}

void push_captures(MatchState* ms, const char* s, const char* e) {
    int nlevels = (ms->level == 0 && s) ? 1 : ms->level;
    for (int i = 0; i < nlevels; i++) {
        if (ms->level == 0) {
            ms->vm->push(Value::runtimeString(ms->vm->internString(std::string(s, e - s))));
        } else {
            if (ms->capture[i].len == -1) {
                ms->vm->push(Value::number(ms->capture[i].init - ms->src_init + 1));
            } else {
                ms->vm->push(Value::runtimeString(ms->vm->internString(std::string(ms->capture[i].init, ms->capture[i].len))));
            }
        }
    }
}

bool native_string_len(VM* vm, int argCount) {
    if (argCount != 1) {
        vm->runtimeError("string.len expects 1 argument");
        return false;
    }
    Value str = vm->peek(0);
    if (!str.isString()) {
        vm->runtimeError("string.len expects a string");
        return false;
    }
    std::string s = vm->getStringValue(str);
    vm->pop();
    vm->push(Value::number(static_cast<double>(s.length())));
    return true;
}

bool native_string_upper(VM* vm, int argCount) {
    if (argCount != 1) {
        vm->runtimeError("string.upper expects 1 argument");
        return false;
    }
    Value str = vm->peek(0);
    std::string s = vm->getStringValue(str);
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
    Value str = vm->peek(0);
    std::string s = vm->getStringValue(str);
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
    Value str = vm->peek(0);
    std::string s = vm->getStringValue(str);
    std::reverse(s.begin(), s.end());
    vm->pop();
    vm->push(Value::runtimeString(vm->internString(s)));
    return true;
}

bool native_string_sub(VM* vm, int argCount) {
    if (argCount < 2 || argCount > 3) {
        vm->runtimeError("string.sub expects 2 or 3 arguments");
        return false;
    }
    Value endVal = (argCount == 3) ? vm->peek(0) : Value::number(-1);
    Value startVal = vm->peek(argCount - 2);
    Value strVal = vm->peek(argCount - 1);

    std::string s = vm->getStringValue(strVal);
    int start = static_cast<int>(startVal.asNumber());
    int end = static_cast<int>(endVal.asNumber());

    int len = static_cast<int>(s.length());
    if (start < 0) start = len + start + 1;
    if (end < 0) end = len + end + 1;
    if (start < 1) start = 1;
    if (end > len) end = len;

    std::string result = "";
    if (start <= end && start <= len) {
        result = s.substr(start - 1, end - start + 1);
    }

    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(Value::runtimeString(vm->internString(result)));
    return true;
}

bool native_string_byte(VM* vm, int argCount) {
    if (argCount < 1 || argCount > 3) {
        vm->runtimeError("string.byte expects 1 to 3 arguments");
        return false;
    }
    Value endVal = (argCount >= 3) ? vm->peek(0) : Value::nil();
    Value startVal = (argCount >= 2) ? vm->peek(argCount - 2) : Value::number(1);
    Value strVal = vm->peek(argCount - 1);

    std::string s = vm->getStringValue(strVal);
    int len = static_cast<int>(s.length());
    int start = static_cast<int>(startVal.asNumber());
    int end = endVal.isNil() ? start : static_cast<int>(endVal.asNumber());

    if (start < 0) start = len + start + 1;
    if (end < 0) end = len + end + 1;
    if (start < 1) start = 1;
    if (end > len) end = len;

    for (int i = 0; i < argCount; i++) vm->pop();
    
    int count = 0;
    for (int i = start; i <= end; i++) {
        vm->push(Value::number(static_cast<unsigned char>(s[i - 1])));
        count++;
    }
    vm->currentCoroutine()->lastResultCount = count;
    return true;
}

bool native_string_char(VM* vm, int argCount) {
    std::string result = "";
    for (int i = 0; i < argCount; i++) {
        Value v = vm->peek(argCount - 1 - i);
        result += static_cast<char>(static_cast<int>(v.asNumber()));
    }
    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(Value::runtimeString(vm->internString(result)));
    return true;
}

bool native_string_find(VM* vm, int argCount) {
    if (argCount < 2 || argCount > 4) {
        vm->runtimeError("string.find expects 2 to 4 arguments");
        return false;
    }
    Value plainVal = (argCount == 4) ? vm->peek(0) : Value::boolean(false);
    Value startVal = (argCount >= 3) ? vm->peek(argCount - 3) : Value::number(1);
    Value patternVal = vm->peek(argCount - 2);
    Value strVal = vm->peek(argCount - 1);

    if (!strVal.isString() || !patternVal.isString()) {
        vm->runtimeError("string.find expects string arguments");
        return false;
    }
    
    std::string s_str = vm->getStringValue(strVal);
    std::string p_str = vm->getStringValue(patternVal);
    int start = static_cast<int>(startVal.asNumber());
    size_t s_len = s_str.length();
    if (start < 0) start = s_len + start + 1;
    if (start < 1) start = 1;

    const char* s = s_str.c_str();
    const char* p = p_str.c_str();

    if (plainVal.isTruthy() || !strpbrk(p, "^$*+-.?()[]%")) {
        size_t pos = s_str.find(p_str, start - 1);
        if (pos != std::string::npos) {
            for (int i = 0; i < argCount; i++) vm->pop();
            vm->push(Value::number(pos + 1));
            vm->push(Value::number(pos + p_str.length()));
            vm->currentCoroutine()->lastResultCount = 2;
            return true;
        }
    } else {
        MatchState ms;
        ms.vm = vm;
        ms.src_init = s;
        ms.src_end = s + s_len;
        ms.p_end = p + p_str.length();
        
        bool anchor = (*p == '^');
        if (anchor) p++;
        
        const char* s1 = s + start - 1;
        do {
            ms.level = 0;
            const char* res = match(&ms, s1, p);
            if (res) {
                for (int i = 0; i < argCount; i++) vm->pop();
                vm->push(Value::number(s1 - s + 1));
                vm->push(Value::number(res - s));
                push_captures(&ms, nullptr, nullptr);
                vm->currentCoroutine()->lastResultCount = 2 + ms.level;
                return true;
            }
        } while (s1++ < ms.src_end && !anchor);
    }

    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(Value::nil());
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_string_match(VM* vm, int argCount) {
    if (argCount < 2 || argCount > 3) {
        vm->runtimeError("string.match expects 2 or 3 arguments");
        return false;
    }
    Value startVal = (argCount == 3) ? vm->peek(0) : Value::number(1);
    Value patternVal = vm->peek(argCount - 2);
    Value strVal = vm->peek(argCount - 1);

    std::string s_str = vm->getStringValue(strVal);
    std::string p_str = vm->getStringValue(patternVal);
    int start = static_cast<int>(startVal.asNumber());
    size_t s_len = s_str.length();
    if (start < 0) start = s_len + start + 1;
    if (start < 1) start = 1;

    const char* s = s_str.c_str();
    const char* p = p_str.c_str();

    MatchState ms;
    ms.vm = vm;
    ms.src_init = s;
    ms.src_end = s + s_len;
    ms.p_end = p + p_str.length();
    
    bool anchor = (*p == '^');
    if (anchor) p++;
    
    const char* s1 = s + start - 1;
    do {
        ms.level = 0;
        const char* res = match(&ms, s1, p);
        if (res) {
            for (int i = 0; i < argCount; i++) vm->pop();
            push_captures(&ms, s1, res);
            vm->currentCoroutine()->lastResultCount = (ms.level == 0) ? 1 : ms.level;
            return true;
        }
    } while (s1++ < ms.src_end && !anchor);

    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(Value::nil());
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_string_gmatch_step(VM* vm, int argCount) {
    if (argCount != 3) {
        vm->runtimeError("string.__gmatch_step expects 3 arguments");
        return false;
    }
    Value posVal = vm->peek(0);
    Value patVal = vm->peek(1);
    Value strVal = vm->peek(2);

    std::string s_str = vm->getStringValue(strVal);
    std::string p_str = vm->getStringValue(patVal);
    int start = static_cast<int>(posVal.asNumber());
    size_t s_len = s_str.length();

    const char* s = s_str.c_str();
    const char* p = p_str.c_str();

    MatchState ms;
    ms.vm = vm;
    ms.src_init = s;
    ms.src_end = s + s_len;
    ms.p_end = p + p_str.length();

    for (const char* s1 = s + start - 1; s1 <= ms.src_end; s1++) {
        ms.level = 0;
        const char* res = match(&ms, s1, p);
        if (res) {
            int next_pos = (res == s1) ? (static_cast<int>(res - s) + 2) : (static_cast<int>(res - s) + 1);
            for(int i=0; i<argCount; i++) vm->pop();
            vm->push(Value::number(next_pos));
            push_captures(&ms, s1, res);
            vm->currentCoroutine()->lastResultCount = 1 + ((ms.level == 0) ? 1 : ms.level);
            return true;
        }
    }

    for(int i=0; i<argCount; i++) vm->pop();
    return true;
}

bool native_string_gmatch(VM* vm, int argCount) {
    if (argCount != 2) {
        vm->runtimeError("string.gmatch expects 2 arguments");
        return false;
    }
    Value patVal = vm->peek(0);
    Value strVal = vm->peek(1);

    const char* script = 
        "local s, p = ...\n"
        "local pos = 1\n"
        "return function()\n"
        "  local res = { string.__gmatch_step(s, p, pos) }\n"
        "  if #res == 0 then return nil end\n"
        "  pos = res[1]\n"
        "  table.remove(res, 1)\n"
        "  if #res == 0 then return nil end\n"
        "  return table.unpack(res)\n"
        "end\n";
    
    for (int i = 0; i < argCount; i++) vm->pop();

    FunctionObject* func = vm->compileSource(script, "string.gmatch");
    if (!func) return false;

    ClosureObject* closure = vm->createClosure(func);
    vm->setupRootUpvalues(closure);

    vm->push(Value::closure(closure));
    vm->push(strVal);
    vm->push(patVal);
    
    if (vm->callValue(2, 2)) {
        size_t baseFrames = vm->currentCoroutine()->frames.size() - 1;
        vm->run(baseFrames);
        Value iter = vm->pop();
        
        vm->push(iter);
        vm->push(Value::nil());
        vm->push(Value::nil());
        vm->currentCoroutine()->lastResultCount = 3;
        return true;
    }
    return false;
}

bool native_string_gsub(VM* vm, int argCount) {
    if (argCount < 3 || argCount > 4) {
        vm->runtimeError("string.gsub expects 3 or 4 arguments");
        return false;
    }
    Value maxVal = (argCount == 4) ? vm->peek(0) : Value::number(-1);
    Value replVal = vm->peek(argCount - 3);
    Value patternVal = vm->peek(argCount - 2);
    Value strVal = vm->peek(argCount - 1);

    std::string s_str = vm->getStringValue(strVal);
    std::string p_str = vm->getStringValue(patternVal);
    int max_subs = static_cast<int>(maxVal.asNumber());

    const char* s = s_str.c_str();
    const char* p = p_str.c_str();
    size_t s_len = s_str.length();

    MatchState ms;
    ms.vm = vm;
    ms.src_init = s;
    ms.src_end = s + s_len;
    ms.p_end = p + p_str.length();

    bool anchor = (*p == '^');
    if (anchor) p++;

    std::string result;
    int count = 0;
    const char* s1 = s;
    while (s1 <= ms.src_end && (max_subs < 0 || count < max_subs)) {
        ms.level = 0;
        const char* res = match(&ms, s1, p);
        if (res) {
            count++;
            if (replVal.isString()) {
                std::string r = vm->getStringValue(replVal);
                for (size_t i = 0; i < r.length(); i++) {
                    if (r[i] == '%' && i + 1 < r.length()) {
                        i++;
                        if (isdigit((unsigned char)r[i])) {
                            int cap = r[i] - '0';
                            if (cap == 0) result.append(s1, res - s1);
                            else if (cap <= ms.level && ms.capture[cap-1].len != -1)
                                result.append(ms.capture[cap-1].init, ms.capture[cap-1].len);
                        } else result.push_back(r[i]);
                    } else result.push_back(r[i]);
                }
            } else if (replVal.isTable()) {
                Value key = (ms.level == 0) ? 
                    Value::runtimeString(vm->internString(std::string(s1, res - s1))) :
                    Value::runtimeString(vm->internString(std::string(ms.capture[0].init, ms.capture[0].len)));
                Value val = replVal.asTableObj()->get(key);
                if (!val.isNil()) result.append(val.toString());
                else result.append(s1, res - s1);
            } else if (replVal.isFunction()) {
                int ncaps = (ms.level == 0) ? 1 : ms.level;
                vm->push(replVal);
                if (ms.level == 0) vm->push(Value::runtimeString(vm->internString(std::string(s1, res - s1))));
                else {
                    for (int i = 0; i < ms.level; i++)
                        vm->push(Value::runtimeString(vm->internString(std::string(ms.capture[i].init, ms.capture[i].len))));
                }
                
                size_t baseFrames = vm->currentCoroutine()->frames.size();
                if (vm->callValue(ncaps, 2)) {
                    if (vm->currentCoroutine()->frames.size() > baseFrames) {
                        vm->run(baseFrames);
                    }
                    Value v = vm->pop();
                    if (!v.isNil()) result.append(v.toString());
                    else result.append(s1, res - s1);
                } else return false;
            }
            if (res == s1) { if (s1 < ms.src_end) result.push_back(*s1); s1++; }
            else s1 = res;
            if (anchor) break;
        } else {
            if (anchor) break;
            if (s1 < ms.src_end) result.push_back(*s1);
            s1++;
        }
    }
    if (s1 < ms.src_end) result.append(s1, ms.src_end - s1);
    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(Value::runtimeString(vm->internString(result)));
    vm->push(Value::number(count));
    vm->currentCoroutine()->lastResultCount = 2;
    return true;
}

bool native_string_format(VM* vm, int argCount) {
    if (argCount < 1) {
        vm->runtimeError("string.format expects at least 1 argument");
        return false;
    }
    std::string fmt = vm->getStringValue(vm->peek(argCount - 1));
    std::string result;
    int argIndex = 1;
    
    for (size_t i = 0; i < fmt.length(); i++) {
        if (fmt[i] == '%' && i + 1 < fmt.length()) {
            i++;
            char spec = fmt[i];
            if (spec == '%') {
                result += '%';
            } else {
                if (argIndex >= argCount) {
                    vm->runtimeError("bad argument to 'format' (no value)");
                    return false;
                }
                Value arg = vm->peek(argCount - 1 - argIndex);
                if (spec == 's') {
                    result += vm->getStringValue(arg);
                } else if (spec == 'q') {
                    std::string s = vm->getStringValue(arg);
                    result += '"';
                    for (char c : s) {
                        if (c == '"' || c == '\\' || c == '\n') {
                            result += '\\';
                            if (c == '\n') result += 'n';
                            else result += c;
                        } else if (iscntrl((unsigned char)c)) {
                            char buf[5];
                            snprintf(buf, sizeof(buf), "\\%03d", (unsigned char)c);
                            result += buf;
                        } else {
                            result += c;
                        }
                    }
                    result += '"';
                } else if (spec == 'd' || spec == 'i' || spec == 'x' || spec == 'X') {
                    if (!arg.isNumber()) {
                        vm->runtimeError("bad argument to 'format' (number expected)");
                        return false;
                    }
                    if (spec == 'd' || spec == 'i') {
                        result += std::to_string(arg.asInteger());
                    } else {
                        char buf[32];
                        snprintf(buf, sizeof(buf), spec == 'x' ? "%llx" : "%llX", (unsigned long long)arg.asInteger());
                        result += buf;
                    }
                } else if (spec == 'f') {
                    if (!arg.isNumber()) {
                        vm->runtimeError("bad argument to 'format' (number expected)");
                        return false;
                    }
                    char buf[64];
                    snprintf(buf, sizeof(buf), "%f", arg.asNumber());
                    result += buf;
                } else if (spec == 'p') {
                    char buf[32];
                    if (arg.isObj()) {
                        snprintf(buf, sizeof(buf), "%p", (void*)arg.asObj());
                    } else {
                        snprintf(buf, sizeof(buf), "0x%llx", (unsigned long long)arg.asInteger());
                    }
                    result += buf;
                } else if (spec == 'c') {
                    if (!arg.isNumber()) {
                        vm->runtimeError("bad argument to 'format' (number expected)");
                        return false;
                    }
                    result += static_cast<char>(arg.asInteger());
                } else {
                    result += '%';
                    result += spec; // Unhandled format specifier
                }
                argIndex++;
            }
        } else {
            result += fmt[i];
        }
    }
    
    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(Value::runtimeString(vm->internString(result)));
    return true;
}

// Helper for string.pack/unpack
struct PackState {
    bool littleEndian = true; // Default for most modern systems
    size_t alignment = 1;
};

static size_t get_size(char spec, int& size) {
    switch (spec) {
        case 'b': case 'B': size = 1; return 1;
        case 'h': case 'H': size = 2; return 2;
        case 'i': case 'I': case 'l': case 'L': case 'j': case 'J': case 'T': size = 8; return 8; // simplified
        case 'f': size = 4; return 4;
        case 'd': case 'n': size = 8; return 8;
        default: size = 0; return 0;
    }
}

bool native_string_packsize(VM* vm, int argCount) {
    if (argCount < 1) { vm->runtimeError("string.packsize expects format"); return false; }
    std::string fmt = vm->getStringValue(vm->peek(argCount - 1));
    size_t total = 0;
    for (size_t i = 0; i < fmt.length(); i++) {
        int size;
        if (get_size(fmt[i], size) > 0) total += size;
        else if (fmt[i] == 's') {
            vm->runtimeError("variable-length format in string.packsize");
            return false;
        }
    }
    for(int i=0; i<argCount; i++) vm->pop();
    vm->push(Value::number(static_cast<double>(total)));
    return true;
}

bool native_string_pack(VM* vm, int argCount) {
    if (argCount < 1) { vm->runtimeError("string.pack expects format"); return false; }
    std::string fmt = vm->getStringValue(vm->peek(argCount - 1));
    std::string result;
    int argIdx = 1;

    for (size_t i = 0; i < fmt.length(); i++) {
        char spec = fmt[i];
        if (spec == '<') continue; // simplified
        if (spec == '>') continue;
        
        if (argIdx >= argCount) { vm->runtimeError("bad argument to 'pack' (no value)"); return false; }
        Value val = vm->peek(argCount - 1 - argIdx);
        
        if (spec == 'b' || spec == 'B') {
            unsigned char b = static_cast<unsigned char>(val.asNumber());
            result.push_back(b);
        } else if (spec == 'i' || spec == 'I' || spec == 'j' || spec == 'J') {
            int64_t v = val.asInteger();
            result.append(reinterpret_cast<char*>(&v), 8);
        } else if (spec == 'n' || spec == 'd') {
            double v = val.asNumber();
            result.append(reinterpret_cast<char*>(&v), 8);
        } else if (spec == 's') {
            std::string s = vm->getStringValue(val);
            uint64_t len = s.length();
            result.append(reinterpret_cast<char*>(&len), 8);
            result.append(s);
        } else if (spec == 'z') {
            std::string s = vm->getStringValue(val);
            result.append(s);
            result.push_back('\0');
        }
        argIdx++;
    }

    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(Value::runtimeString(vm->internString(result)));
    return true;
}

bool native_string_unpack(VM* vm, int argCount) {
    if (argCount < 2) { vm->runtimeError("string.unpack expects format and string"); return false; }
    std::string fmt = vm->getStringValue(vm->peek(argCount - 1));
    std::string data = vm->getStringValue(vm->peek(argCount - 2));
    int pos = (argCount >= 3) ? static_cast<int>(vm->peek(argCount - 3).asNumber()) : 1;
    
    if (pos < 1 || pos > (int)data.length() + 1) {
        vm->runtimeError("initial position out of bounds");
        return false;
    }

    int current = pos - 1;
    int results = 0;

    for (size_t i = 0; i < fmt.length(); i++) {
        char spec = fmt[i];
        if (spec == '<' || spec == '>') continue;

        if (spec == 'b' || spec == 'B') {
            if (current + 1 > (int)data.length()) { vm->runtimeError("data string too short"); return false; }
            vm->push(Value::number(static_cast<unsigned char>(data[current])));
            current += 1;
        } else if (spec == 'i' || spec == 'I' || spec == 'j' || spec == 'J') {
            if (current + 8 > (int)data.length()) { vm->runtimeError("data string too short"); return false; }
            int64_t v;
            std::memcpy(&v, &data[current], 8);
            vm->push(Value::integer(v));
            current += 8;
        } else if (spec == 'n' || spec == 'd') {
            if (current + 8 > (int)data.length()) { vm->runtimeError("data string too short"); return false; }
            double v;
            std::memcpy(&v, &data[current], 8);
            vm->push(Value::number(v));
            current += 8;
        } else if (spec == 's') {
            if (current + 8 > (int)data.length()) { vm->runtimeError("data string too short"); return false; }
            uint64_t len;
            std::memcpy(&len, &data[current], 8);
            current += 8;
            if (current + (int)len > (int)data.length()) { vm->runtimeError("data string too short"); return false; }
            vm->push(Value::runtimeString(vm->internString(data.substr(current, len))));
            current += len;
        } else if (spec == 'z') {
            size_t null_pos = data.find('\0', current);
            if (null_pos == std::string::npos) { vm->runtimeError("unfinished string for format 'z'"); return false; }
            vm->push(Value::runtimeString(vm->internString(data.substr(current, null_pos - current))));
            current = (int)null_pos + 1;
        }
        results++;
    }

    vm->push(Value::number(current + 1));
    results++;

    // Actually we need to pop original arguments but keep results.
    // Standard approach: push results, then use a helper or manual stack management.
    // callValue/run handle this, but for native functions we must pop manually.
    std::vector<Value> res_vals;
    for(int i=0; i<results; i++) res_vals.push_back(vm->pop());
    std::reverse(res_vals.begin(), res_vals.end());
    
    for(int i=0; i<argCount; i++) vm->pop();
    for(const auto& v : res_vals) vm->push(v);
    vm->currentCoroutine()->lastResultCount = results;
    return true;
}

bool native_string_dump(VM* vm, int argCount) {
    if (argCount < 1) {
        vm->runtimeError("string.dump expects at least 1 argument");
        return false;
    }
    Value val = vm->peek(argCount - 1);
    if (!val.isClosure()) {
        vm->runtimeError("bad argument #1 to 'dump' (function expected)");
        return false;
    }

    ClosureObject* closure = val.asClosureObj();
    FunctionObject* function = closure->function();

    std::ostringstream os(std::ios::binary);
    // Add signature \x1bLua
    os.write("\x1bLua", 4);
    function->serialize(os);

    std::string bytecode = os.str();

    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(Value::runtimeString(vm->internString(bytecode)));
    return true;
}

} // anonymous namespace

void registerStringLibrary(VM* vm, TableObject* stringTable) {
    TableObject* stringMt = vm->createTable();
    Value stringTableVal = Value::table(stringTable);
    stringMt->set("__index", stringTableVal);
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
    vm->addNativeToTable(stringTable, "match", native_string_match);
    vm->addNativeToTable(stringTable, "gmatch", native_string_gmatch);
    vm->addNativeToTable(stringTable, "__gmatch_step", native_string_gmatch_step);
    vm->addNativeToTable(stringTable, "gsub", native_string_gsub);
    vm->addNativeToTable(stringTable, "format", native_string_format);
    vm->addNativeToTable(stringTable, "packsize", native_string_packsize);
    vm->addNativeToTable(stringTable, "pack", native_string_pack);
    vm->addNativeToTable(stringTable, "unpack", native_string_unpack);
    vm->addNativeToTable(stringTable, "dump", native_string_dump);
}
