#ifndef LUA_STATE_H
#define LUA_STATE_H

#include "vm/vm.hpp"

struct lua_State {
    VM* vm;
    bool is_owned;
    size_t stackBase;
    int argCount;
};

#endif // LUA_STATE_H
