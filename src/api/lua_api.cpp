#include "api/lua.h"
#include "api/lua_state.h"
#include "vm/vm.hpp"
#include "value/value.hpp"
#include "value/table.hpp"
#include "value/userdata.hpp"
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
static int to_abs_idx(lua_State *L, int idx) {
    if (idx > 0) return idx;
    return lua_gettop(L) + idx + 1;
}

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

void lua_pop(lua_State *L, int n) {
    lua_settop(L, -(n) - 1);
}

void lua_remove(lua_State *L, int idx) {
    auto& stack = L->vm->currentCoroutine()->stack;
    int abs_idx = to_abs_idx(L, idx);
    size_t stack_idx = L->stackBase + static_cast<size_t>(abs_idx - 1);
    if (stack_idx < stack.size()) {
        stack.erase(stack.begin() + stack_idx);
    }
}

void lua_insert(lua_State *L, int idx) {
    auto& stack = L->vm->currentCoroutine()->stack;
    int abs_idx = to_abs_idx(L, idx);
    size_t stack_idx = L->stackBase + static_cast<size_t>(abs_idx - 1);
    if (stack_idx < stack.size()) {
        Value v = stack.back();
        stack.pop_back();
        stack.insert(stack.begin() + stack_idx, v);
    }
}

void lua_replace(lua_State *L, int idx) {
    auto& stack = L->vm->currentCoroutine()->stack;
    int abs_idx = to_abs_idx(L, idx);
    size_t stack_idx = L->stackBase + static_cast<size_t>(abs_idx - 1);
    if (stack_idx < stack.size()) {
        stack[stack_idx] = stack.back();
        stack.pop_back();
    }
}

void lua_copy(lua_State *L, int fromidx, int toidx) {
    Value* from = get_val(L, fromidx);
    Value* to = get_val(L, toidx);
    if (from && to) {
        *to = *from;
    }
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

int lua_isuserdata(lua_State *L, int idx) {
    return lua_type(L, idx) == LUA_TUSERDATA;
}

// Get functions
double lua_tonumber(lua_State *L, int idx) {
    Value* v = get_val(L, idx);
    return (v && v->isNumber()) ? v->asNumber() : 0.0;
}

long long lua_tointeger(lua_State *L, int idx) {
    Value* v = get_val(L, idx);
    return (v && v->isNumber()) ? static_cast<long long>(v->asInteger()) : 0;
}

int lua_toboolean(lua_State *L, int idx) {
    Value* v = get_val(L, idx);
    return (v && !v->isFalsey()) ? 1 : 0;
}

const char *lua_tostring(lua_State *L, int idx) {
    Value* v = get_val(L, idx);
    if (v && v->isString()) {
        return v->asStringObj()->chars();
    }
    return nullptr;
}

void *lua_touserdata(lua_State *L, int idx) {
    Value* v = get_val(L, idx);
    if (v && v->isUserdata()) {
        return v->asUserdataObj()->data();
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

void lua_settable(lua_State *L, int idx) {
    int abs_idx = to_abs_idx(L, idx);
    Value val = L->vm->pop();
    Value key = L->vm->pop();
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

int lua_rawget(lua_State *L, int idx) {
    return lua_gettable(L, idx); // Our TableObject::get IS raw
}

void lua_rawset(lua_State *L, int idx) {
    lua_settable(L, idx); // Our TableObject::set IS raw
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

// Userdata
void *lua_newuserdata(lua_State *L, size_t size) {
    void* data = malloc(size);
    if (!data) return nullptr;
    memset(data, 0, size);
    
    UserdataObject* ud = L->vm->createUserdata(data);
    L->vm->push(Value::userdata(ud));
    return data;
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

void luaL_openlibs(lua_State *L) {
    L->vm->initStandardLibrary();
}
