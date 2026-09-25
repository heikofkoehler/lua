#ifndef LAUXLIB_H
#define LAUXLIB_H

#include "lua.h"
#include <stdio.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct luaL_Reg {
    const char *name;
    lua_CFunction func;
} luaL_Reg;

#define LUA_NOREF       (-2)
#define LUA_REFNIL      (-1)

#define luaL_checkversion(L) ((void)0)
#define luaL_newstate() lua_newstate()

LUALIB_API void (luaL_openlibs) (lua_State *L);

#define luaL_newlibtable(L, l) \
    lua_createtable(L, 0, sizeof(l)/sizeof((l)[0]) - 1)

#define luaL_newlib(L, l) \
    (luaL_newlibtable(L, l), luaL_setfuncs(L, l, 0))

#define luaL_argcheck(L, cond, arg, extramsg) \
    ((void)((cond) || luaL_argerror(L, (arg), (extramsg))))

#define luaL_checkstring(L, n)     (luaL_checklstring(L, (n), NULL))
#define luaL_optstring(L, n, d)    (luaL_optlstring(L, (n), (d), NULL))
#define luaL_typename(L, i)        lua_typename(L, lua_type(L, (i)))
#define luaL_getmetatable(L, n)    (lua_getfield(L, LUA_REGISTRYINDEX, (n)))

#define luaL_checkstack(L, sz, msg) ((void)0)

/* Standard I/O stream representation */
#ifndef LUAL_STREAM_DEFINED
#define LUAL_STREAM_DEFINED
typedef struct luaL_Stream {
    FILE *f;
    lua_CFunction closef;
} luaL_Stream;
#endif

/* Buffer API structure */
#define LUAL_BUFFERSIZE   1024

typedef struct luaL_Buffer {
    char *b;     /* buffer address */
    size_t size; /* buffer size */
    size_t n;    /* number of characters in buffer */
    lua_State *L;
    char init[LUAL_BUFFERSIZE]; /* initial buffer */
} luaL_Buffer;

#define luaL_addchar(B, c) \
    ((void)((B)->n < (B)->size || luaL_prepbuffsize((B), 1)), \
     ((B)->b[(B)->n++] = (c)))

#define luaL_prepbuffer(B) luaL_prepbuffsize(B, LUAL_BUFFERSIZE)

/* Auxiliary functions */
LUALIB_API void        (luaL_checkversion_) (lua_State *L, lua_Number ver, size_t sz);
LUALIB_API int         (luaL_getmetafield) (lua_State *L, int obj, const char *e);
LUALIB_API int         (luaL_callmeta) (lua_State *L, int obj, const char *e);
LUALIB_API const char *(luaL_tolstring) (lua_State *L, int idx, size_t *len);
LUALIB_API int         (luaL_argerror) (lua_State *L, int arg, const char *extramsg);
LUALIB_API int         (luaL_typeerror) (lua_State *L, int arg, const char *tname);
LUALIB_API const char *(luaL_checklstring) (lua_State *L, int arg, size_t *l);
LUALIB_API const char *(luaL_optlstring) (lua_State *L, int arg, const char *def, size_t *l);
LUALIB_API lua_Number  (luaL_checknumber) (lua_State *L, int arg);
LUALIB_API lua_Number  (luaL_optnumber) (lua_State *L, int arg, lua_Number def);
LUALIB_API lua_Integer (luaL_checkinteger) (lua_State *L, int arg);
LUALIB_API lua_Integer (luaL_optinteger) (lua_State *L, int arg, lua_Integer def);
LUALIB_API void        (luaL_checktype) (lua_State *L, int arg, int t);
LUALIB_API void        (luaL_checkany) (lua_State *L, int arg);

LUALIB_API int         (luaL_newmetatable) (lua_State *L, const char *tname);
LUALIB_API void        (luaL_setmetatable) (lua_State *L, const char *tname);
LUALIB_API void       *(luaL_testudata) (lua_State *L, int ud, const char *tname);
LUALIB_API void       *(luaL_checkudata) (lua_State *L, int ud, const char *tname);

LUALIB_API int         (luaL_checkoption) (lua_State *L, int arg, const char *def, const char *const lst[]);
LUALIB_API void        (luaL_setfuncs) (lua_State *L, const luaL_Reg *l, int nup);
LUALIB_API int         (luaL_getsubtable) (lua_State *L, int idx, const char *fname);
LUALIB_API void        (luaL_requiref) (lua_State *L, const char *modname, lua_CFunction openf, int glb);
LUALIB_API lua_Integer (luaL_len) (lua_State *L, int idx);

LUALIB_API int         (luaL_ref) (lua_State *L, int t);
LUALIB_API void        (luaL_unref) (lua_State *L, int t, int ref);

LUALIB_API int         (luaL_error) (lua_State *L, const char *fmt, ...);

/* Buffer functions */
LUALIB_API void        (luaL_buffinit) (lua_State *L, luaL_Buffer *B);
LUALIB_API char       *(luaL_prepbuffsize) (luaL_Buffer *B, size_t sz);
LUALIB_API void        (luaL_addlstring) (luaL_Buffer *B, const char *s, size_t l);
LUALIB_API void        (luaL_addstring) (luaL_Buffer *B, const char *s);
LUALIB_API void        (luaL_addvalue) (luaL_Buffer *B);
LUALIB_API void        (luaL_pushresult) (luaL_Buffer *B);
LUALIB_API void        (luaL_pushresultsize) (luaL_Buffer *B, size_t sz);
LUALIB_API char       *(luaL_buffinitsize) (lua_State *L, luaL_Buffer *B, size_t sz);

/* Compatibility functions */
#define luaL_register(L, n, l) (luaL_openlib(L, (n), (l), 0))
LUALIB_API void        (luaL_openlib) (lua_State *L, const char *libname, const luaL_Reg *l, int nup);

#ifdef __cplusplus
}
#endif

#endif // LAUXLIB_H
