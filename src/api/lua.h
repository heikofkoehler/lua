#ifndef LUA_H
#define LUA_H

#include <stddef.h>
#include <stdarg.h>

#define LUA_VERSION_MAJOR_N   5
#define LUA_VERSION_MINOR_N   5
#define LUA_VERSION_RELEASE_N 0
#define LUA_VERSION_NUM       505

#define LUA_VERSION_MAJOR   "5"
#define LUA_VERSION_MINOR   "5"
#define LUA_VERSION_RELEASE "0"
#define LUA_VERSION         "Lua 5.5"
#define LUA_RELEASE         "Lua 5.5.0"
#define LUA_COPYRIGHT       LUA_RELEASE "  Copyright (C) 1994-2024 Lua.org, PUC-Rio"
#define LUA_AUTHORS         "R. Ierusalimschy, L. H. de Figueiredo, W. Celes"

#define LUA_SIGNATURE "\x1bLua"
#define LUA_MULTRET   (-1)

#include "luaconf.h"

#define LUA_REGISTRYINDEX (-1001000)
#define lua_upvalueindex(i) (LUA_REGISTRYINDEX - (i))

/* Predefined values in the registry */
#define LUA_RIDX_MAINTHREAD 1
#define LUA_RIDX_GLOBALS    2
#define LUA_RIDX_LAST       LUA_RIDX_GLOBALS

/* Basic types */
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
#define LUA_NUMTYPES        9

/* Thread status */
#define LUA_OK        0
#define LUA_YIELD     1
#define LUA_ERRRUN    2
#define LUA_ERRSYNTAX 3
#define LUA_ERRMEM    4
#define LUA_ERRERR    5

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lua_State lua_State;

typedef int (*lua_CFunction)(lua_State *L);
typedef long long lua_Integer;
typedef double lua_Number;
typedef ptrdiff_t lua_KContext;
typedef int (*lua_KFunction)(lua_State *L, int status, lua_KContext ctx);
typedef const char * (*lua_Reader) (lua_State *L, void *ud, size_t *sz);
typedef int (*lua_Writer) (lua_State *L, const void *p, size_t sz, void *ud);
typedef void * (*lua_Alloc) (void *ud, void *ptr, size_t osize, size_t nsize);
typedef void * (*lua_Free) (void *ud, void *ptr, size_t osize, size_t nsize);

/* State manipulation */
LUA_API lua_State *(lua_newstate) (void);
LUA_API void       (lua_close) (lua_State *L);
LUA_API lua_State *(lua_newthread) (lua_State *L);
LUA_API int        (lua_closethread) (lua_State *L, lua_State *from);

/* Basic stack manipulation */
LUA_API int   (lua_absindex) (lua_State *L, int idx);
LUA_API int   (lua_gettop) (lua_State *L);
LUA_API void  (lua_settop) (lua_State *L, int idx);
LUA_API void  (lua_pushvalue) (lua_State *L, int idx);
LUA_API void  (lua_rotate) (lua_State *L, int idx, int n);
LUA_API void  (lua_copy) (lua_State *L, int fromidx, int toidx);
LUA_API int   (lua_checkstack) (lua_State *L, int n);
LUA_API void  (lua_insert) (lua_State *L, int idx);
LUA_API void  (lua_remove) (lua_State *L, int idx);
LUA_API void  (lua_replace) (lua_State *L, int idx);
LUA_API void  (lua_pop) (lua_State *L, int n);
LUA_API void  (lua_newtable) (lua_State *L);
LUA_API void  (lua_pushcfunction) (lua_State *L, lua_CFunction f);
LUA_API int   (lua_isfunction) (lua_State *L, int n);
LUA_API int   (lua_istable) (lua_State *L, int n);
LUA_API int   (lua_islightuserdata) (lua_State *L, int n);
LUA_API int   (lua_isnil) (lua_State *L, int n);
LUA_API int   (lua_isboolean) (lua_State *L, int n);
LUA_API int   (lua_isthread) (lua_State *L, int n);
LUA_API int   (lua_isnone) (lua_State *L, int n);
LUA_API int   (lua_isnoneornil) (lua_State *L, int n);
LUA_API const char *(lua_tostring) (lua_State *L, int i);
LUA_API lua_Integer (lua_tointeger) (lua_State *L, int i);
LUA_API lua_Number  (lua_tonumber) (lua_State *L, int i);
LUA_API void  (lua_call) (lua_State *L, int nargs, int nresults);
LUA_API int   (lua_pcall) (lua_State *L, int nargs, int nresults, int errfunc);
LUA_API int   (lua_yield) (lua_State *L, int nresults);

