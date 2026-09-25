#ifndef LUALIB_H
#define LUALIB_H

#include "lua.h"

/* Standard library names */
#define LUA_COLIBNAME   "coroutine"
#define LUA_TABLIBNAME  "table"
#define LUA_IOLIBNAME   "io"
#define LUA_OSLIBNAME   "os"
#define LUA_STRLIBNAME  "string"
#define LUA_MATHLIBNAME "math"
#define LUA_UTF8LIBNAME "utf8"
#define LUA_DBLIBNAME   "debug"
#define LUA_LOADLIBNAME "package"

#ifdef __cplusplus
extern "C" {
#endif

LUALIB_API int luaopen_base(lua_State *L);
LUALIB_API int luaopen_coroutine(lua_State *L);
LUALIB_API int luaopen_table(lua_State *L);
LUALIB_API int luaopen_io(lua_State *L);
LUALIB_API int luaopen_os(lua_State *L);
LUALIB_API int luaopen_string(lua_State *L);
LUALIB_API int luaopen_math(lua_State *L);
LUALIB_API int luaopen_utf8(lua_State *L);
LUALIB_API int luaopen_debug(lua_State *L);
LUALIB_API int luaopen_package(lua_State *L);

/* open all standard libraries */
LUALIB_API void luaL_openlibs(lua_State *L);

#ifdef __cplusplus
}
#endif

#endif // LUALIB_H
