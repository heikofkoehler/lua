#include "api/lua.h"
#include "api/lauxlib.h"
#include "api/lualib.h"
#include "api/lua_state.h"
#include "vm/vm.hpp"
#include "value/value.hpp"
#include "value/table.hpp"
#include "value/closure.hpp"
#include "value/userdata.hpp"
#include "value/file.hpp"
#include "compiler/lexer.hpp"
#include "compiler/parser.hpp"
#include "compiler/codegen.hpp"
#include <cstring>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdlib>
#include <fstream>
#include <iostream>

// State manipulation
lua_State *lua_newstate(void) {
    lua_State* L = new lua_State;
    L->vm = new VM();
    L->is_owned = true;
    L->stackBase = 0;
    L->argCount = 0;
    L->currentClosure = nullptr;
    L->registryVal = Value::nil();
    return L;
}

void lua_close(lua_State *L) {
    if (L->is_owned) {
        delete L->vm;
    }
    delete L;
}

static bool double_to_integer(double d, int64_t& out) {
    double intPart;
    if (std::isfinite(d) && std::modf(d, &intPart) == 0.0 &&
        d >= -9223372036854775808.0 && d < 9223372036854775808.0) {
        out = static_cast<int64_t>(d);
        return true;
    }
    return false;
}

static int to_abs_idx(lua_State *L, int idx) {
    if (idx > 0 || idx <= LUA_REGISTRYINDEX) return idx;
    return lua_gettop(L) + idx + 1;
}

int lua_absindex(lua_State *L, int idx) {
    return to_abs_idx(L, idx);
}

static Value* get_val(lua_State *L, int idx) {
    if (idx == LUA_REGISTRYINDEX) {
        L->registryVal = Value::table(L->vm->registryTable());
        return &L->registryVal;
    }
    
    if (idx < LUA_REGISTRYINDEX) {
        int upvalIndex = LUA_REGISTRYINDEX - idx; // 1-based: 1, 2, ...
        if (L->currentClosure && L->currentClosure->isC()) {
            return const_cast<Value*>(L->currentClosure->getCUpvaluePtr(static_cast<size_t>(upvalIndex - 1)));
        }
        return nullptr;
    }

    auto& stack = L->vm->currentCoroutine()->stack;
    size_t abs_idx;
    
    if (idx > 0) {
        // Positive index: relative to stackBase
        abs_idx = L->stackBase + static_cast<size_t>(idx - 1);
    } else if (idx < 0) {
        // Negative index: relative to current top
        abs_idx = stack.size() + static_cast<size_t>(idx);
    } else {
        return nullptr;
    }

    if (abs_idx < stack.size() && abs_idx >= L->stackBase) {
        return &stack[abs_idx];
    }
    return nullptr;
}

// Basic stack manipulation
int lua_gettop(lua_State *L) {
    return static_cast<int>(L->vm->currentCoroutine()->stack.size() - L->stackBase);
}

void lua_settop(lua_State *L, int idx) {
    auto& stack = L->vm->currentCoroutine()->stack;
    if (idx >= 0) {
        size_t target = L->stackBase + static_cast<size_t>(idx);
        while (stack.size() > target) stack.pop_back();
        while (stack.size() < target) stack.push_back(Value::nil());
    } else {
        size_t target = stack.size() + static_cast<size_t>(idx + 1);
        if (target < L->stackBase) target = L->stackBase;
        while (stack.size() > target) stack.pop_back();
    }
}

void lua_pushvalue(lua_State *L, int idx) {
    Value* v = get_val(L, idx);
    if (v) {
        L->vm->push(*v);
    } else {
        L->vm->push(Value::nil());
    }
}

void lua_rotate(lua_State *L, int idx, int n) {
    auto& stack = L->vm->currentCoroutine()->stack;
    int abs_idx = to_abs_idx(L, idx);
    if (abs_idx <= 0) return;
    size_t first = L->stackBase + static_cast<size_t>(abs_idx - 1);
    size_t last = stack.size();
    if (first >= last) return;
    
    int count = static_cast<int>(last - first);
    int m = (n % count + count) % count;
    if (m == 0) return;
    
    std::rotate(stack.begin() + first, stack.begin() + (last - m), stack.begin() + last);
}

void lua_copy(lua_State *L, int fromidx, int toidx) {
    Value* from = get_val(L, fromidx);
    Value* to = get_val(L, toidx);
    if (from && to) {
        *to = *from;
    }
}

int lua_checkstack(lua_State *L, int n) {
    (void)L; (void)n;
    return 1;
}

// Push functions
void lua_pushnil(lua_State *L) {
    L->vm->push(Value::nil());
}

void lua_pushnumber(lua_State *L, lua_Number n) {
    L->vm->push(Value::number(n));
}

void lua_pushinteger(lua_State *L, lua_Integer n) {
    L->vm->push(Value::integer(n));
}

const char *lua_pushlstring(lua_State *L, const char *s, size_t len) {
    std::string str(s ? s : "", len);
    L->vm->push(Value::runtimeString(L->vm->internString(str)));
    return lua_tostring(L, -1);
}

const char *lua_pushstring(lua_State *L, const char *s) {
    if (s) {
        L->vm->push(Value::runtimeString(L->vm->internString(s)));
        return lua_tostring(L, -1);
    } else {
        L->vm->push(Value::nil());
        return nullptr;
    }
}

