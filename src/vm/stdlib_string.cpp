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

    FunctionObject* func = vm->compileSource(script, "gmatch_factory");
    if (!func) return false;
    
    vm->push(Value::closure(vm->createClosure(func)));
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
        } else { if (s1 < ms.src_end) result.push_back(*s1); s1++; }
    }
    if (s1 < ms.src_end) result.append(s1, ms.src_end - s1);
    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(Value::runtimeString(vm->internString(result)));
    vm->push(Value::number(count));
    vm->currentCoroutine()->lastResultCount = 2;
    return true;
}

bool native_string_format(VM* vm, int argCount) {
    if (argCount < 1) return false;
    std::string fmt = vm->getStringValue(vm->peek(argCount - 1));
    // Very basic format stub
    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(Value::runtimeString(vm->internString(fmt)));
    return true;
}

bool native_string_packsize(VM* vm, int argCount) {
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
    vm->addNativeToTable(stringTable, "dump", native_string_dump);
}
