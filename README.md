# Lua VM Implementation

A complete, standards-compliant Lua 5.5 implementation in C++ featuring a stack-based bytecode virtual machine, an ARM64 JIT compiler (using AsmJit), standard Lua C API & modern C++17/20 embedding API, and built-in TCP socket networking. 

The implementation is complete and robust enough to pass the official Lua 5.5.0 test suite (`all.lua`) and run **real-world Lua applications**, including a full-featured HTTP/1.1 REST web server with dynamic routing, pure-Lua JSON serialization, on-the-fly Markdown compilation, and an interactive web dashboard.

---

## Real-World Web Server Demo

The repository includes a comprehensive real-world web application showcase in [`demo/web_server.lua`](demo/web_server.lua).

### Features
* **HTTP/1.1 Protocol Engine**: Full request parser supporting HTTP methods (`GET`, `POST`, `PUT`, `DELETE`, `OPTIONS`), URI parsing, query parameter decoding (`?query=value`), HTTP headers, and JSON request bodies.
* **Express/Sinatra-Style Router**: Route registry supporting URL pattern matching, parameter capture (`/api/notes/:id`), CORS preflight handling, 404/500 error responses, and sub-millisecond request latency logging.
* **Pure-Lua Third-Party Libraries**:
  * [`rxi/json.lua`](demo/json.lua): Pure-Lua JSON encoding and decoding.
  * [`speedata/luamarkdown`](demo/markdown.lua): Pure-Lua Markdown parser that compiles Markdown documents into HTML on the fly.
* **REST API Endpoints**:
  * `GET /api/info` & `GET /api/system`: Real-time VM statistics (`collectgarbage("count")`, `_VERSION`, uptime, total request counts).
  * `GET /api/notes`: Lists stored notes with both raw Markdown and compiled HTML.
  * `POST /api/notes`: Creates a new note from a JSON payload (`title`, `content`).
  * `GET /api/notes/:id` & `DELETE /api/notes/:id`: Retrieves or deletes a note by ID.
  * `POST /api/render`: Compiles arbitrary Markdown text into HTML dynamically.
  * `GET /api/echo`: Echoes request method, headers, and query parameters.
* **Interactive Web Dashboard (`GET /`)**: A modern, responsive single-page application (SPA) with CSS and vanilla JS featuring real-time VM memory and uptime cards, a Markdown document editor with instant HTML preview, and document management.

### Running the Web Server

```bash
# Build the Lua binary
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# Start the web server
./build/lua demo/web_server.lua
```

Once running, visit `http://127.0.0.1:8080/` in your browser, or test the API via `curl`:

```bash
# Check server & VM metrics
curl -s http://127.0.0.1:8080/api/info

# Create a Markdown note
curl -s -X POST http://127.0.0.1:8080/api/notes \
  -H "Content-Type: application/json" \
  -d '{"title": "Note 1", "content": "# Hello World\nRunning on **C++ Lua VM**!"}'

# Compile Markdown on the fly
curl -s -X POST http://127.0.0.1:8080/api/render \
  -H "Content-Type: application/json" \
  -d '{"markdown": "## Dynamic Rendering\n* Fast\n* Pure Lua"}'
```

---

## Features

### Core Engine & VM
- **Stack-based Virtual Machine**: High-performance bytecode interpreter with optimized execution loop.
- **JIT Compilation (ARM64)**: Hotspot-detecting JIT compiler powered by AsmJit that compiles hot Lua functions directly to native machine code.
- **NaN-boxing Values**: 64-bit value representation supporting nil, boolean, integer, float, function, string, table, userdata, thread, and socket types.
- **Garbage Collection**: Mark-and-sweep automatic memory management with support for incremental GC, emergency collection, and weak tables (ephemerons, `__mode = "k"`, `"v"`).
- **Coroutines & First-Class Threads**: Full support for `coroutine.create`, `resume`, `yield`, `status`, `running`, and `coroutine.close` (Lua 5.4).
- **Metatables & Metamethods**: Complete metamethod coverage:
  - **Arithmetic**: `__add`, `__sub`, `__mul`, `__div`, `__idiv`, `__mod`, `__pow`, `__unm`
  - **Bitwise**: `__band`, `__bor`, `__bxor`, `__bnot`, `__shl`, `__shr`
  - **Comparison**: `__eq`, `__lt`, `__le`
  - **Object & Lifecycle**: `__index`, `__newindex`, `__call`, `__concat`, `__len`, `__tostring`, `__gc`, `__close`