#define lua_pop(L, n)           lua_settop(L, -(n) - 1)
#define lua_newtable(L)         lua_createtable(L, 0, 0)
#define lua_register(L, n, f)   (lua_pushcfunction(L, (f)), lua_setglobal(L, (n)))
#define lua_pushcfunction(L, f) lua_pushcclosure(L, (f), 0)
#define lua_isfunction(L, n)    (lua_type(L, (n)) == LUA_TFUNCTION)
#define lua_istable(L, n)       (lua_type(L, (n)) == LUA_TTABLE)
#define lua_islightuserdata(L, n) (lua_type(L, (n)) == LUA_TLIGHTUSERDATA)
#define lua_isnil(L, n)         (lua_type(L, (n)) == LUA_TNIL)
#define lua_isboolean(L, n)     (lua_type(L, (n)) == LUA_TBOOLEAN)
#define lua_isthread(L, n)      (lua_type(L, (n)) == LUA_TTHREAD)
#define lua_isnone(L, n)        (lua_type(L, (n)) == LUA_TNONE)
#define lua_isnoneornil(L, n)   (lua_type(L, (n)) <= 0)
#define lua_pushliteral(L, s)   lua_pushstring(L, "" s)
#define lua_pushglobaltable(L)  ((void)lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS))

/* Stack manipulation legacy aliases */
#define lua_remove(L, idx)      (lua_rotate(L, (idx), -1), lua_pop(L, 1))
#define lua_insert(L, idx)      lua_rotate(L, (idx), 1)
#define lua_replace(L, idx)     (lua_copy(L, -1, (idx)), lua_pop(L, 1))

/* Push functions */
LUA_API void        (lua_pushnil) (lua_State *L);
LUA_API void        (lua_pushnumber) (lua_State *L, lua_Number n);
LUA_API void        (lua_pushinteger) (lua_State *L, lua_Integer n);
LUA_API const char *(lua_pushlstring) (lua_State *L, const char *s, size_t len);
LUA_API const char *(lua_pushstring) (lua_State *L, const char *s);
LUA_API const char *(lua_pushvfstring) (lua_State *L, const char *fmt, va_list argp);
LUA_API const char *(lua_pushfstring) (lua_State *L, const char *fmt, ...);
LUA_API void        (lua_pushcclosure) (lua_State *L, lua_CFunction fn, int n);
LUA_API void        (lua_pushboolean) (lua_State *L, int b);
LUA_API void        (lua_pushlightuserdata) (lua_State *L, void *p);
LUA_API int         (lua_pushthread) (lua_State *L);
LUA_API const char *(lua_pushexternalstring) (lua_State *L, const char *s, size_t len, lua_Free falloc, void *ud);

/* Check and type query */
LUA_API int         (lua_type) (lua_State *L, int idx);
LUA_API const char *(lua_typename) (lua_State *L, int tp);
LUA_API int         (lua_isnumber) (lua_State *L, int idx);
LUA_API int         (lua_isstring) (lua_State *L, int idx);
LUA_API int         (lua_iscfunction) (lua_State *L, int idx);
LUA_API int         (lua_isinteger) (lua_State *L, int idx);
LUA_API int         (lua_isuserdata) (lua_State *L, int idx);
LUA_API int         (lua_rawequal) (lua_State *L, int idx1, int idx2);

