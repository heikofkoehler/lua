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
- `lua_touserdata(L, idx)`: Returns pointer to userdata payload.
- `lua_type(L, idx)`: Returns type tag (LUA_TNUMBER, LUA_TSTRING, etc.).

### Table Manipulation
- `lua_newtable(L)`: Creates a new empty table.
- `lua_gettable(L, idx)`: Pushes `t[k]`, where `t` is at `idx` and `k` is at the top of the stack.
- `lua_settable(L, idx)`: Does `t[k] = v`, where `t` is at `idx`, `v` is at the top, and `k` is below `v`.
- `lua_getfield(L, idx, k)`: Pushes `t[k]`.
- `lua_setfield(L, idx, k)`: Does `t[k] = v`, where `v` is at the top.
- `lua_rawget(L, idx)` / `lua_rawset(L, idx)`: Raw versions of table access.

### Metatables
- `lua_getmetatable(L, idx)`: Pushes the metatable of the value at `idx`.
- `lua_setmetatable(L, idx)`: Sets the table at the top of the stack as the metatable for the value at `idx`.

### Userdata
- `lua_newuserdata(L, size)`: Allocates a new block of memory and pushes it as userdata.

### Chunk Loading & Execution
- `luaL_loadstring(L, s)`: Compiles a string buffer as a Lua chunk and leaves the compiled function on the stack. Returns `LUA_OK` or error code.
- `luaL_loadbufferx(L, buff, sz, name, mode)`: Compiles a memory buffer with a custom chunk name and mode.
- `luaL_loadfilex(L, filename, mode)`: Compiles a file from disk and pushes the resulting function.
- `luaL_dostring(L, s)`: Convenience macro that loads and executes a string via `lua_pcall`.
- `luaL_dofile(L, filename)`: Convenience macro that loads and executes a file via `lua_pcall`.

### Auxiliary Functions
- `luaL_openlibs(L)`: Initializes all standard Lua libraries (`_G`, `string`, `table`, `math`, `io`, `os`, `debug`, `utf8`, `coroutine`).
- `luaL_newstate()`: Allocates and initializes a new `lua_State`.
- `lua_close(L)`: Closes and releases resources associated with a `lua_State`.

### Function Calls
- `lua_pcall(L, nargs, nres, msgh)`: Calls a function in protected mode.
- `lua_call(L, nargs, nres)`: Calls a function (unprotected).

## Registering C Functions

C functions must follow the standard Lua signature:
```c
int (*lua_CFunction) (lua_State *L);
```

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
1. It creates an internal `lua_State` on the C++ stack referencing the active `VM` context.
2. It sets `stackBase` to point to the first argument.
3. It invokes the `lua_CFunction`.
4. It captures the return count and transfers returned values to the Lua VM stack, cleaning up the call frame.

---

## Modern C++17/20 Embedding API (`lua::Context`)

The project provides a header-only modern C++ embedding API in `include/lua/lua.hpp` under the `lua` namespace.

### Key Capabilities
- **RAII Lifecycle**: `lua::Context` automatically creates `lua_State` on construction and safely tears it down via `lua_close` on destruction.
- **Type-Safe Marshaling**: Automatic conversion between C++ and Lua types via `lua::push` and `lua::get<T>`:
  - Primitives: `bool`, integers (`int`, `int64_t`, `size_t`), floating point (`float`, `double`), string types (`const char*`, `std::string`, `std::string_view`).
  - Containers: `std::vector<T>` maps to sequence tables, `std::map<K, V>` and `std::unordered_map<K, V>` map to associative tables.
  - Optionals: `std::optional<T>` converts `nil` to `std::nullopt` and values to `std::make_optional`.
  - Multiple Returns: `std::tuple<Args...>` unpacks into multiple return values on the Lua stack.
- **Direct Lambda Binding**: `ctx.bind(name, callable)` binds arbitrary C++ lambdas, functions, or functors with type-checked argument unmarshaling and GC finalizers.
- **Table & Global Proxies**: Ergonomic nested indexing `ctx["user"]["name"] = "Alice"`.

### Modern C++ Example

```cpp
#include <lua/lua.hpp>
#include <iostream>
#include <vector>
#include <tuple>

int main() {
    lua::Context ctx;
    ctx.openLibs();

    // Bind lambda returning multiple values
    ctx.bind("stats", [](const std::vector<double>& values) {
        double sum = 0.0;
        double min_val = values.empty() ? 0.0 : values[0];
        double max_val = min_val;
        for (double v : values) {
            sum += v;
            if (v < min_val) min_val = v;
            if (v > max_val) max_val = v;
        }
        double avg = values.empty() ? 0.0 : sum / values.size();
        return std::make_tuple(avg, min_val, max_val);
    });

    // Execute script
    ctx.execute(R"(
        avg, min_v, max_v = stats({10.5, 20.0, 30.5, 40.0})
    )");

    std::cout << "Avg: " << ctx.get<double>("avg") << "\n";
    std::cout << "Min: " << ctx.get<double>("min_v") << "\n";
    std::cout << "Max: " << ctx.get<double>("max_v") << "\n";
    return 0;
}
```

---

## Distribution Headers & Shared Library

### Headers (`include/`)
- `include/lua.h`: Standard C API definitions and function declarations.
- `include/lauxlib.h`: Auxiliary library functions (`luaL_*`).
- `include/lualib.h`: Standard library open functions (`luaL_openlibs`).
- `include/luaconf.h`: Configuration macros and platform-specific definitions.
- `include/lua.hpp`: Standard C++ wrapper including `lua.h`, `lauxlib.h`, and `lualib.h` inside `extern "C"`.
- `include/lua/lua.hpp`: Modern C++17/20 header-only embedding API (`lua::Context`).

### Shared Library
The build generates `liblua.dylib` (macOS) or `liblua.so` (Linux). Third-party C modules and Luarocks packages link against this library or dynamically bind symbols via `package.loadlib`.