### Language Syntax & Lua 5.5 Features
- **Local & Global Variables**: Lexical block scoping with shadowing, multi-assignment (`a, b = x, y`), and `_ENV` upvalue resolution.
- **Attribute Variables**: `<const>` variables and `<close>` to-be-closed variables with deterministic `__close` metamethod escalation.
- **Lua 5.5 Global Declarations**: `global *`, `global none`, `global <const> *`, and initialized global bindings (`global a = 1, b = 2`).
- **Lua 5.5 Named Varargs**: Direct vararg naming (`function f(a, ...t)`) where `t` is bound as a table containing all extra arguments and field `t.n`.
- **Control Flow**:
  - `if ... then ... elseif ... else ... end`
  - `while ... do ... end`, `repeat ... until ...`
  - Numeric for (`for i = 1, 10, 2 do`) and Generic for (`for k, v in pairs(t) do`)
  - `goto label` and `::label::` (Lua 5.2+)
  - Explicit scoping `do ... end` and early loop `break`
- **Functions & Closures**:
  - Closures capturing upvalues across arbitrary lexical scopes
  - Variadic functions with `...` expressions
  - Multiple return values with proper truncation and expansion
- **Module System**:
  - Standard `require(modname)` with `package.loaded`, `package.preload`, and configurable `package.path` searchers.
  - Native C shared library loader (`package.loadlib`) supporting C modules and Luarocks packages.

### Modern C++ Embedding API & Standard C API
- **Header-Only C++ Embedding API (`include/lua/lua.hpp`)**:
  - Type-safe, RAII-managed `lua::Context` wrapping the VM state.
  - Automatic stack marshaling and unmarshaling for primitive types (`int`, `double`, `std::string`, `bool`), STL containers (`std::vector`, `std::map`, `std::unordered_map`, `std::optional`), and multi-return tuples (`std::tuple`).
  - Seamless binding of C++ lambdas and callables with automatic lifetime management and garbage collection finalization.
  - Ergonomic table and global proxy indexing (`ctx["my_table"]["key"] = value`).
- **Standard C API**:
  - Standard headers in `include/` (`lua.h`, `lauxlib.h`, `lualib.h`, `luaconf.h`, `lua.hpp`).
  - Full support for `lua_State`, stack manipulation, table operations, protected calls (`lua_pcall`), and chunk loaders (`luaL_loadstring`, `luaL_loadbufferx`, `luaL_loadfilex`, `luaL_dostring`, `luaL_dofile`).
  - Compiles as a shared library `liblua` for dynamic embedding and C extension modules.

### Standard Libraries
- **`socket`**: Built-in Berkeley TCP socket primitives (`create`, `bind`, `listen`, `accept`, `send`, `receive`, `close`).
- **`string`**: `len`, `sub`, `upper`, `lower`, `reverse`, `byte`, `char`, `rep`, `format`, `pack`, `unpack`, `packsize`, and full pattern-matching engines (`find`, `match`, `gmatch`, `gsub`).
- **`table`**: `insert`, `remove`, `move`, `sort`, `concat`, `pack`, `unpack`, and Lua 5.5 `table.create(nseq, nrec)` with capacity preallocation.
- **`math`**: `abs`, `floor`, `ceil`, `sqrt`, `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `exp`, `log`, `deg`, `rad`, `min`, `max`, `random`, `randomseed`, `tointeger`, `type`, `ult`, integer limits (`maxinteger`, `mininteger`), and `pi`.
- **`os`**: `clock`, `time`, `date`, `difftime`, `getenv`, `execute`, `remove`, `rename`, `exit`, `setlocale`.
- **`io`**: Standard streams (`stdin`, `stdout`, `stderr`), `open`, `close`, `read`, `write`, `lines`, `flush`, `popen`.
- **`utf8`**: `char`, `charpattern`, `codes`, `codepoint`, `len`, `offset` (supporting dual-return index ranges and lax mode).
- **`debug`**: `getinfo`, `getlocal`, `setlocal`, `getupvalue`, `setupvalue`, `upvalueid`, `upvaluejoin`, `traceback`, `sethook`.

---

## Building

### Prerequisites
- CMake 3.10+
- C++17 compatible compiler (Clang, GCC, or Apple Clang)

### Build Steps

```bash
mkdir build
cd build
cmake ..
cmake --build . -j
```

---

## Running Tests

The test suite covers compiler edge cases, metamethods, standard library functions, closures, coroutines, memory management, JIT compilation, and C/C++ embedding:

```bash
# Run all tests via CMake / CTest
cmake --build build --target test