const char *lua_pushvfstring(lua_State *L, const char *fmt, va_list argp) {
    char buf[2048];
    vsnprintf(buf, sizeof(buf), fmt, argp);
    return lua_pushstring(L, buf);
}

const char *lua_pushfstring(lua_State *L, const char *fmt, ...) {
    va_list argp;
    va_start(argp, fmt);
    const char *res = lua_pushvfstring(L, fmt, argp);
    va_end(argp);
    return res;
}

void lua_pushcclosure(lua_State *L, lua_CFunction fn, int n) {
    std::vector<Value> upvalues;
    if (n > 0) {
        upvalues.resize(n);
        int top = lua_gettop(L);
        int start = top - n + 1;
        for (int i = 0; i < n; i++) {
            Value* v = get_val(L, start + i);
            upvalues[i] = v ? *v : Value::nil();
        }
        lua_pop(L, n);
    }
    ClosureObject* cl = L->vm->createCClosure(fn, upvalues);
    L->vm->push(Value::closure(cl));
}

void lua_pushboolean(lua_State *L, int b) {
    L->vm->push(Value::boolean(b != 0));
}

void lua_pushlightuserdata(lua_State *L, void *p) {
    UserdataObject* ud = L->vm->createUserdata(p, 0, true, false);
    L->vm->push(Value::userdata(ud));
}

int lua_pushthread(lua_State *L) {
    L->vm->push(Value::thread(L->vm->currentCoroutine()));
    return (L->vm->currentCoroutine() == L->vm->mainCoroutine()) ? 1 : 0;
}

const char *lua_pushexternalstring(lua_State *L, const char *s, size_t len, lua_Free falloc, void *ud) {
    (void)falloc; (void)ud;
    return lua_pushlstring(L, s, len);
}

// Type info
int lua_type(lua_State *L, int idx) {
    Value* v = get_val(L, idx);
    if (!v) return LUA_TNONE;
    if (v->isNil()) return LUA_TNIL;
    if (v->isBool()) return LUA_TBOOLEAN;
    if (v->isNumber()) return LUA_TNUMBER;
    if (v->isString()) return LUA_TSTRING;
    if (v->isTable()) return LUA_TTABLE;
    if (v->isFunction() || v->isNativeFunction() || v->isCFunction() || (v->isClosure())) return LUA_TFUNCTION;
    if (v->isThread()) return LUA_TTHREAD;
    if (v->isUserdata()) {
        if (v->asUserdataObj()->isLight()) return LUA_TLIGHTUSERDATA;
        return LUA_TUSERDATA;
    }
    if (v->isFile()) {
        return LUA_TUSERDATA; // In standard Lua, file is a full userdata
    }
    return LUA_TNONE;
}

const char *lua_typename(lua_State *L, int tp) {
    (void)L;
    switch (tp) {
        case LUA_TNONE: return "no value";
        case LUA_TNIL: return "nil";
        case LUA_TBOOLEAN: return "boolean";
        case LUA_TLIGHTUSERDATA: return "userdata";
        case LUA_TNUMBER: return "number";
        case LUA_TSTRING: return "string";
        case LUA_TTABLE: return "table";
        case LUA_TFUNCTION: return "function";
        case LUA_TTHREAD: return "thread";
        case LUA_TUSERDATA: return "userdata";
        default: return "no value";
    }
}

int lua_isnumber(lua_State *L, int idx) {
    Value* v = get_val(L, idx);
    return (v && v->isNumber()) ? 1 : 0;
}

int lua_isstring(lua_State *L, int idx) {
    Value* v = get_val(L, idx);
    return (v && (v->isString() || v->isNumber())) ? 1 : 0;
}

int lua_iscfunction(lua_State *L, int idx) {
    Value* v = get_val(L, idx);
    if (!v) return 0;
    if (v->isCFunction()) return 1;
    if (v->isClosure() && v->asClosureObj()->isC()) return 1;
    if (v->isNativeFunction()) return 1;
    return 0;
}

int lua_isinteger(lua_State *L, int idx) {
    Value* v = get_val(L, idx);
    if (!v) return 0;
    if (v->isInteger()) return 1;
    if (v->isFloat()) {
        double d = v->asNumber();
        int64_t i;
        return double_to_integer(d, i) ? 1 : 0;
    }
    return 0;
}

int lua_isuserdata(lua_State *L, int idx) {
    Value* v = get_val(L, idx);
    if (!v) return 0;
    return (v->isUserdata() || v->isFile()) ? 1 : 0;
}

int lua_rawequal(lua_State *L, int idx1, int idx2) {
    Value* v1 = get_val(L, idx1);
    Value* v2 = get_val(L, idx2);
    if (!v1 || !v2) return 0;
    return (*v1 == *v2) ? 1 : 0;
}

// Get functions
lua_Number lua_tonumberx(lua_State *L, int idx, int *isnum) {
    Value* v = get_val(L, idx);
    if (v && v->isNumber()) {
        if (isnum) *isnum = 1;
        return v->asNumber();
    }
    if (v && v->isString()) {
        double d = 0.0;
        bool isInt = false;
        if (VM::stringToNumber(v->asStringObj()->chars(), d, isInt)) {
            if (isnum) *isnum = 1;
            return d;
        }
    }
    if (isnum) *isnum = 0;
    return 0.0;
}

