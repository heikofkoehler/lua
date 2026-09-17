#include "api/lua.h"

extern "C" {

#ifdef _WIN32
__declspec(dllexport)
#endif
int dummy_test_function(lua_State* L) {
    lua_pushnumber(L, 42.0);
    return 1;
}

}
