# C API Documentation

The Lua VM provides a Lua 5.4 compatible C API, allowing C++ code to interact with the VM, manipulate the stack, and register new functions.

## The `lua_State`

The central structure is `lua_State`, which wraps the `VM` and tracks current execution context.

- **Stack Indexing**: The API supports standard 1-based relative indexing (1 is the first argument, -1 is the top of the stack).
- **Context Awareness**: `lua_State` tracks `stackBase` and `argCount`, ensuring that C functions only see their own arguments when using relative indices.

## Core API Functions

### Stack Manipulation
- `lua_pushnumber(L, n)`: Pushes a double onto the stack.
- `lua_pushinteger(L, n)`: Pushes a 64-bit integer.
- `lua_pushstring(L, s)`: Interns and pushes a string.
- `lua_pop(L, n)`: Pops `n` elements.
- `lua_gettop(L)`: Returns the number of elements on the stack.

### Accessors
- `lua_tonumber(L, idx)`: Converts value at index to double.
- `lua_tointeger(L, idx)`: Converts to 64-bit integer.
- `lua_tostring(L, idx)`: Returns C string representation.
- `lua_type(L, idx)`: Returns type tag (LUA_TNUMBER, LUA_TSTRING, etc.).

### Function Calls
- `lua_pcall(L, nargs, nres, msgh)`: Calls a function in protected mode.
- `lua_call(L, nargs, nres)`: Calls a function (unprotected).

## Registering C Functions

C functions must follow the signature:
`int (*lua_CFunction) (lua_State *L)`

Example:
```cpp
int my_add(lua_State* L) {
    double a = lua_tonumber(L, 1);
    double b = lua_tonumber(L, 2);
    lua_pushnumber(L, a + b);
    return 1; // Number of results
}

// Registration
lua_pushcfunction(L, my_add);
lua_setglobal(L, "my_add");
```

## Internal Dispatch

When `OP_CALL` encounters a value of type `TAG_C_FUNCTION`:
1. It creates a temporary `lua_State` on the C++ stack.
2. It sets `stackBase` to point to the first argument.
3. It calls the `lua_CFunction`.
4. It captures the return value (number of results) and moves them to the Lua stack, cleaning up the arguments.

## Header Files
- `src/api/lua.h`: The public C-style header.
- `src/api/lua_state.h`: Internal state definition.
- `src/api/lua_api.cpp`: Implementation of the API functions.
