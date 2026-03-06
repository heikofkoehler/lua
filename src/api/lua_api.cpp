#include "api/lua.h"
#include "api/lua_state.h"
#include "vm/vm.hpp"
#include "value/value.hpp"
#include <cstring>
#include <algorithm>

// State manipulation
lua_State *lua_newstate(void) {
    lua_State* L = new lua_State;
    L->vm = new VM();
    L->is_owned = true;
    L->stackBase = 0;
    L->argCount = 0;
    return L;
}

void lua_close(lua_State *L) {
    if (L->is_owned) {
        delete L->vm;
    }
    delete L;
}

// Check functions helper
static Value* get_val(lua_State *L, int idx) {
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

    if (abs_idx < stack.size()) {
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

void lua_pop(lua_State *L, int n) {
    lua_settop(L, -(n) - 1);
}

// Push functions
void lua_pushnil(lua_State *L) {
    L->vm->push(Value::nil());
}

void lua_pushnumber(lua_State *L, double n) {
    L->vm->push(Value::number(n));
}

void lua_pushinteger(lua_State *L, long long n) {
    L->vm->push(Value::integer(n));
}

void lua_pushstring(lua_State *L, const char *s) {
    if (s) {
        L->vm->push(Value::runtimeString(L->vm->internString(s)));
    } else {
        L->vm->push(Value::nil());
    }
}

void lua_pushboolean(lua_State *L, int b) {
    L->vm->push(Value::boolean(b != 0));
}

void lua_pushcfunction(lua_State *L, lua_CFunction f) {
    L->vm->push(Value::cFunction(reinterpret_cast<void*>(f)));
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
    if (v->isFunction()) return LUA_TFUNCTION;
    if (v->isThread()) return LUA_TTHREAD;
    if (v->isUserdata()) return LUA_TUSERDATA;
    return LUA_TNONE;
}

const char *lua_typename(lua_State *L, int tp) {
    (void)L;
    switch (tp) {
        case LUA_TNIL: return "nil";
        case LUA_TBOOLEAN: return "boolean";
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
    return v ? v->isNumber() : 0;
}

int lua_isstring(lua_State *L, int idx) {
    Value* v = get_val(L, idx);
    return v ? v->isString() : 0;
}

int lua_isboolean(lua_State *L, int idx) {
    Value* v = get_val(L, idx);
    return v ? v->isBool() : 0;
}

int lua_isnil(lua_State *L, int idx) {
    return lua_type(L, idx) == LUA_TNIL;
}

int lua_isfunction(lua_State *L, int idx) {
    return lua_type(L, idx) == LUA_TFUNCTION;
}

int lua_istable(lua_State *L, int idx) {
    return lua_type(L, idx) == LUA_TTABLE;
}

// Get functions
double lua_tonumber(lua_State *L, int idx) {
    Value* v = get_val(L, idx);
    return (v && v->isNumber()) ? v->asNumber() : 0.0;
}

long long lua_tointeger(lua_State *L, int idx) {
    Value* v = get_val(L, idx);
    return (v && v->isInteger()) ? v->asInteger() : ((v && v->isNumber()) ? (long long)v->asNumber() : 0);
}

int lua_toboolean(lua_State *L, int idx) {
    Value* v = get_val(L, idx);
    return v ? !v->isFalsey() : 0;
}

const char *lua_tostring(lua_State *L, int idx) {
    Value* v = get_val(L, idx);
    if (v && v->isString()) {
        return v->asStringObj()->chars();
    }
    return nullptr;
}

// Global
void lua_getglobal(lua_State *L, const char *name) {
    L->vm->push(L->vm->getGlobal(name));
}

void lua_setglobal(lua_State *L, const char *name) {
    L->vm->setGlobal(name, L->vm->pop());
}

// Calls
int lua_pcall(lua_State *L, int nargs, int nresults, int errfunc) {
    (void)errfunc;
    
    // In our VM, callValue expects retCount encoded as:
    // 0 = LUA_MULTRET
    // N > 0 means N - 1 results.
    // Standard Lua C API passes nresults directly, or LUA_MULTRET (-1).
    int vmRetCount = (nresults == -1) ? 0 : nresults + 1;
    
    bool success = L->vm->callValue(nargs, vmRetCount);
    return success ? 0 : 1;
}