lua_Integer lua_tointegerx(lua_State *L, int idx, int *isnum) {
    Value* v = get_val(L, idx);
    if (v && v->isInteger()) {
        if (isnum) *isnum = 1;
        return v->asInteger();
    }
    if (v && v->isFloat()) {
        double d = v->asNumber();
        int64_t i;
        if (double_to_integer(d, i)) {
            if (isnum) *isnum = 1;
            return i;
        }
    }
    if (v && v->isString()) {
        double d = 0.0;
        bool isInt = false;
        if (VM::stringToNumber(v->asStringObj()->chars(), d, isInt)) {
            if (isInt) {
                if (isnum) *isnum = 1;
                return static_cast<lua_Integer>(d);
            }
            int64_t i;
            if (double_to_integer(d, i)) {
                if (isnum) *isnum = 1;
                return i;
            }
        }
    }
    if (isnum) *isnum = 0;
    return 0;
}

int lua_toboolean(lua_State *L, int idx) {
    Value* v = get_val(L, idx);
    return (v && !v->isFalsey()) ? 1 : 0;
}

const char *lua_tolstring(lua_State *L, int idx, size_t *len) {
    Value* v = get_val(L, idx);
    if (v && v->isString()) {
        StringObject* s = v->asStringObj();
        if (len) *len = s->length();
        return s->chars();
    }
    if (v && v->isNumber()) {
        // In standard Lua, lua_tolstring converts numbers to strings on the stack
        std::string s = v->toString();
        StringObject* so = L->vm->internString(s);
        *v = Value::runtimeString(so);
        if (len) *len = so->length();
        return so->chars();
    }
    if (len) *len = 0;
    return nullptr;
}

size_t lua_rawlen(lua_State *L, int idx) {
    Value* v = get_val(L, idx);
    if (!v) return 0;
    if (v->isString()) {
        return v->asStringObj()->length();
    }
    if (v->isTable()) {
        return v->asTableObj()->length();
    }
    if (v->isUserdata()) {
        return v->asUserdataObj()->numUserValues() * sizeof(Value);
    }
    return 0;
}

lua_CFunction lua_tocfunction(lua_State *L, int idx) {
    Value* v = get_val(L, idx);
    if (!v) return nullptr;
    if (v->isCFunction()) return reinterpret_cast<lua_CFunction>(v->asCFunction());
    if (v->isClosure() && v->asClosureObj()->isC()) return v->asClosureObj()->cFunc();
    return nullptr;
}

void *lua_touserdata(lua_State *L, int idx) {
    Value* v = get_val(L, idx);
    if (!v) return nullptr;
    if (v->isUserdata()) {
        return v->asUserdataObj()->data();
    }
    if (v->isFile()) {
        return v->asFileObj()->stream();
    }
    return nullptr;
}

lua_State *lua_tothread(lua_State *L, int idx) {
    Value* v = get_val(L, idx);
    if (v && v->isThread()) {
        return L;
    }
    return nullptr;
}

const void *lua_topointer(lua_State *L, int idx) {
    Value* v = get_val(L, idx);
    if (!v) return nullptr;
    if (v->isObj()) return v->asObj();
    if (v->isCFunction()) return v->asCFunction();
    return nullptr;
}

// Global
void lua_getglobal(lua_State *L, const char *name) {
    L->vm->push(L->vm->getGlobal(name));
}

void lua_setglobal(lua_State *L, const char *name) {
    L->vm->setGlobal(name, L->vm->pop());
}

// Tables
void lua_createtable(lua_State *L, int narr, int nrec) {
    (void)narr; (void)nrec;
    L->vm->push(Value::table(L->vm->createTable()));
}

int lua_gettable(lua_State *L, int idx) {
    int abs_idx = to_abs_idx(L, idx);
    Value key = L->vm->pop();
    Value* t_ptr = get_val(L, abs_idx);
    if (!t_ptr || !t_ptr->isTable()) {
        L->vm->push(Value::nil());
        return LUA_TNIL;
    }
    Value val = t_ptr->asTableObj()->get(key);
    L->vm->push(val);
    return lua_type(L, -1);
}

int lua_getfield(lua_State *L, int idx, const char *k) {
    int abs_idx = to_abs_idx(L, idx);
    Value* t_ptr = get_val(L, abs_idx);
    if (!t_ptr || !t_ptr->isTable()) {
        L->vm->push(Value::nil());
        return LUA_TNIL;
    }
    Value val = t_ptr->asTableObj()->get(k);
    L->vm->push(val);
    return lua_type(L, -1);
}

int lua_geti(lua_State *L, int idx, lua_Integer i) {
    int abs_idx = to_abs_idx(L, idx);
    Value* t_ptr = get_val(L, abs_idx);
    if (!t_ptr || !t_ptr->isTable()) {
        L->vm->push(Value::nil());
        return LUA_TNIL;
    }
    Value val = t_ptr->asTableObj()->get(Value::integer(i));
    L->vm->push(val);
    return lua_type(L, -1);
}

void lua_settable(lua_State *L, int idx) {
    int abs_idx = to_abs_idx(L, idx);
    Value val = L->vm->pop();
    Value key = L->vm->pop();
    if (key.isNil()) {
        L->vm->runtimeError("table index is nil");
        return;
    }
    if (key.isFloat() && std::isnan(key.asNumber())) {
        L->vm->runtimeError("table index is NaN");
        return;
    }
    Value* t_ptr = get_val(L, abs_idx);
    if (t_ptr && t_ptr->isTable()) {
        t_ptr->asTableObj()->set(key, val);
    }
}

