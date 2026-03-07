#include "api/lua.h"
#include <iostream>
#include <cassert>
#include <cstring>

int main() {
    lua_State* L = lua_newstate();
    
    // Test push and type
    lua_pushnumber(L, 42.5);
    assert(lua_isnumber(L, -1));
    assert(lua_type(L, -1) == LUA_TNUMBER);
    assert(lua_tonumber(L, -1) == 42.5);
    
    lua_pushinteger(L, 100);
    assert(lua_isnumber(L, -1));
    assert(lua_tointeger(L, -1) == 100);
    
    lua_pushstring(L, "hello API");
    assert(lua_isstring(L, -1));
    assert(std::strcmp(lua_tostring(L, -1), "hello API") == 0);
    
    lua_pushboolean(L, 1);
    assert(lua_isboolean(L, -1));
    assert(lua_toboolean(L, -1) == 1);
    
    // Stack manipulation
    assert(lua_gettop(L) == 4);
    lua_pop(L, 1);
    assert(lua_gettop(L) == 3);
    assert(lua_isstring(L, -1)); // "hello API" is now on top
    
    // pushvalue
    lua_pushvalue(L, 1); // duplicate 42.5
    assert(lua_gettop(L) == 4);
    assert(lua_tonumber(L, -1) == 42.5);
    
    // C Function test
    auto my_cfunc = [](lua_State* L) -> int {
        // arg 1 should be 10
        int top = lua_gettop(L);
        double n = lua_tonumber(L, 1);
        std::cout << "DEBUG cfunc: top=" << top << " arg1=" << n << std::endl;
        lua_pushnumber(L, n * 2);
        return 1;
    };
    lua_pushcfunction(L, my_cfunc);
    lua_pushnumber(L, 10);
    lua_pcall(L, 1, 1, 0);
    
    // Result should be 20
    std::cout << "DEBUG post-call: top=" << lua_gettop(L) << " result=" << lua_tonumber(L, -1) << std::endl;
    assert(lua_tonumber(L, -1) == 20);
    
    // Table tests
    lua_newtable(L);
    assert(lua_istable(L, -1));
    
    lua_pushstring(L, "key");
    lua_pushstring(L, "value");
    lua_settable(L, -3); // t["key"] = "value"
    
    lua_pushstring(L, "key");
    lua_gettable(L, -2); // t["key"]
    assert(lua_isstring(L, -1));
    assert(std::strcmp(lua_tostring(L, -1), "value") == 0);
    lua_pop(L, 1);
    
    lua_pushnumber(L, 123);
    lua_setfield(L, -2, "field"); // t["field"] = 123
    
    lua_getfield(L, -1, "field");
    assert(lua_tonumber(L, -1) == 123);
    lua_pop(L, 1);
    
    // Metatable tests
    lua_newtable(L); // new mt
    lua_pushstring(L, "meta");
    lua_setfield(L, -2, "name");
    
    lua_setmetatable(L, -2); // set mt for table t
    
    assert(lua_getmetatable(L, -1));
    lua_getfield(L, -1, "name");
    assert(std::strcmp(lua_tostring(L, -1), "meta") == 0);
    lua_pop(L, 2); // pop field and mt
    
    // Userdata tests
    void* data = lua_newuserdata(L, 100);
    assert(data != nullptr);
    assert(lua_isuserdata(L, -1));
    assert(lua_touserdata(L, -1) == data);
    
    // Library tests
    luaL_openlibs(L);
    lua_getglobal(L, "math");
    assert(lua_istable(L, -1));
    lua_getfield(L, -1, "sqrt");
    assert(lua_isfunction(L, -1));
    lua_pushnumber(L, 16);
    lua_pcall(L, 1, 1, 0);
    assert(lua_tonumber(L, -1) == 4.0);
    
    lua_close(L);
    std::cout << "C API tests passed!" << std::endl;
    return 0;
}
