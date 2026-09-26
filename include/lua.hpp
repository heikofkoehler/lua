// lua.hpp
// Lua header files for C++
// Supports both standard C API inclusion and modern C++17 embedding API (namespace lua)

#ifndef LUA_HPP_ROOT
#define LUA_HPP_ROOT

extern "C" {
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}

#include "lua/lua.hpp"

#endif // LUA_HPP_ROOT