void lua_setfield(lua_State *L, int idx, const char *k) {
    int abs_idx = to_abs_idx(L, idx);
    Value val = L->vm->pop();
    Value* t_ptr = get_val(L, abs_idx);
    if (t_ptr && t_ptr->isTable()) {
        t_ptr->asTableObj()->set(k, val);
    }
}

void lua_seti(lua_State *L, int idx, lua_Integer i) {
    int abs_idx = to_abs_idx(L, idx);
    Value val = L->vm->pop();
    Value* t_ptr = get_val(L, abs_idx);
    if (t_ptr && t_ptr->isTable()) {
        t_ptr->asTableObj()->set(Value::integer(i), val);
    }
}

int lua_rawget(lua_State *L, int idx) {
    return lua_gettable(L, idx);
}

int lua_rawgeti(lua_State *L, int idx, lua_Integer n) {
    return lua_geti(L, idx, n);
}

int lua_rawgetp(lua_State *L, int idx, const void *p) {
    int abs_idx = to_abs_idx(L, idx);
    Value* t_ptr = get_val(L, abs_idx);
    if (!t_ptr || !t_ptr->isTable()) {
        L->vm->push(Value::nil());
        return LUA_TNIL;
    }
    UserdataObject* ud = L->vm->createUserdata(const_cast<void*>(p), 0, true, false);
    Value val = t_ptr->asTableObj()->get(Value::userdata(ud));
    L->vm->push(val);
    return lua_type(L, -1);
}

void lua_rawset(lua_State *L, int idx) {
    lua_settable(L, idx);
}

void lua_rawseti(lua_State *L, int idx, lua_Integer n) {
    lua_seti(L, idx, n);
}

void lua_rawsetp(lua_State *L, int idx, const void *p) {
    int abs_idx = to_abs_idx(L, idx);
    Value val = L->vm->pop();
    Value* t_ptr = get_val(L, abs_idx);
    if (t_ptr && t_ptr->isTable()) {
        UserdataObject* ud = L->vm->createUserdata(const_cast<void*>(p), 0, true, false);
        t_ptr->asTableObj()->set(Value::userdata(ud), val);
    }
}

int lua_next(lua_State *L, int idx) {
    int abs_idx = to_abs_idx(L, idx);
    Value* t_ptr = get_val(L, abs_idx);
    Value key = L->vm->pop();
    if (t_ptr && t_ptr->isTable()) {
        auto next = t_ptr->asTableObj()->next(key);
        if (!next.first.isNil()) {
            L->vm->push(next.first);
            L->vm->push(next.second);
            return 1;
        }
    }
    return 0;
}

// Metatables
int lua_getmetatable(lua_State *L, int objindex) {
    Value* obj = get_val(L, objindex);
    if (!obj) return 0;
    
    Value mt = Value::nil();
    if (obj->isTable()) {
        mt = obj->asTableObj()->getMetatable();
    } else if (obj->isUserdata()) {
        mt = obj->asUserdataObj()->metatable();
    } else if (obj->isFile()) {
        mt = L->vm->getTypeMetatable(Value::Type::FILE);
    } else {
        mt = L->vm->getTypeMetatable(obj->type());
    }

    if (mt.isNil()) return 0;
    L->vm->push(mt);
    return 1;
}

int lua_setmetatable(lua_State *L, int objindex) {
    int abs_idx = to_abs_idx(L, objindex);
    Value mt = L->vm->pop();
    Value* obj = get_val(L, abs_idx);
    if (!obj) return 0;

    if (!mt.isTable() && !mt.isNil()) return 0;

    if (obj->isTable()) {
        obj->asTableObj()->setMetatable(mt);
    } else if (obj->isUserdata()) {
        obj->asUserdataObj()->setMetatable(mt);
    } else {
        L->vm->setTypeMetatable(obj->type(), mt);
    }
    return 1;
}

int lua_getiuservalue(lua_State *L, int idx, int n) {
    Value* v = get_val(L, idx);
    if (v && v->isUserdata()) {
        Value val = v->asUserdataObj()->getUserValue(n - 1);
        L->vm->push(val);
        return lua_type(L, -1);
    }
    L->vm->push(Value::nil());
    return LUA_TNONE;
}

int lua_setiuservalue(lua_State *L, int idx, int n) {
    Value val = L->vm->pop();
    Value* v = get_val(L, idx);
    if (v && v->isUserdata()) {
        v->asUserdataObj()->setUserValue(n - 1, val);
        return 1;
    }
    return 0;
}

// Userdata
void *lua_newuserdatauv(lua_State *L, size_t sz, int nuvalue) {
    void* data = std::malloc(sz > 0 ? sz : 1);
    if (!data) return nullptr;
    std::memset(data, 0, sz > 0 ? sz : 1);
    UserdataObject* ud = L->vm->createUserdata(data, nuvalue, false, true);
    L->vm->push(Value::userdata(ud));
    return data;
}

// Thread operations
lua_State *lua_newthread(lua_State *L) {
    CoroutineObject* co = L->vm->createCoroutine(nullptr);
    lua_State* th = new lua_State;
    th->vm = L->vm;
    th->is_owned = false;
    th->stackBase = 0;
    th->argCount = 0;
    th->currentClosure = nullptr;
    th->registryVal = Value::nil();
    L->vm->push(Value::thread(co));
    return th;
}

int lua_closethread(lua_State *L, lua_State *from) {
    (void)L; (void)from;
    return LUA_OK;
}

// Calls
void lua_callk(lua_State *L, int nargs, int nresults, lua_KContext ctx, lua_KFunction k) {
    (void)ctx; (void)k;
    int vmRetCount = (nresults == LUA_MULTRET) ? 0 : nresults + 1;
    L->vm->callValue(nargs, vmRetCount);
}

