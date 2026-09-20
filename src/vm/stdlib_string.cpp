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
#include <iostream>
#include <clocale>

namespace {

// Lua Pattern Matching Implementation
#define MAXCCALLS 200  // maximum recursion depth in pattern matching

struct MatchState {
    VM* vm;
    const char* src_init;  // init of source string
    const char* src_end;   // end of source string
    const char* p_end;     // end of pattern
    int level;             // total number of captures
    int matchdepth;        // current recursion depth
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
            if (count > 0 && (res = match(ms, s + 1, ep + 1))) return res;
            return match(ms, s, ep + 1);
        }
        case '+':
            if (count == 0) return nullptr;
            [[fallthrough]];
        case '*':
            while (count >= (op == '+' ? 1 : 0)) {
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

const char* match_balanced(MatchState* ms, const char* s, const char* p) {
    if (s >= ms->src_end || *s != *p) return nullptr;
    char b = *p;
    char e = *(p + 1);
    int cont = 1;
    while (++s < ms->src_end) {
        if (*s == e) {
            if (--cont == 0) return s + 1;
        } else if (*s == b) cont++;
    }
    return nullptr;
}

const char* match_frontier(MatchState* ms, const char* s, const char* p) {
    const char* ep = class_end(ms, p);
    char prev = (s == ms->src_init) ? '\0' : *(s - 1);
    char curr = (s == ms->src_end) ? '\0' : *s;
    if (!single_match(prev, p, ep) && single_match(curr, p, ep))
        return s;
    return nullptr;
}

const char* match(MatchState* ms, const char* s, const char* p) {
    if (ms->matchdepth-- == 0)
        ms->vm->runtimeError("pattern too complex");
    const char* res;
    if (p == ms->p_end) { res = s; goto ret; }
    
    switch (*p) {
        case '(':
            if (*(p + 1) == ')') { // position capture
                int level = ms->level;
                if (level >= 32) ms->vm->runtimeError("too many captures");
                ms->capture[level].init = s;
                ms->capture[level].len = -1;
                ms->level = level + 1;
                res = match(ms, s, p + 2);
                if (!res) ms->level--;
                goto ret;
            }
            res = start_capture(ms, s, p + 1); goto ret;
        case ')':
            res = end_capture(ms, s, p + 1); goto ret;
        case '%':
            if (p[1] == 'b') { // balanced string
                if (p + 3 >= ms->p_end) ms->vm->runtimeError("malformed pattern (missing arguments to '%b')");
                res = match_balanced(ms, s, p + 2);
                if (res) res = match(ms, res, p + 4);
                goto ret;
            }
            if (p[1] == 'f') { // frontier pattern
                p += 2;
                if (*p != '[') ms->vm->runtimeError("missing '[' after '%f' in pattern");
                const char* ep = class_end(ms, p);
                res = match_frontier(ms, s, p);
                if (res) res = match(ms, res, ep);
                goto ret;
            }
            if (isdigit((unsigned char)p[1])) {
                res = match_capture(ms, s, p[1]);
                if (res) res = match(ms, res, p + 2);
                goto ret;
            }
            [[fallthrough]];
        case '$':
            if (p + 1 == ms->p_end) { res = (s == ms->src_end) ? s : nullptr; goto ret; }
            [[fallthrough]];
        default: {
            const char* ep = class_end(ms, p);
            bool m = (s < ms->src_end && single_match(*s, p, ep));
            if (ep < ms->p_end && strchr("*+-?", *ep)) {
                res = match_quant(ms, s, p, ep);
            } else {
                res = m ? match(ms, s + 1, ep) : nullptr;
            }
            goto ret;
        }
    }
  ret:
    ms->matchdepth++;
    return res;
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
    vm->push(Value::integer(static_cast<int64_t>(s.length())));
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
    vm->currentCoroutine()->lastResultCount = 1;
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
    std::string result;
    result.reserve(argCount);
    for (int i = 0; i < argCount; i++) {
        Value v = vm->peek(argCount - 1 - i);
        int64_t c;
        if (!vm->toInteger(v, c)) {
            if (v.isNumber()) {
                vm->runtimeError("bad argument #" + std::to_string(i + 1) + " to 'char' (number has no integer representation)");
            } else {
                vm->runtimeError("bad argument #" + std::to_string(i + 1) + " to 'char' (number expected, got " + v.typeToString() + ")");
            }
            return false;
        }
        if (c < 0 || c > 255) {
            vm->runtimeError("bad argument #" + std::to_string(i + 1) + " to 'char' (value out of range)");
            return false;
        }
        result.push_back(static_cast<char>(static_cast<unsigned char>(c)));
    }
    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(Value::runtimeString(vm->internString(result)));
    vm->currentCoroutine()->lastResultCount = 1;
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

    if (!plainVal.isFalsey() || !strpbrk(p, "^$*+-.?()[]%")) {
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
        ms.matchdepth = MAXCCALLS;
        
        bool anchor = (*p == '^');
        if (anchor) p++;
        
        const char* s1 = s + start - 1;
        do {
            ms.level = 0;
            ms.matchdepth = MAXCCALLS;
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
    ms.matchdepth = MAXCCALLS;
    
    bool anchor = (*p == '^');
    if (anchor) p++;
    
    const char* s1 = s + start - 1;
    do {
        ms.level = 0;
        ms.matchdepth = MAXCCALLS;
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
    ms.matchdepth = MAXCCALLS;

    for (const char* s1 = s + start - 1; s1 <= ms.src_end; s1++) {
        ms.level = 0;
        ms.matchdepth = MAXCCALLS;
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
    vm->currentCoroutine()->lastResultCount = 0;
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
    ms.matchdepth = MAXCCALLS;

    bool anchor = (*p == '^');
    if (anchor) p++;

    std::string result;
    int count = 0;
    const char* s1 = s;
    while (s1 <= ms.src_end && (max_subs < 0 || count < max_subs)) {
        ms.level = 0;
        ms.matchdepth = MAXCCALLS;
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
                struct NonYieldableGuard {
                    CoroutineObject* co;
                    NonYieldableGuard(CoroutineObject* c) : co(c) { if (co) co->nonYieldableCount++; }
                    ~NonYieldableGuard() { if (co) co->nonYieldableCount--; }
                } nyGuard(vm->currentCoroutine());
                Value val = vm->getTable(replVal, key);
                if (val.isString() || val.isNumber()) {
                    result.append(val.toString());
                } else if (val.isFalsey()) {
                    result.append(s1, res - s1);
                } else {
                    vm->runtimeError("invalid replacement value (a " + val.typeToString() + ")");
                    return false;
                }
            } else if (replVal.isFunction()) {
                int ncaps = (ms.level == 0) ? 1 : ms.level;
                vm->push(replVal);
                if (ms.level == 0) vm->push(Value::runtimeString(vm->internString(std::string(s1, res - s1))));
                else {
                    for (int i = 0; i < ms.level; i++)
                        vm->push(Value::runtimeString(vm->internString(std::string(ms.capture[i].init, ms.capture[i].len))));
                }
                
                size_t baseFrames = vm->currentCoroutine()->frames.size();
                struct NonYieldableGuard {
                    CoroutineObject* co;
                    NonYieldableGuard(CoroutineObject* c) : co(c) { if (co) co->nonYieldableCount++; }
                    ~NonYieldableGuard() { if (co) co->nonYieldableCount--; }
                } nyGuard(vm->currentCoroutine());

                if (vm->callValue(ncaps, 2)) {
                    if (vm->currentCoroutine()->frames.size() > baseFrames) {
                        if (!vm->run(baseFrames)) return false;
                    }
                    Value v = vm->pop();
                    if (v.isString() || v.isNumber()) {
                        result.append(v.toString());
                    } else if (v.isFalsey()) {
                        result.append(s1, res - s1);
                    } else {
                        vm->runtimeError("invalid replacement value (a " + v.typeToString() + ")");
                        return false;
                    }
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
        if (fmt[i] == '%') {
            if (i + 1 < fmt.length() && fmt[i+1] == '%') {
                result += '%';
                i++;
                continue;
            }

            // Parse format specifier: %[flags][width][.precision]specifier
            size_t start = i;
            size_t p = i + 1;

            // Collect all characters of the specifier until an alphabetic character or invalid character
            while (p < fmt.length() && (strchr("-+ #0.", fmt[p]) || isdigit((unsigned char)fmt[p]))) {
                p++;
                if (p - start > 50) {
                    vm->runtimeError("invalid format (too long)");
                    return false;
                }
            }
            if (p < fmt.length() && isalpha((unsigned char)fmt[p])) {
                p++;
            }
            if (p - start > 50) {
                vm->runtimeError("invalid format (too long)");
                return false;
            }

            std::string sub_fmt = fmt.substr(start, p - start);
            i = p - 1; // update i to point to the end of the specifier

            size_t idx = 1;
            std::string flags;
            while (idx < sub_fmt.length() && strchr("-+ #0", sub_fmt[idx])) {
                flags += sub_fmt[idx++];
            }

            // Check for repeated flags
            bool repeated_flags = false;
            for (size_t f1 = 0; f1 < flags.length(); f1++) {
                for (size_t f2 = f1 + 1; f2 < flags.length(); f2++) {
                    if (flags[f1] == flags[f2]) repeated_flags = true;
                }
            }
            if (repeated_flags) {
                vm->runtimeError("invalid format (repeated flags)");
                return false;
            }

            // Width: at most 2 digits
            int width_digits = 0;
            while (idx < sub_fmt.length() && isdigit((unsigned char)sub_fmt[idx])) {
                width_digits++;
                idx++;
            }
            if (width_digits > 2) {
                vm->runtimeError("invalid conversion specification: '" + sub_fmt + "'");
                return false;
            }

            // Precision: optional dot followed by at most 2 digits
            bool has_precision = false;
            int prec_digits = 0;
            if (idx < sub_fmt.length() && sub_fmt[idx] == '.') {
                has_precision = true;
                idx++;
                while (idx < sub_fmt.length() && isdigit((unsigned char)sub_fmt[idx])) {
                    prec_digits++;
                    idx++;
                }
                if (prec_digits > 2) {
                    vm->runtimeError("invalid conversion specification: '" + sub_fmt + "'");
                    return false;
                }
            }

            // The next character must be the specifier, and must be the last character of sub_fmt
            if (idx != sub_fmt.length() - 1) {
                vm->runtimeError("invalid conversion specification: '" + sub_fmt + "'");
                return false;
            }

            char spec = sub_fmt[idx];
            const char* valid_specs = "cdiouxXfeEgGaApsq";
            if (strchr(valid_specs, spec) == nullptr) {
                vm->runtimeError("invalid conversion specification: '" + sub_fmt + "'");
                return false;
            }

            if (spec == 'q') {
                if (!flags.empty() || width_digits > 0 || has_precision) {
                    vm->runtimeError("specifier '%q' cannot have modifiers");
                    return false;
                }
            } else if (spec == 'c') {
                if (has_precision || flags.find_first_not_of('-') != std::string::npos) {
                    vm->runtimeError("invalid conversion specification: '" + sub_fmt + "'");
                    return false;
                }
            } else if (spec == 's') {
                if (flags.find_first_not_of('-') != std::string::npos) {
                    vm->runtimeError("invalid conversion specification: '" + sub_fmt + "'");
                    return false;
                }
            } else if (spec == 'p') {
                if (has_precision || flags.find_first_not_of('-') != std::string::npos) {
                    vm->runtimeError("invalid conversion specification: '" + sub_fmt + "'");
                    return false;
                }
            } else if (spec == 'd' || spec == 'i' || spec == 'u') {
                if (flags.find('#') != std::string::npos) {
                    vm->runtimeError("invalid conversion specification: '" + sub_fmt + "'");
                    return false;
                }
            } else if (spec == 'o' || spec == 'x' || spec == 'X') {
                if (flags.find('+') != std::string::npos || flags.find(' ') != std::string::npos) {
                    vm->runtimeError("invalid conversion specification: '" + sub_fmt + "'");
                    return false;
                }
            }

            if (argIndex >= argCount) {
                vm->runtimeError("bad argument #" + std::to_string(argIndex + 1) + " to 'format' (no value)");
                return false;
            }
            Value arg = vm->peek(argCount - 1 - argIndex);
            char buf[1024]; // Large enough for most formatting

            if (spec == 's') {
                std::string s;
                if (!vm->toLString(arg, s)) return false;
                if (sub_fmt == "%s") {
                    result += s;
                } else {
                    if (s.length() != strlen(s.c_str())) {
                        vm->runtimeError("bad argument #" + std::to_string(argIndex + 1) + " to 'format' (string contains zeros)");
                        return false;
                    }
                    int needed = snprintf(nullptr, 0, sub_fmt.c_str(), s.c_str());
                    if (needed >= 0) {
                        std::vector<char> dynBuf(needed + 1);
                        snprintf(dynBuf.data(), dynBuf.size(), sub_fmt.c_str(), s.c_str());
                        result += dynBuf.data();
                    }
                }
            } else if (spec == 'q') {
                if (arg.isNil()) {
                    result += "nil";
                } else if (arg.isBool()) {
                    result += arg.asBool() ? "true" : "false";
                } else if (arg.isInteger()) {
                    int64_t n = arg.asInteger();
                    if (n == std::numeric_limits<int64_t>::min()) {
                        snprintf(buf, sizeof(buf), "0x%llx", (unsigned long long)(uint64_t)n);
                    } else {
                        snprintf(buf, sizeof(buf), "%lld", (long long)n);
                    }
                    result += buf;
                } else if (arg.isFloat()) {
                    double d = arg.asNumber();
                    if (std::isnan(d)) {
                        result += "(0/0)";
                    } else if (std::isinf(d)) {
                        if (d > 0) result += "1e9999";
                        else result += "-1e9999";
                    } else {
                        snprintf(buf, sizeof(buf), "%a", d);
                        for (char* p = buf; *p; ++p) {
                            if (*p == ',') *p = '.';
                        }
                        result += buf;
                    }
                } else if (arg.isString()) {
                    std::string s = vm->getStringValue(arg);
                    result += '"';
                    size_t len = s.length();
                    for (size_t k = 0; k < len; k++) {
                        char c = s[k];
                        if (c == '"' || c == '\\' || c == '\n') {
                            result += '\\';
                            result += c;
                        } else if (iscntrl((unsigned char)c)) {
                            char b2[10];
                            if (k + 1 < len && isdigit((unsigned char)s[k + 1])) {
                                snprintf(b2, sizeof(b2), "\\%03d", (unsigned char)c);
                            } else {
                                snprintf(b2, sizeof(b2), "\\%d", (unsigned char)c);
                            }
                            result += b2;
                        } else {
                            result += c;
                        }
                    }
                    result += '"';
                } else {
                    vm->runtimeError("bad argument #" + std::to_string(argIndex) + " to 'format' (value has no literal form)");
                    return false;
                }
            } else if (spec == 'd' || spec == 'i' || spec == 'o' || spec == 'u' || spec == 'x' || spec == 'X') {
                if (!arg.isNumber()) { vm->runtimeError("number expected for %" + std::string(1, spec)); return false; }
                // Use ll for 64-bit integers. We need to inject 'll' before the specifier
                std::string fixed_fmt = sub_fmt.substr(0, sub_fmt.length() - 1) + "ll" + spec;
                snprintf(buf, sizeof(buf), fixed_fmt.c_str(), (long long)arg.asInteger());
                result += buf;
            } else if (spec == 'f' || spec == 'e' || spec == 'E' || spec == 'g' || spec == 'G' || spec == 'a' || spec == 'A') {
                if (!arg.isNumber()) { vm->runtimeError("number expected for %" + std::string(1, spec)); return false; }
                snprintf(buf, sizeof(buf), sub_fmt.c_str(), arg.asNumber());
                result += buf;
            } else if (spec == 'c') {
                if (!arg.isNumber()) { vm->runtimeError("number expected for %c"); return false; }
                snprintf(buf, sizeof(buf), sub_fmt.c_str(), (int)arg.asInteger());
                result += buf;
            } else if (spec == 'p') {
                const void* ptr = vm->valueToPointer(arg);
                if (ptr == nullptr) {
                    std::string null_fmt = sub_fmt;
                    size_t p_pos = null_fmt.rfind('p');
                    if (p_pos != std::string::npos) {
                        null_fmt[p_pos] = 's';
                    }
                    snprintf(buf, sizeof(buf), null_fmt.c_str(), "(null)");
                } else {
                    snprintf(buf, sizeof(buf), sub_fmt.c_str(), ptr);
                }
                result += buf;
            } else {
                result += sub_fmt;
            }
            argIndex++;
        } else {
            result += fmt[i];
        }
    }
    
    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(Value::runtimeString(vm->internString(result)));
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

// Helpers for string.pack/unpack/packsize
static const size_t MAX_PACK_SIZE = 0x7FFFFFFFFFFFFFFFULL;

static void swap_bytes(char* data, size_t size) {
    for (size_t i = 0; i < size / 2; i++) {
        std::swap(data[i], data[size - 1 - i]);
    }
}

static void apply_pack_alignment(size_t& offset, size_t align, std::string* result = nullptr) {
    if (align > 1) {
        size_t padding = (align - (offset % align)) % align;
        if (result) {
            result->append(padding, '\0');
        }
        offset += padding;
    }
}

static int64_t parse_format_num(const std::string& fmt, size_t& i) {
    if (i + 1 >= fmt.length() || !isdigit(static_cast<unsigned char>(fmt[i + 1]))) {
        return -2;
    }
    i++;
    uint64_t val = 0;
    bool overflow = false;
    while (i < fmt.length() && isdigit(static_cast<unsigned char>(fmt[i]))) {
        int d = fmt[i] - '0';
        if (val > (0x7FFFFFFFFFFFFFFFULL - d) / 10) {
            overflow = true;
        }
        val = val * 10 + d;
        i++;
    }
    i--;
    if (overflow) return -1;
    return static_cast<int64_t>(val);
}

static bool pack_integer(VM* vm, std::string& result, int64_t val, int size, bool issigned, bool littleEndian) {
    if (size < 8) {
        if (issigned) {
            int64_t lim = 1LL << (8 * size - 1);
            if (val < -lim || val >= lim) {
                vm->runtimeError(std::to_string(size) + "-byte integer overflow");
                return false;
            }
        } else {
            if (val < 0) {
                vm->runtimeError(std::to_string(size) + "-byte integer overflow");
                return false;
            }
            uint64_t ulim = (size == 8) ? UINT64_MAX : ((1ULL << (8 * size)) - 1);
            if (static_cast<uint64_t>(val) > ulim) {
                vm->runtimeError(std::to_string(size) + "-byte integer overflow");
                return false;
            }
        }
    }

    uint8_t buf[16];
    uint64_t uval = static_cast<uint64_t>(val);
    uint8_t fill = (issigned && val < 0) ? 0xFF : 0x00;

    if (littleEndian) {
        for (int k = 0; k < size; k++) {
            if (k < 8) {
                buf[k] = static_cast<uint8_t>((uval >> (8 * k)) & 0xFF);
            } else {
                buf[k] = fill;
            }
        }
    } else {
        for (int k = 0; k < size; k++) {
            int src_idx = size - 1 - k;
            if (src_idx < 8) {
                buf[k] = static_cast<uint8_t>((uval >> (8 * src_idx)) & 0xFF);
            } else {
                buf[k] = fill;
            }
        }
    }
    result.append(reinterpret_cast<char*>(buf), size);
    return true;
}

static bool unpack_integer(VM* vm, const std::string& data, size_t current, int size, bool issigned, bool littleEndian, int64_t& outVal) {
    if (current + size > data.length()) {
        vm->runtimeError("data string too short");
        return false;
    }

    const uint8_t* p = reinterpret_cast<const uint8_t*>(&data[current]);
    uint64_t uval = 0;

    if (size <= 8) {
        if (littleEndian) {
            for (int k = 0; k < size; k++) {
                uval |= (static_cast<uint64_t>(p[k]) << (8 * k));
            }
        } else {
            for (int k = 0; k < size; k++) {
                uval = (uval << 8) | p[k];
            }
        }
        if (issigned) {
            if (size < 8) {
                if (uval & (1ULL << (8 * size - 1))) {
                    uval |= (~0ULL << (8 * size));
                }
            }
            outVal = static_cast<int64_t>(uval);
        } else {
            outVal = static_cast<int64_t>(uval);
        }
    } else {
        uint8_t extra[8];
        int extra_len = size - 8;
        if (littleEndian) {
            for (int k = 0; k < 8; k++) {
                uval |= (static_cast<uint64_t>(p[k]) << (8 * k));
            }
            for (int k = 0; k < extra_len; k++) {
                extra[k] = p[8 + k];
            }
        } else {
            for (int k = 0; k < extra_len; k++) {
                extra[k] = p[k];
            }
            for (int k = 0; k < 8; k++) {
                uval = (uval << 8) | p[extra_len + k];
            }
        }

        if (issigned) {
            bool is_neg = (uval & (1ULL << 63)) != 0;
            uint8_t expected = is_neg ? 0xFF : 0x00;
            for (int k = 0; k < extra_len; k++) {
                if (extra[k] != expected) {
                    vm->runtimeError(std::to_string(size) + "-byte integer does not fit into Lua Integer");
                    return false;
                }
            }
            outVal = static_cast<int64_t>(uval);
        } else {
            for (int k = 0; k < extra_len; k++) {
                if (extra[k] != 0x00) {
                    vm->runtimeError(std::to_string(size) + "-byte integer does not fit into Lua Integer");
                    return false;
                }
            }
            outVal = static_cast<int64_t>(uval);
        }
    }
    return true;
}

bool native_string_packsize(VM* vm, int argCount) {
    if (argCount < 1) { vm->runtimeError("string.packsize expects format"); return false; }
    std::string fmt = vm->getStringValue(vm->peek(argCount - 1));

    size_t total = 0;
    size_t maxalign = 1;

    for (size_t i = 0; i < fmt.length(); i++) {
        char spec = fmt[i];
        if (spec == ' ' || spec == '<' || spec == '>' || spec == '=') continue;

        if (spec == '!') {
            int64_t num = parse_format_num(fmt, i);
            if (num == -2) {
                maxalign = sizeof(void*);
            } else if (num < 1 || num > 16) {
                vm->runtimeError("integral size (" + std::to_string(num) + ") out of limits [1,16]");
                return false;
            } else if ((num & (num - 1)) != 0) {
                vm->runtimeError("alignment " + std::to_string(num) + " is not power of 2");
                return false;
            } else {
                maxalign = static_cast<size_t>(num);
            }
            continue;
        }

        if (spec == 'X') {
            if (i + 1 >= fmt.length()) {
                vm->runtimeError("invalid next option for option 'X'");
                return false;
            }
            char nextSpec = fmt[i + 1];
            if (nextSpec == ' ' || nextSpec == 'X' || nextSpec == '<' || nextSpec == '>' ||
                nextSpec == '=' || nextSpec == '!' || nextSpec == 'c' || nextSpec == 's' || nextSpec == 'z') {
                vm->runtimeError("invalid next option for option 'X'");
                return false;
            }
            i++;
            size_t xsize = 0;
            switch (nextSpec) {
                case 'b': case 'B': xsize = 1; break;
                case 'h': case 'H': xsize = 2; break;
                case 'l': case 'L': xsize = sizeof(long); break;
                case 'j': case 'J': xsize = sizeof(int64_t); break;
                case 'T': xsize = sizeof(size_t); break;
                case 'f': xsize = sizeof(float); break;
                case 'd': case 'n': xsize = sizeof(double); break;
                case 'i': case 'I': {
                    int64_t num = parse_format_num(fmt, i);
                    if (num == -2) {
                        xsize = sizeof(int);
                    } else if (num < 1 || num > 16) {
                        vm->runtimeError("integral size (" + std::to_string(num) + ") out of limits [1,16]");
                        return false;
                    } else {
                        xsize = static_cast<size_t>(num);
                    }
                    break;
                }
                default:
                    vm->runtimeError("invalid next option for option 'X'");
                    return false;
            }
            size_t align = (xsize <= maxalign) ? xsize : maxalign;
            if ((align & (align - 1)) != 0) {
                vm->runtimeError("format asks for alignment not power of 2");
                return false;
            }
            apply_pack_alignment(total, align);
            continue;
        }

        if (spec == 'x') {
            if (total > MAX_PACK_SIZE - 1) {
                vm->runtimeError("format result too large");
                return false;
            }
            total += 1;
            continue;
        }

        if (spec == 's' || spec == 'z') {
            vm->runtimeError("variable-length format in string.packsize");
            return false;
        }

        size_t specSize = 0;
        size_t itemAlign = 1;
        if (spec == 'c') {
            int64_t num = parse_format_num(fmt, i);
            if (num == -2) {
                vm->runtimeError("missing size for format 'c'");
                return false;
            } else if (num < 0) {
                vm->runtimeError("invalid format (width or height out of limits)");
                return false;
            }
            specSize = static_cast<size_t>(num);
            itemAlign = 1;
        } else if (spec == 'b' || spec == 'B') {
            specSize = 1;
            itemAlign = 1;
        } else if (spec == 'h' || spec == 'H') {
            specSize = 2;
            itemAlign = (specSize <= maxalign) ? specSize : maxalign;
        } else if (spec == 'l' || spec == 'L') {
            specSize = sizeof(long);
            itemAlign = (specSize <= maxalign) ? specSize : maxalign;
        } else if (spec == 'j' || spec == 'J') {
            specSize = sizeof(int64_t);
            itemAlign = (specSize <= maxalign) ? specSize : maxalign;
        } else if (spec == 'T') {
            specSize = sizeof(size_t);
            itemAlign = (specSize <= maxalign) ? specSize : maxalign;
        } else if (spec == 'f') {
            specSize = sizeof(float);
            itemAlign = (specSize <= maxalign) ? specSize : maxalign;
        } else if (spec == 'd' || spec == 'n') {
            specSize = sizeof(double);
            itemAlign = (specSize <= maxalign) ? specSize : maxalign;
        } else if (spec == 'i' || spec == 'I') {
            int64_t num = parse_format_num(fmt, i);
            if (num == -2) {
                specSize = sizeof(int);
            } else if (num < 1 || num > 16) {
                vm->runtimeError("integral size (" + std::to_string(num) + ") out of limits [1,16]");
                return false;
            } else {
                specSize = static_cast<size_t>(num);
            }
            itemAlign = (specSize <= maxalign) ? specSize : maxalign;
        } else {
            vm->runtimeError("invalid format option '" + std::string(1, spec) + "'");
            return false;
        }

        if ((itemAlign & (itemAlign - 1)) != 0) {
            vm->runtimeError("format asks for alignment not power of 2");
            return false;
        }

        apply_pack_alignment(total, itemAlign);
        if (total > MAX_PACK_SIZE - specSize) {
            vm->runtimeError("format result too large");
            return false;
        }
        total += specSize;
    }

    for (int k = 0; k < argCount; k++) vm->pop();
    vm->push(vm->makeInteger(static_cast<int64_t>(total)));
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_string_pack(VM* vm, int argCount) {
    if (argCount < 1) { vm->runtimeError("string.pack expects format"); return false; }
    std::string fmt = vm->getStringValue(vm->peek(argCount - 1));
    std::string result;
    size_t maxalign = 1;
    bool littleEndian = true;
    int argIdx = 1;

    for (size_t i = 0; i < fmt.length(); i++) {
        char spec = fmt[i];
        if (spec == ' ') continue;
        if (spec == '<') { littleEndian = true; continue; }
        if (spec == '>') { littleEndian = false; continue; }
        if (spec == '=') { littleEndian = true; continue; }

        if (spec == '!') {
            int64_t num = parse_format_num(fmt, i);
            if (num == -2) {
                maxalign = sizeof(void*);
            } else if (num < 1 || num > 16) {
                vm->runtimeError("integral size (" + std::to_string(num) + ") out of limits [1,16]");
                return false;
            } else if ((num & (num - 1)) != 0) {
                vm->runtimeError("alignment " + std::to_string(num) + " is not power of 2");
                return false;
            } else {
                maxalign = static_cast<size_t>(num);
            }
            continue;
        }

        if (spec == 'X') {
            if (i + 1 >= fmt.length()) {
                vm->runtimeError("invalid next option for option 'X'");
                return false;
            }
            char nextSpec = fmt[i + 1];
            if (nextSpec == ' ' || nextSpec == 'X' || nextSpec == '<' || nextSpec == '>' ||
                nextSpec == '=' || nextSpec == '!' || nextSpec == 'c' || nextSpec == 's' || nextSpec == 'z') {
                vm->runtimeError("invalid next option for option 'X'");
                return false;
            }
            i++;
            size_t xsize = 0;
            switch (nextSpec) {
                case 'b': case 'B': xsize = 1; break;
                case 'h': case 'H': xsize = 2; break;
                case 'l': case 'L': xsize = sizeof(long); break;
                case 'j': case 'J': xsize = sizeof(int64_t); break;
                case 'T': xsize = sizeof(size_t); break;
                case 'f': xsize = sizeof(float); break;
                case 'd': case 'n': xsize = sizeof(double); break;
                case 'i': case 'I': {
                    int64_t num = parse_format_num(fmt, i);
                    if (num == -2) {
                        xsize = sizeof(int);
                    } else if (num < 1 || num > 16) {
                        vm->runtimeError("integral size (" + std::to_string(num) + ") out of limits [1,16]");
                        return false;
                    } else {
                        xsize = static_cast<size_t>(num);
                    }
                    break;
                }
                default:
                    vm->runtimeError("invalid next option for option 'X'");
                    return false;
            }
            size_t align = (xsize <= maxalign) ? xsize : maxalign;
            if ((align & (align - 1)) != 0) {
                vm->runtimeError("format asks for alignment not power of 2");
                return false;
            }
            size_t offset = result.length();
            apply_pack_alignment(offset, align, &result);
            continue;
        }

        if (spec == 'x') {
            if (result.length() > MAX_PACK_SIZE - 1) {
                vm->runtimeError("resulting string too long");
                return false;
            }
            result.push_back('\0');
            continue;
        }

        if (spec == 'c') {
            int64_t num = parse_format_num(fmt, i);
            if (num == -2) {
                vm->runtimeError("missing size for format 'c'");
                return false;
            } else if (num < 0) {
                vm->runtimeError("invalid format (width or height out of limits)");
                return false;
            }
            size_t csize = static_cast<size_t>(num);
            if (result.length() > MAX_PACK_SIZE - csize) {
                vm->runtimeError("resulting string too long");
                return false;
            }
            if (argIdx >= argCount) {
                vm->runtimeError("bad argument to 'pack' (no value)");
                return false;
            }
            Value val = vm->peek(argCount - 1 - argIdx);
            argIdx++;

            std::string s = vm->getStringValue(val);
            if (s.length() > csize) {
                vm->runtimeError("string longer than given size");
                return false;
            }
            result.append(s);
            if (s.length() < csize) {
                result.append(csize - s.length(), '\0');
            }
            continue;
        }

        if (spec == 's') {
            int64_t num = parse_format_num(fmt, i);
            size_t lensize = 0;
            if (num == -2) {
                lensize = sizeof(size_t);
            } else if (num < 1 || num > 16) {
                vm->runtimeError("integral size (" + std::to_string(num) + ") out of limits [1,16]");
                return false;
            } else {
                lensize = static_cast<size_t>(num);
            }
            size_t align = (lensize <= maxalign) ? lensize : maxalign;
            if ((align & (align - 1)) != 0) {
                vm->runtimeError("format asks for alignment not power of 2");
                return false;
            }
            size_t offset = result.length();
            apply_pack_alignment(offset, align, &result);

            if (argIdx >= argCount) {
                vm->runtimeError("bad argument to 'pack' (no value)");
                return false;
            }
            Value val = vm->peek(argCount - 1 - argIdx);
            argIdx++;

            std::string s = vm->getStringValue(val);
            size_t slen = s.length();
            if (lensize < 8 && slen >= (1ULL << (8 * lensize))) {
                vm->runtimeError("string length does not fit in given size");
                return false;
            }
            if (!pack_integer(vm, result, static_cast<int64_t>(slen), static_cast<int>(lensize), false, littleEndian)) {
                return false;
            }
            result.append(s);
            continue;
        }

        if (spec == 'z') {
            if (argIdx >= argCount) {
                vm->runtimeError("bad argument to 'pack' (no value)");
                return false;
            }
            Value val = vm->peek(argCount - 1 - argIdx);
            argIdx++;

            std::string s = vm->getStringValue(val);
            if (s.find('\0') != std::string::npos) {
                vm->runtimeError("string contains zeros");
                return false;
            }
            result.append(s);
            result.push_back('\0');
            continue;
        }

        // Numeric formats: b, B, h, H, l, L, j, J, T, i, I, f, d, n
        size_t specSize = 0;
        bool isInt = true;
        bool isSigned = true;
        if (spec == 'b') { specSize = 1; isSigned = true; }
        else if (spec == 'B') { specSize = 1; isSigned = false; }
        else if (spec == 'h') { specSize = 2; isSigned = true; }
        else if (spec == 'H') { specSize = 2; isSigned = false; }
        else if (spec == 'l') { specSize = sizeof(long); isSigned = true; }
        else if (spec == 'L') { specSize = sizeof(long); isSigned = false; }
        else if (spec == 'j') { specSize = sizeof(int64_t); isSigned = true; }
        else if (spec == 'J') { specSize = sizeof(uint64_t); isSigned = false; }
        else if (spec == 'T') { specSize = sizeof(size_t); isSigned = false; }
        else if (spec == 'i' || spec == 'I') {
            isSigned = (spec == 'i');
            int64_t num = parse_format_num(fmt, i);
            if (num == -2) {
                specSize = sizeof(int);
            } else if (num < 1 || num > 16) {
                vm->runtimeError("integral size (" + std::to_string(num) + ") out of limits [1,16]");
                return false;
            } else {
                specSize = static_cast<size_t>(num);
            }
        } else if (spec == 'f') {
            specSize = sizeof(float);
            isInt = false;
        } else if (spec == 'd' || spec == 'n') {
            specSize = sizeof(double);
            isInt = false;
        } else {
            vm->runtimeError("invalid format option '" + std::string(1, spec) + "'");
            return false;
        }

        size_t itemAlign = (specSize <= maxalign) ? specSize : maxalign;
        if ((itemAlign & (itemAlign - 1)) != 0) {
            vm->runtimeError("format asks for alignment not power of 2");
            return false;
        }
        size_t offset = result.length();
        apply_pack_alignment(offset, itemAlign, &result);

        if (argIdx >= argCount) {
            vm->runtimeError("bad argument to 'pack' (no value)");
            return false;
        }
        Value val = vm->peek(argCount - 1 - argIdx);
        argIdx++;

        if (isInt) {
            int64_t ival = 0;
            if (val.isInteger()) {
                ival = val.asInteger();
            } else if (val.isNumber()) {
                double d = val.asNumber();
                ival = static_cast<int64_t>(d);
                if (static_cast<double>(ival) != d) {
                    vm->runtimeError("number has no integer representation");
                    return false;
                }
            } else {
                vm->runtimeError("bad argument to 'pack' (number expected)");
                return false;
            }
            if (!pack_integer(vm, result, ival, static_cast<int>(specSize), isSigned, littleEndian)) {
                return false;
            }
        } else {
            if (!val.isNumber()) {
                vm->runtimeError("bad argument to 'pack' (number expected)");
                return false;
            }
            double d = val.asNumber();
            if (spec == 'f') {
                float f = static_cast<float>(d);
                char buf[4];
                std::memcpy(buf, &f, 4);
                if (!littleEndian) swap_bytes(buf, 4);
                result.append(buf, 4);
            } else {
                char buf[8];
                std::memcpy(buf, &d, 8);
                if (!littleEndian) swap_bytes(buf, 8);
                result.append(buf, 8);
            }
        }
    }

    for (int k = 0; k < argCount; k++) vm->pop();
    vm->push(Value::runtimeString(vm->internString(result)));
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_string_unpack(VM* vm, int argCount) {
    if (argCount < 2) { vm->runtimeError("string.unpack expects format and string"); return false; }
    std::string fmt = vm->getStringValue(vm->peek(argCount - 1));
    std::string data = vm->getStringValue(vm->peek(argCount - 2));
    int64_t pos = (argCount >= 3) ? vm->peek(argCount - 3).asInteger() : 1;

    if (pos < 0) {
        pos = static_cast<int64_t>(data.length()) + pos + 1;
    }

    if (pos < 1 || pos > static_cast<int64_t>(data.length()) + 1) {
        vm->runtimeError("initial position out of string");
        return false;
    }

    size_t current = static_cast<size_t>(pos - 1);
    size_t maxalign = 1;
    bool littleEndian = true;
    std::vector<Value> results;

    for (size_t i = 0; i < fmt.length(); i++) {
        char spec = fmt[i];
        if (spec == ' ') continue;
        if (spec == '<') { littleEndian = true; continue; }
        if (spec == '>') { littleEndian = false; continue; }
        if (spec == '=') { littleEndian = true; continue; }

        if (spec == '!') {
            int64_t num = parse_format_num(fmt, i);
            if (num == -2) {
                maxalign = sizeof(void*);
            } else if (num < 1 || num > 16) {
                vm->runtimeError("integral size (" + std::to_string(num) + ") out of limits [1,16]");
                return false;
            } else if ((num & (num - 1)) != 0) {
                vm->runtimeError("alignment " + std::to_string(num) + " is not power of 2");
                return false;
            } else {
                maxalign = static_cast<size_t>(num);
            }
            continue;
        }

        if (spec == 'X') {
            if (i + 1 >= fmt.length()) {
                vm->runtimeError("invalid next option for option 'X'");
                return false;
            }
            char nextSpec = fmt[i + 1];
            if (nextSpec == ' ' || nextSpec == 'X' || nextSpec == '<' || nextSpec == '>' ||
                nextSpec == '=' || nextSpec == '!' || nextSpec == 'c' || nextSpec == 's' || nextSpec == 'z') {
                vm->runtimeError("invalid next option for option 'X'");
                return false;
            }
            i++;
            size_t xsize = 0;
            switch (nextSpec) {
                case 'b': case 'B': xsize = 1; break;
                case 'h': case 'H': xsize = 2; break;
                case 'l': case 'L': xsize = sizeof(long); break;
                case 'j': case 'J': xsize = sizeof(int64_t); break;
                case 'T': xsize = sizeof(size_t); break;
                case 'f': xsize = sizeof(float); break;
                case 'd': case 'n': xsize = sizeof(double); break;
                case 'i': case 'I': {
                    int64_t num = parse_format_num(fmt, i);
                    if (num == -2) {
                        xsize = sizeof(int);
                    } else if (num < 1 || num > 16) {
                        vm->runtimeError("integral size (" + std::to_string(num) + ") out of limits [1,16]");
                        return false;
                    } else {
                        xsize = static_cast<size_t>(num);
                    }
                    break;
                }
                default:
                    vm->runtimeError("invalid next option for option 'X'");
                    return false;
            }
            size_t align = (xsize <= maxalign) ? xsize : maxalign;
            if ((align & (align - 1)) != 0) {
                vm->runtimeError("format asks for alignment not power of 2");
                return false;
            }
            apply_pack_alignment(current, align);
            continue;
        }

        if (spec == 'x') {
            if (current + 1 > data.length()) {
                vm->runtimeError("data string too short");
                return false;
            }
            current += 1;
            continue;
        }

        if (spec == 'c') {
            int64_t num = parse_format_num(fmt, i);
            if (num == -2) {
                vm->runtimeError("missing size for format 'c'");
                return false;
            } else if (num < 0) {
                vm->runtimeError("invalid format (width or height out of limits)");
                return false;
            }
            size_t csize = static_cast<size_t>(num);
            if (current + csize > data.length()) {
                vm->runtimeError("data string too short");
                return false;
            }
            std::string s = data.substr(current, csize);
            results.push_back(Value::runtimeString(vm->internString(s)));
            current += csize;
            continue;
        }

        if (spec == 's') {
            int64_t num = parse_format_num(fmt, i);
            size_t lensize = 0;
            if (num == -2) {
                lensize = sizeof(size_t);
            } else if (num < 1 || num > 16) {
                vm->runtimeError("integral size (" + std::to_string(num) + ") out of limits [1,16]");
                return false;
            } else {
                lensize = static_cast<size_t>(num);
            }
            size_t align = (lensize <= maxalign) ? lensize : maxalign;
            if ((align & (align - 1)) != 0) {
                vm->runtimeError("format asks for alignment not power of 2");
                return false;
            }
            apply_pack_alignment(current, align);

            int64_t slen = 0;
            if (!unpack_integer(vm, data, current, static_cast<int>(lensize), false, littleEndian, slen)) {
                return false;
            }
            current += lensize;
            if (slen < 0 || current + static_cast<size_t>(slen) > data.length()) {
                vm->runtimeError("data string too short");
                return false;
            }
            std::string s = data.substr(current, static_cast<size_t>(slen));
            results.push_back(Value::runtimeString(vm->internString(s)));
            current += static_cast<size_t>(slen);
            continue;
        }

        if (spec == 'z') {
            size_t null_pos = data.find('\0', current);
            if (null_pos == std::string::npos) {
                vm->runtimeError("unfinished string for format 'z'");
                return false;
            }
            std::string s = data.substr(current, null_pos - current);
            results.push_back(Value::runtimeString(vm->internString(s)));
            current = null_pos + 1;
            continue;
        }

        // Numeric formats
        size_t specSize = 0;
        bool isInt = true;
        bool isSigned = true;
        if (spec == 'b') { specSize = 1; isSigned = true; }
        else if (spec == 'B') { specSize = 1; isSigned = false; }
        else if (spec == 'h') { specSize = 2; isSigned = true; }
        else if (spec == 'H') { specSize = 2; isSigned = false; }
        else if (spec == 'l') { specSize = sizeof(long); isSigned = true; }
        else if (spec == 'L') { specSize = sizeof(long); isSigned = false; }
        else if (spec == 'j') { specSize = sizeof(int64_t); isSigned = true; }
        else if (spec == 'J') { specSize = sizeof(uint64_t); isSigned = false; }
        else if (spec == 'T') { specSize = sizeof(size_t); isSigned = false; }
        else if (spec == 'i' || spec == 'I') {
            isSigned = (spec == 'i');
            int64_t num = parse_format_num(fmt, i);
            if (num == -2) {
                specSize = sizeof(int);
            } else if (num < 1 || num > 16) {
                vm->runtimeError("integral size (" + std::to_string(num) + ") out of limits [1,16]");
                return false;
            } else {
                specSize = static_cast<size_t>(num);
            }
        } else if (spec == 'f') {
            specSize = sizeof(float);
            isInt = false;
        } else if (spec == 'd' || spec == 'n') {
            specSize = sizeof(double);
            isInt = false;
        } else {
            vm->runtimeError("invalid format option '" + std::string(1, spec) + "'");
            return false;
        }

        size_t itemAlign = (specSize <= maxalign) ? specSize : maxalign;
        if ((itemAlign & (itemAlign - 1)) != 0) {
            vm->runtimeError("format asks for alignment not power of 2");
            return false;
        }
        apply_pack_alignment(current, itemAlign);

        if (isInt) {
            int64_t ival = 0;
            if (!unpack_integer(vm, data, current, static_cast<int>(specSize), isSigned, littleEndian, ival)) {
                return false;
            }
            results.push_back(vm->makeInteger(ival));
            current += specSize;
        } else {
            if (current + specSize > data.length()) {
                vm->runtimeError("data string too short");
                return false;
            }
            if (spec == 'f') {
                char buf[4];
                std::memcpy(buf, &data[current], 4);
                if (!littleEndian) swap_bytes(buf, 4);
                float f;
                std::memcpy(&f, buf, 4);
                results.push_back(Value::number(static_cast<double>(f)));
            } else {
                char buf[8];
                std::memcpy(buf, &data[current], 8);
                if (!littleEndian) swap_bytes(buf, 8);
                double d;
                std::memcpy(&d, buf, 8);
                results.push_back(Value::number(d));
            }
            current += specSize;
        }
    }

    results.push_back(vm->makeInteger(static_cast<int64_t>(current + 1)));

    for (int k = 0; k < argCount; k++) vm->pop();
    for (const auto& val : results) {
        vm->push(val);
    }
    vm->currentCoroutine()->lastResultCount = static_cast<int>(results.size());
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
    function->serialize(os);

    std::string bytecode = os.str();

    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(Value::runtimeString(vm->internString(bytecode)));
    return true;
}

bool native_string_rep(VM* vm, int argCount) {
    if (argCount < 2) { vm->runtimeError("string.rep expects at least 2 arguments"); return false; }
    std::string s = vm->getStringValue(vm->peek(argCount - 1));
    int64_t n;
    if (!vm->toInteger(vm->peek(argCount - 2), n)) {
        vm->runtimeError("bad argument #2 to 'rep' (number has no integer representation)");
        return false;
    }
    std::string sep = (argCount >= 3) ? vm->getStringValue(vm->peek(argCount - 3)) : "";

    if (n <= 0) {
        for (int i = 0; i < argCount; i++) vm->pop();
        vm->push(Value::runtimeString(vm->internString("")));
        return true;
    }

    size_t l = s.length();
    size_t lsep = sep.length();
    if (l + lsep == 0) {
        for (int i = 0; i < argCount; i++) vm->pop();
        vm->push(Value::runtimeString(vm->internString("")));
        return true;
    }

    constexpr size_t MAX_SIZE = (size_t)(~((size_t)0)) / 2;
    if (l + lsep < l || (size_t)n > (MAX_SIZE - lsep) / (l + lsep)) {
        vm->runtimeError("resulting string too large");
        return false;
    }

    size_t totalLen = (l * n) + (lsep * (n - 1));
    std::string result;
    try {
        result.reserve(totalLen);
    } catch (const std::exception&) {
        vm->runtimeError("resulting string too large");
        return false;
    }

    for (int64_t i = 0; i < n; i++) {
        result += s;
        if (i < n - 1) result += sep;
    }

    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(Value::runtimeString(vm->internString(result)));
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

} // anonymous namespace

void registerStringLibrary(VM* vm, TableObject* stringTable) {
    TableObject* stringMt = vm->createTable();
    Value stringTableVal = Value::table(stringTable);
    stringMt->set("__index", stringTableVal);
    vm->setRegistry("string_table", stringTableVal);
    vm->setTypeMetatable(Value::Type::STRING, Value::table(stringMt));

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
    vm->addNativeToTable(stringTable, "rep", native_string_rep);
    vm->addNativeToTable(stringTable, "packsize", native_string_packsize);
    vm->addNativeToTable(stringTable, "pack", native_string_pack);
    vm->addNativeToTable(stringTable, "unpack", native_string_unpack);
    vm->addNativeToTable(stringTable, "dump", native_string_dump);
}