# Run 208 internal Lua test suite cases
bash tests/run_all_tests.sh "$PWD/build/lua"

# Run C API integration tests
./build/test_c_api

# Run Modern C++17/20 Embedding API integration tests
./build/test_cpp_api

# Run Modern C++ Embedding demo
./build/embedding_demo

# Run official Lua 5.5.0 test suite (100% PASS - final OK !!!)
(cd lua-5.5.0-tests && ../build/lua --nojit all.lua)

# Run performance benchmark suite
cmake --build build --target benchmark
# (or directly: bash benchmarks/run.sh)

# Run CLI and disassembler tests
bash tests/test_cli_flags.sh
bash tests/test_disasm_metadata.sh
bash tests/test_repl_logic.sh
```

---

## Modern C++ Embedding API

The modern C++ interface lives in the single include `<lua/lua.hpp>`:

```cpp
#include <lua/lua.hpp>
#include <iostream>
#include <vector>

int main() {
    lua::Context ctx;
    ctx.openLibs();

    // 1. Bind C++ lambda
    ctx.bind("add", [](double a, double b) {
        return a + b;
    });

    // 2. Execute script
    ctx.execute("result = add(10, 25)");
    std::cout << "Result: " << ctx.get<double>("result") << "\n"; // 35

    // 3. Return multiple values via std::tuple
    ctx.bind("divmod", [](int a, int b) {
        return std::make_tuple(a / b, a % b);
    });

    // 4. Pass and return STL containers
    ctx.bind("double_all", [](const std::vector<int>& items) {
        std::vector<int> res;
        for (int v : items) res.push_back(v * 2);
        return res;
    });

    ctx.execute(R"(
        q, r = divmod(17, 5)
        doubled = double_all({1, 2, 3, 4})
    )");

    auto vec = ctx.get<std::vector<int>>("doubled"); // {2, 4, 6, 8}
    return 0;
}
```

---

## Usage

### Run a Lua File
```bash
./build/lua script.lua
```

### Interactive REPL
```bash
./build/lua
```
Features:
- **Tab Auto-completion**: Auto-complete globals, keywords, and table fields (`math.s<TAB>`).
- **Persistent History**: Command history preserved across sessions.
- **Multi-line Continuation**: Automatic detection of open blocks with `>>` continuation prompt.
- **Expression Evaluation**: Direct expressions like `2 + 2` evaluate and print automatically.
- **Meta-commands**: `=expr`, `globals`, and `help`.

### Bytecode Disassembly & Compiler (`luac`)
```bash
# Disassemble bytecode directly
./build/lua -L script.lua

# Compile Lua script to binary bytecode (default: luac.out)
./build/luac script.lua

# Compile with custom output file and stripped debug information
./build/luac -s -o compiled.luac script.lua

# Disassemble bytecode using luac
./build/luac -l script.lua

# Syntax check only without emitting output
./build/luac -p script.lua

# Run compiled bytecode directly with the VM
./build/lua compiled.luac
```

---

## Documentation & Roadmaps

- [Documentation Index](docs/INDEX.md): Overview of architecture, value representation, GC, and standard libraries.
- [C API & Modern C++ Embedding API](docs/C_API.md): Standard C API, dynamic shared library, and modern C++17/20 `lua::Context` embedding interface.
- [Lua 5.5 Compliance Plan & Status](docs/LUA_5_5_COMPLIANCE_PLAN.md): Architectural gap analysis, test suite diagnostic matrix, and implementation roadmap for full Lua 5.5 compliance.
- [JIT Compilation Plan & Status](docs/JIT_COMPILATION_PLAN.md): Design and implementation of the ARM64 template JIT compiler.

---

## License

MIT License - see [LICENSE](LICENSE) file for details.