int lua_yieldk(lua_State *L, int nresults, lua_KContext ctx, lua_KFunction k) {
    (void)L; (void)nresults; (void)ctx; (void)k;
    return 0;
}

int lua_pcallk(lua_State *L, int nargs, int nresults, int errfunc, lua_KContext ctx, lua_KFunction k) {
    (void)ctx; (void)k; (void)errfunc;
    int vmRetCount = (nresults == LUA_MULTRET) ? 0 : nresults + 1;
    size_t prevFrames = L->vm->currentCoroutine()->frames.size();
    size_t preCallStack = L->vm->currentCoroutine()->stack.size() - nargs - 1;

    try {
        bool ok = L->vm->callValue(nargs, vmRetCount);
        if (ok && L->vm->currentCoroutine()->frames.size() > prevFrames) {
            ok = L->vm->run(prevFrames);
        }
        if (!ok) {
            std::string err = L->vm->lastErrorMessage();
            L->vm->clearError();
            while (L->vm->currentCoroutine()->frames.size() > prevFrames) {
                L->vm->currentCoroutine()->frames.pop_back();
            }
            while (L->vm->currentCoroutine()->stack.size() > preCallStack) {
                L->vm->pop();
            }
            L->vm->push(Value::runtimeString(L->vm->internString(err)));
            return LUA_ERRRUN;
        }
        return LUA_OK;
    } catch (const RuntimeError& e) {
        L->vm->clearError();
        while (L->vm->currentCoroutine()->frames.size() > prevFrames) {
            L->vm->currentCoroutine()->frames.pop_back();
        }
        while (L->vm->currentCoroutine()->stack.size() > preCallStack) {
            L->vm->pop();
        }
        L->vm->push(Value::runtimeString(L->vm->internString(e.what())));
        return LUA_ERRRUN;
    } catch (const std::exception& e) {
        L->vm->clearError();
        while (L->vm->currentCoroutine()->frames.size() > prevFrames) {
            L->vm->currentCoroutine()->frames.pop_back();
        }
        while (L->vm->currentCoroutine()->stack.size() > preCallStack) {
            L->vm->pop();
        }
        L->vm->push(Value::runtimeString(L->vm->internString(e.what())));
        return LUA_ERRRUN;
    }
}

int lua_concat(lua_State *L, int n) {
    if (n < 0) return 0;
    if (n == 0) {
        lua_pushliteral(L, "");
        return 0;
    }
    if (n == 1) return 0;
    
    std::string result;
    int top = lua_gettop(L);
    int start = top - n + 1;
    for (int i = 0; i < n; i++) {
        size_t len = 0;
        const char* s = lua_tolstring(L, start + i, &len);
        if (!s) {
            luaL_error(L, "attempt to concatenate a %s value", luaL_typename(L, start + i));
            return 0;
        }
        result.append(s, len);
    }
    lua_pop(L, n);
    lua_pushlstring(L, result.data(), result.size());
    return 0;
}

int lua_error(lua_State *L) {
    std::string msg = "error";
    if (lua_gettop(L) > 0) {
        const char* s = lua_tostring(L, -1);
        if (s) msg = s;
    }
    L->vm->runtimeError(msg);
    return 0;
}

// Memory allocator
static void *std_alloc(void *ud, void *ptr, size_t osize, size_t nsize) {
    (void)ud; (void)osize;
    if (nsize == 0) {
        std::free(ptr);
        return nullptr;
    }
    return std::realloc(ptr, nsize);
}

lua_Alloc lua_getallocf(lua_State *L, void **ud) {
    (void)L;
    if (ud) *ud = nullptr;
    return std_alloc;
}

void lua_setallocf(lua_State *L, lua_Alloc f, void *ud) {
    (void)L; (void)f; (void)ud;
}

// Standard library openers
int luaopen_base(lua_State *L) { (void)L; return 1; }
int luaopen_coroutine(lua_State *L) { (void)L; return 1; }
int luaopen_table(lua_State *L) { (void)L; return 1; }
int luaopen_io(lua_State *L) { (void)L; return 1; }
int luaopen_os(lua_State *L) { (void)L; return 1; }
int luaopen_string(lua_State *L) { (void)L; return 1; }
int luaopen_math(lua_State *L) { (void)L; return 1; }
int luaopen_utf8(lua_State *L) { (void)L; return 1; }
int luaopen_debug(lua_State *L) { (void)L; return 1; }
int luaopen_package(lua_State *L) { (void)L; return 1; }

void luaL_openlibs(lua_State *L) {
    L->vm->initStandardLibrary();
}

// Auxiliary Library Implementation
void luaL_checkversion_(lua_State *L, lua_Number ver, size_t sz) {
    (void)L; (void)ver; (void)sz;
}

int luaL_argerror(lua_State *L, int arg, const char *extramsg) {
    return luaL_error(L, "bad argument #%d (%s)", arg, extramsg);
}

int luaL_typeerror(lua_State *L, int arg, const char *tname) {
    const char *msg;
    const char *typearg;
    if (luaL_getmetafield(L, arg, "__name") == LUA_TSTRING)
        typearg = lua_tostring(L, -1);
    else if (lua_type(L, arg) == LUA_TLIGHTUSERDATA)
        typearg = "light userdata";
    else
        typearg = luaL_typename(L, arg);
    msg = lua_pushfstring(L, "%s expected, got %s", tname, typearg);
    return luaL_argerror(L, arg, msg);
}

