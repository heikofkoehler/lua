#ifndef LUACONF_H
#define LUACONF_H

#if defined(_WIN32)
  #if defined(LUA_BUILD_AS_DLL)
    #define LUA_API __declspec(dllexport)
  #elif defined(LUA_USE_DLL)
    #define LUA_API __declspec(dllimport)
  #else
    #define LUA_API extern
  #endif
#else
  #if defined(__GNUC__) && ((__GNUC__ >= 4) || defined(__clang__))
    #define LUA_API __attribute__((visibility("default"))) extern
  #else
    #define LUA_API extern
  #endif
#endif

#define LUALIB_API LUA_API
#define LUAMOD_API LUA_API
#define LUAI_FUNC extern
#define LUAI_DDEC extern
#define LUAI_DDEF

#define LUA_INTEGER_FMT "%lld"
#define LUA_NUMBER_FMT  "%.14g"

#endif // LUACONF_H
