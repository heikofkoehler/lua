#ifndef LUA_H
#define LUA_H

#include <stddef.h>

#define LUA_VERSION "Lua 5.4"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lua_State lua_State;

typedef int (*lua_CFunction)(lua_State *L);

#define LUA_TNONE          (-1)
#define LUA_TNIL            0
#define LUA_TBOOLEAN        1
#define LUA_TLIGHTUSERDATA  2
#define LUA_TNUMBER         3
#define LUA_TSTRING         4
#define LUA_TTABLE          5
#define LUA_TFUNCTION       6
#define LUA_TUSERDATA       7
#define LUA_TTHREAD         8

// State manipulation
lua_State *lua_newstate(void);
void lua_close(lua_State *L);

// Basic stack manipulation
int lua_gettop(lua_State *L);
void lua_settop(lua_State *L, int idx);
void lua_pushvalue(lua_State *L, int idx);
void lua_pop(lua_State *L, int n);

// Push functions
void lua_pushnil(lua_State *L);
void lua_pushnumber(lua_State *L, double n);
void lua_pushinteger(lua_State *L, long long n);
void lua_pushstring(lua_State *L, const char *s);
void lua_pushboolean(lua_State *L, int b);
void lua_pushcfunction(lua_State *L, lua_CFunction f);

// Check functions
int lua_isnumber(lua_State *L, int idx);
int lua_isstring(lua_State *L, int idx);
int lua_isboolean(lua_State *L, int idx);
int lua_isnil(lua_State *L, int idx);
int lua_isfunction(lua_State *L, int idx);
int lua_istable(lua_State *L, int idx);
int lua_type(lua_State *L, int idx);
const char *lua_typename(lua_State *L, int tp);

// Get functions
double lua_tonumber(lua_State *L, int idx);
long long lua_tointeger(lua_State *L, int idx);
int lua_toboolean(lua_State *L, int idx);
const char *lua_tostring(lua_State *L, int idx);

// Global
void lua_getglobal(lua_State *L, const char *name);
void lua_setglobal(lua_State *L, const char *name);

// Calls
int lua_pcall(lua_State *L, int nargs, int nresults, int errfunc);

#ifdef __cplusplus
}
#endif

#endif // LUA_H