void luaL_checktype(lua_State *L, int arg, int t) {
    if (lua_type(L, arg) != t) {
        luaL_typeerror(L, arg, lua_typename(L, t));
    }
}

void luaL_checkany(lua_State *L, int arg) {
    if (lua_type(L, arg) == LUA_TNONE) {
        luaL_argerror(L, arg, "value expected");
    }
}

const char *luaL_checklstring(lua_State *L, int arg, size_t *l) {
    const char *s = lua_tolstring(L, arg, l);
    if (!s) {
        luaL_typeerror(L, arg, lua_typename(L, LUA_TSTRING));
        return "";
    }
    return s;
}

const char *luaL_optlstring(lua_State *L, int arg, const char *def, size_t *len) {
    if (lua_isnoneornil(L, arg)) {
        if (len) *len = (def ? strlen(def) : 0);
        return def;
    }
    return luaL_checklstring(L, arg, len);
}

lua_Number luaL_checknumber(lua_State *L, int arg) {
    int isnum = 0;
    lua_Number d = lua_tonumberx(L, arg, &isnum);
    if (!isnum) {
        luaL_typeerror(L, arg, lua_typename(L, LUA_TNUMBER));
        return 0.0;
    }
    return d;
}

lua_Number luaL_optnumber(lua_State *L, int arg, lua_Number def) {
    return lua_isnoneornil(L, arg) ? def : luaL_checknumber(L, arg);
}

lua_Integer luaL_checkinteger(lua_State *L, int arg) {
    int isnum = 0;
    lua_Integer d = lua_tointegerx(L, arg, &isnum);
    if (!isnum) {
        luaL_typeerror(L, arg, lua_typename(L, LUA_TNUMBER));
        return 0;
    }
    return d;
}

lua_Integer luaL_optinteger(lua_State *L, int arg, lua_Integer def) {
    return lua_isnoneornil(L, arg) ? def : luaL_checkinteger(L, arg);
}

int luaL_checkoption(lua_State *L, int arg, const char *def, const char *const lst[]) {
    const char *name = (def) ? luaL_optstring(L, arg, def) : luaL_checkstring(L, arg);
    for (int i = 0; lst[i]; i++) {
        if (strcmp(lst[i], name) == 0) return i;
    }
    return luaL_error(L, "bad argument #%d to option '%s'", arg, name);
}

int luaL_newmetatable(lua_State *L, const char *tname) {
    if (lua_getfield(L, LUA_REGISTRYINDEX, tname) != LUA_TNIL) {
        return 0; // already exists
    }
    lua_pop(L, 1); // pop nil
    lua_newtable(L);
    lua_pushstring(L, tname);
    lua_setfield(L, -2, "__name");
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, tname);
    return 1;
}

void luaL_setmetatable(lua_State *L, const char *tname) {
    luaL_getmetatable(L, tname);
    lua_setmetatable(L, -2);
}

int luaL_getmetafield(lua_State *L, int obj, const char *e) {
    if (!lua_getmetatable(L, obj)) return LUA_TNIL;
    lua_pushstring(L, e);
    lua_rawget(L, -2);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 2);
        return LUA_TNIL;
    }
    lua_remove(L, -2);
    return lua_type(L, -1);
}

int luaL_callmeta(lua_State *L, int obj, const char *e) {
    obj = lua_absindex(L, obj);
    if (!luaL_getmetafield(L, obj, e)) return 0;
    lua_pushvalue(L, obj);
    lua_call(L, 1, 1);
    return 1;
}

const char *luaL_tolstring(lua_State *L, int idx, size_t *len) {
    if (luaL_callmeta(L, idx, "__tostring")) {
        if (!lua_isstring(L, -1))
            luaL_error(L, "'__tostring' must return a string");
    } else {
        Value* v = get_val(L, idx);
        if (!v) {
            lua_pushliteral(L, "nil");
        } else {
            lua_pushstring(L, v->toString().c_str());
        }
    }
    return lua_tolstring(L, -1, len);
}

void *luaL_testudata(lua_State *L, int ud, const char *tname) {
    Value* v = get_val(L, ud);
    if (!v) return nullptr;
    
    // Support FILE* userdata
    if (v->isFile() && strcmp(tname, "FILE*") == 0) {
        return v->asFileObj()->stream();
    }
    
    if (v->isUserdata()) {
        UserdataObject* udo = v->asUserdataObj();
        Value mt = udo->metatable();
        if (mt.isTable()) {
            Value regMt = L->vm->registryTable()->get(tname);
            if (regMt.isTable() && mt.asTableObj() == regMt.asTableObj()) {
                return udo->data();
            }
        }
    }
    return nullptr;
}

void *luaL_checkudata(lua_State *L, int ud, const char *tname) {
    void *p = luaL_testudata(L, ud, tname);
    if (!p) {
        luaL_typeerror(L, ud, tname);
        return nullptr;
    }
    return p;
}

void luaL_setfuncs(lua_State *L, const struct luaL_Reg *l, int nup) {
    for (; l->name != NULL; l++) {
        for (int i = 0; i < nup; i++) {
            lua_pushvalue(L, -nup);
        }
        lua_pushcclosure(L, l->func, nup);
        lua_setfield(L, -(nup + 2), l->name);
    }
    lua_pop(L, nup);
}