/* Get functions */
LUA_API lua_Number  (lua_tonumberx) (lua_State *L, int idx, int *isnum);
LUA_API lua_Integer (lua_tointegerx) (lua_State *L, int idx, int *isnum);
#define lua_tonumber(L, i)     lua_tonumberx(L, (i), NULL)
#define lua_tointeger(L, i)    lua_tointegerx(L, (i), NULL)
LUA_API int         (lua_toboolean) (lua_State *L, int idx);
LUA_API const char *(lua_tolstring) (lua_State *L, int idx, size_t *len);
#define lua_tostring(L, i)     lua_tolstring(L, (i), NULL)
LUA_API size_t      (lua_rawlen) (lua_State *L, int idx);
#define lua_objlen(L, i)       lua_rawlen(L, (i))
LUA_API lua_CFunction (lua_tocfunction) (lua_State *L, int idx);
LUA_API void       *(lua_touserdata) (lua_State *L, int idx);
LUA_API lua_State  *(lua_tothread) (lua_State *L, int idx);
LUA_API const void *(lua_topointer) (lua_State *L, int idx);

/* Table operations */
LUA_API void  (lua_createtable) (lua_State *L, int narr, int nrec);
LUA_API int   (lua_gettable) (lua_State *L, int idx);
LUA_API int   (lua_getfield) (lua_State *L, int idx, const char *k);
LUA_API int   (lua_geti) (lua_State *L, int idx, lua_Integer i);
LUA_API int   (lua_rawget) (lua_State *L, int idx);
LUA_API int   (lua_rawgeti) (lua_State *L, int idx, lua_Integer n);
LUA_API int   (lua_rawgetp) (lua_State *L, int idx, const void *p);
LUA_API void  (lua_settable) (lua_State *L, int idx);
LUA_API void  (lua_setfield) (lua_State *L, int idx, const char *k);
LUA_API void  (lua_seti) (lua_State *L, int idx, lua_Integer i);
LUA_API void  (lua_rawset) (lua_State *L, int idx);
LUA_API void  (lua_rawseti) (lua_State *L, int idx, lua_Integer n);
LUA_API void  (lua_rawsetp) (lua_State *L, int idx, const void *p);
LUA_API int   (lua_next) (lua_State *L, int idx);

/* Global environment operations */
LUA_API void  (lua_getglobal) (lua_State *L, const char *name);
LUA_API void  (lua_setglobal) (lua_State *L, const char *name);

/* Metatables and User Values */
LUA_API int   (lua_getmetatable) (lua_State *L, int objindex);
LUA_API int   (lua_setmetatable) (lua_State *L, int objindex);
LUA_API int   (lua_getiuservalue) (lua_State *L, int idx, int n);
LUA_API int   (lua_setiuservalue) (lua_State *L, int idx, int n);
#define lua_getuservalue(L, i) lua_getiuservalue(L, (i), 1)
#define lua_setuservalue(L, i) lua_setiuservalue(L, (i), 1)

/* Userdata allocation */
LUA_API void *(lua_newuserdatauv) (lua_State *L, size_t sz, int nuvalue);
#define lua_newuserdata(L, s)  lua_newuserdatauv(L, (s), 1)

LUA_API void  (lua_callk) (lua_State *L, int nargs, int nresults, lua_KContext ctx, lua_KFunction k);
#define lua_call(L, n, r)      lua_callk(L, (n), (r), 0, NULL)
LUA_API int   (lua_pcallk) (lua_State *L, int nargs, int nresults, int errfunc, lua_KContext ctx, lua_KFunction k);
#define lua_pcall(L, n, r, f)  lua_pcallk(L, (n), (r), (f), 0, NULL)
LUA_API int   (lua_yieldk) (lua_State *L, int nresults, lua_KContext ctx, lua_KFunction k);
#define lua_yield(L, n)        lua_yieldk(L, (n), 0, NULL)
LUA_API int   (lua_error) (lua_State *L);
LUA_API int   (lua_concat) (lua_State *L, int n);

/* Memory allocation */
LUA_API lua_Alloc (lua_getallocf) (lua_State *L, void **ud);
LUA_API void      (lua_setallocf) (lua_State *L, lua_Alloc f, void *ud);

#ifdef __cplusplus
}
#endif

#endif // LUA_H
