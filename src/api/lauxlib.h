#ifndef LAUXLIB_H
#define LAUXLIB_H

#include "lua.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct luaL_Reg {
    const char *name;
    lua_CFunction func;
} luaL_Reg;

#define luaL_checkversion(L) ((void)0)

void luaL_setfuncs(lua_State *L, const luaL_Reg *l, int nup);

#define luaL_newlibtable(L, l) \
    lua_createtable(L, 0, sizeof(l)/sizeof((l)[0]) - 1)

#define luaL_newlib(L, l) \
    (luaL_newlibtable(L, l), luaL_setfuncs(L, l, 0))

const char *luaL_checklstring(lua_State *L, int arg, size_t *l);
#define luaL_checkstring(L, n) (luaL_checklstring(L, (n), NULL))

int luaL_ref(lua_State *L, int t);
void luaL_unref(lua_State *L, int t, int ref);

#ifdef __cplusplus
}
#endif

#endif // LAUXLIB_H