int luaL_getsubtable(lua_State *L, int idx, const char *fname) {
    if (lua_getfield(L, idx, fname) == LUA_TTABLE)
        return 1;
    lua_pop(L, 1);
    idx = lua_absindex(L, idx);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, idx, fname);
    return 0;
}

void luaL_requiref(lua_State *L, const char *modname, lua_CFunction openf, int glb) {
    luaL_getsubtable(L, LUA_REGISTRYINDEX, "_LOADED");
    lua_getfield(L, -1, modname);
    if (!lua_toboolean(L, -1)) {
        lua_pop(L, 1);
        lua_pushcfunction(L, openf);
        lua_pushstring(L, modname);
        lua_call(L, 1, 1);
        lua_pushvalue(L, -1);
        lua_setfield(L, -3, modname);
    }
    lua_remove(L, -2);
    if (glb) {
        lua_pushvalue(L, -1);
        lua_setglobal(L, modname);
    }
}

lua_Integer luaL_len(lua_State *L, int idx) {
    Value* v = get_val(L, idx);
    if (!v) return 0;
    if (v->isTable()) {
        Value mt = v->asTableObj()->getMetatable();
        if (mt.isTable()) {
            Value h = mt.asTableObj()->get("__len");
            if (!h.isNil()) {
                L->vm->push(h);
                L->vm->push(*v);
                L->vm->callValue(1, 2);
                lua_Integer res = lua_tointeger(L, -1);
                lua_pop(L, 1);
                return res;
            }
        }
    }
    return static_cast<lua_Integer>(lua_rawlen(L, idx));
}

int luaL_ref(lua_State *L, int t) {
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return LUA_REFNIL;
    }
    Value val = L->vm->pop();
    Value* t_ptr = get_val(L, t);
    if (!t_ptr || !t_ptr->isTable()) {
        return LUA_REFNIL;
    }
    TableObject* table = t_ptr->asTableObj();
    Value freelistHead = table->get(Value::integer(0));
    int ref;
    if (freelistHead.isInteger() && freelistHead.asInteger() > 0) {
        ref = static_cast<int>(freelistHead.asInteger());
        Value nextFree = table->get(Value::integer(ref));
        table->set(Value::integer(0), nextFree);
    } else {
        ref = static_cast<int>(table->length()) + 1;
        while (!table->get(Value::integer(ref)).isNil()) {
            ref++;
        }
    }
    table->set(Value::integer(ref), val);
    return ref;
}

void luaL_unref(lua_State *L, int t, int ref) {
    if (ref < 0) return;
    Value* t_ptr = get_val(L, t);
    if (!t_ptr || !t_ptr->isTable()) return;
    TableObject* table = t_ptr->asTableObj();
    Value freelistHead = table->get(Value::integer(0));
    table->set(Value::integer(ref), freelistHead);
    table->set(Value::integer(0), Value::integer(ref));
}

int luaL_error(lua_State *L, const char *fmt, ...) {
    char buf[2048];
    va_list argp;
    va_start(argp, fmt);
    vsnprintf(buf, sizeof(buf), fmt, argp);
    va_end(argp);
    lua_pushstring(L, buf);
    return lua_error(L);
}

// Buffer API
void luaL_buffinit(lua_State *L, luaL_Buffer *B) {
    B->L = L;
    B->b = B->init;
    B->size = LUAL_BUFFERSIZE;
    B->n = 0;
}

char *luaL_prepbuffsize(luaL_Buffer *B, size_t sz) {
    if (B->n + sz > B->size) {
        size_t newsize = B->size * 2;
        if (newsize < B->n + sz) newsize = B->n + sz + LUAL_BUFFERSIZE;
        char *newb = (char*)malloc(newsize);
        if (!newb) {
            luaL_error(B->L, "not enough memory for buffer allocation");
            return B->b + B->n;
        }
        memcpy(newb, B->b, B->n);
        if (B->b != B->init) free(B->b);
        B->b = newb;
        B->size = newsize;
    }
    return B->b + B->n;
}

void luaL_addlstring(luaL_Buffer *B, const char *s, size_t l) {
    if (l > 0) {
        char *p = luaL_prepbuffsize(B, l);
        memcpy(p, s, l);
        B->n += l;
    }
}

void luaL_addstring(luaL_Buffer *B, const char *s) {
    if (s) {
        luaL_addlstring(B, s, strlen(s));
    }
}

void luaL_addvalue(luaL_Buffer *B) {
    size_t l = 0;
    const char *s = luaL_tolstring(B->L, -1, &l);
    luaL_addlstring(B, s, l);
    lua_pop(B->L, 2); // pop tolstring result and original value
}

void luaL_pushresult(luaL_Buffer *B) {
    lua_pushlstring(B->L, B->b, B->n);
    if (B->b != B->init) {
        free(B->b);
    }
    B->b = B->init;
    B->size = LUAL_BUFFERSIZE;
    B->n = 0;
}

void luaL_pushresultsize(luaL_Buffer *B, size_t sz) {
    B->n += sz;
    luaL_pushresult(B);
}

char *luaL_buffinitsize(lua_State *L, luaL_Buffer *B, size_t sz) {
    luaL_buffinit(L, B);
    return luaL_prepbuffsize(B, sz);
}

