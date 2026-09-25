#ifndef LUA_STATE_H
#define LUA_STATE_H

#include "vm/vm.hpp"

class ClosureObject;

struct lua_State {
    VM* vm;
    bool is_owned;
    size_t stackBase;
    int argCount;
    ClosureObject* currentClosure = nullptr;
    Value registryVal = Value::nil();
};

#endif // LUA_STATE_H
