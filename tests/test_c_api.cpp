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
    lua_settop(L, 0); // Clean stack
    
    // Stack manipulation tests
    lua_pushnumber(L, 1);
    lua_pushnumber(L, 2);
    lua_pushnumber(L, 3);
    // Stack: [1, 2, 3]
    
    lua_remove(L, -2);
    // Stack: [1, 3]
    assert(lua_gettop(L) == 2);
    assert(lua_tonumber(L, 1) == 1);
    assert(lua_tonumber(L, 2) == 3);
    
    lua_pushnumber(L, 2);
    lua_insert(L, 2);
    // Stack: [1, 2, 3]
    assert(lua_tonumber(L, 2) == 2);
    assert(lua_tonumber(L, 3) == 3);
    
    lua_pushnumber(L, 4);
    lua_replace(L, 2);
    // Stack: [1, 4, 3]
    assert(lua_tonumber(L, 2) == 4);
    assert(lua_gettop(L) == 3);
    
    lua_settop(L, 0);
    
    // Test lua_next
    lua_newtable(L);
    lua_pushstring(L, "a");
    lua_pushnumber(L, 10);
    lua_settable(L, -3);
    lua_pushstring(L, "b");
    lua_pushnumber(L, 20);
    lua_settable(L, -3);
    
    lua_pushnil(L);
    int count = 0;
    while (lua_next(L, -2)) {
        // key at -2, value at -1
        assert(lua_isstring(L, -2));
        assert(lua_isnumber(L, -1));
        lua_pop(L, 1); // pop value, keep key for next iteration
        count++;
    }
    assert(count == 2);
    
    lua_close(L);
    std::cout << "C API tests passed!" << std::endl;
    return 0;
}