void luaL_openlib(lua_State *L, const char *libname, const luaL_Reg *l, int nup) {
    if (libname) {
        luaL_getsubtable(L, LUA_REGISTRYINDEX, "_LOADED");
        lua_getfield(L, -1, libname);
        if (!lua_istable(L, -1)) {
            lua_pop(L, 1);
            lua_newtable(L);
            lua_pushvalue(L, -1);
            lua_setfield(L, -3, libname);
            lua_pushvalue(L, -1);
            lua_setglobal(L, libname);
        }
        lua_remove(L, -2);
        lua_insert(L, -(nup + 1));
    }
    if (l) {
        luaL_setfuncs(L, l, nup);
    } else {
        lua_pop(L, nup);
    }
}

// Exported functions for macros
#undef lua_call
void lua_call(lua_State *L, int nargs, int nresults) {
    lua_callk(L, nargs, nresults, 0, nullptr);
}

#undef lua_pcall
int lua_pcall(lua_State *L, int nargs, int nresults, int errfunc) {
    return lua_pcallk(L, nargs, nresults, errfunc, 0, nullptr);
}

#undef lua_yield
int lua_yield(lua_State *L, int nresults) {
    return lua_yieldk(L, nresults, 0, nullptr);
}

#undef lua_pop
void lua_pop(lua_State *L, int n) {
    lua_settop(L, -(n) - 1);
}

#undef lua_newtable
void lua_newtable(lua_State *L) {
    lua_createtable(L, 0, 0);
}

#undef lua_pushcfunction
void lua_pushcfunction(lua_State *L, lua_CFunction f) {
    lua_pushcclosure(L, f, 0);
}

#undef lua_isfunction
int lua_isfunction(lua_State *L, int n) {
    return lua_type(L, n) == LUA_TFUNCTION;
}

#undef lua_istable
int lua_istable(lua_State *L, int n) {
    return lua_type(L, n) == LUA_TTABLE;
}

#undef lua_islightuserdata
int lua_islightuserdata(lua_State *L, int n) {
    return lua_type(L, n) == LUA_TLIGHTUSERDATA;
}

#undef lua_isnil
int lua_isnil(lua_State *L, int n) {
    return lua_type(L, n) == LUA_TNIL;
}

#undef lua_isboolean
int lua_isboolean(lua_State *L, int n) {
    return lua_type(L, n) == LUA_TBOOLEAN;
}

#undef lua_isthread
int lua_isthread(lua_State *L, int n) {
    return lua_type(L, n) == LUA_TTHREAD;
}

#undef lua_isnone
int lua_isnone(lua_State *L, int n) {
    return lua_type(L, n) == LUA_TNONE;
}

#undef lua_isnoneornil
int lua_isnoneornil(lua_State *L, int n) {
    return lua_type(L, n) <= 0;
}

#undef lua_tostring
const char *lua_tostring(lua_State *L, int i) {
    return lua_tolstring(L, i, nullptr);
}

#undef lua_tointeger
lua_Integer lua_tointeger(lua_State *L, int i) {
    return lua_tointegerx(L, i, nullptr);
}

#undef lua_tonumber
lua_Number lua_tonumber(lua_State *L, int i) {
    return lua_tonumberx(L, i, nullptr);
}

#undef lua_insert
void lua_insert(lua_State *L, int idx) {
    lua_rotate(L, idx, 1);
}

#undef lua_remove
void lua_remove(lua_State *L, int idx) {
    lua_rotate(L, idx, -1);
    lua_pop(L, 1);
}

#undef lua_replace
void lua_replace(lua_State *L, int idx) {
    lua_copy(L, -1, idx);
    lua_pop(L, 1);
}

int luaL_loadbufferx(lua_State *L, const char *buff, size_t sz, const char *name, const char *mode) {
    (void)mode;
    std::string source(buff ? buff : "", sz);
    std::string chunkName = name ? name : "chunk";
    try {
        Lexer lexer(source);
        Parser parser(lexer);
        auto program = parser.parse();
        if (!program) {
            lua_pushstring(L, "syntax error: unexpected end of input");
            return LUA_ERRSYNTAX;
        }
        CodeGenerator codegen;
        auto function = codegen.generate(program.get(), chunkName);
        if (!function) {
            lua_pushstring(L, "code generation failed");
            return LUA_ERRSYNTAX;
        }
        FunctionObject* funcPtr = function.get();
        L->vm->registerFunction(function.release());
        L->vm->internConstants(*funcPtr);
        ClosureObject* closure = L->vm->createClosure(funcPtr);
        L->vm->setupRootUpvalues(closure);
        L->vm->push(Value::closure(closure));
        return LUA_OK;
    } catch (const CompileError& e) {
        lua_pushstring(L, e.what());
        return LUA_ERRSYNTAX;
    } catch (const std::exception& e) {
        lua_pushstring(L, e.what());
        return LUA_ERRSYNTAX;
    }
}

int luaL_loadstring(lua_State *L, const char *s) {
    return luaL_loadbufferx(L, s, s ? strlen(s) : 0, s, nullptr);
}

int luaL_loadfilex(lua_State *L, const char *filename, const char *mode) {
    std::string content;
    std::string chunkName;
    if (filename == nullptr) {
        chunkName = "=stdin";
        content.assign((std::istreambuf_iterator<char>(std::cin)),
                       std::istreambuf_iterator<char>());
    } else {
        std::ifstream file(filename);
        if (!file.is_open()) {
            std::string err = std::string("cannot open ") + filename;
            lua_pushstring(L, err.c_str());
            return LUA_ERRFILE;
        }
        content.assign((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
        chunkName = std::string("@") + filename;
    }
    return luaL_loadbufferx(L, content.data(), content.size(), chunkName.c_str(), mode);
}


