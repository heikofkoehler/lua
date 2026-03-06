# Standard Library Implementation

The Lua VM implements a significant portion of the Lua 5.4 standard library. Native functions are implemented in C++ and registered into global tables during VM initialization.

## Architecture

- **Native Function Signature**: `bool (*)(VM* vm, int argCount)`
- **Calling Convention**: Arguments are on the VM stack. The function is responsible for popping arguments and pushing results. It returns `true` on success and `false` on runtime error.
- **Namespaces**: Most functions are organized into tables like `math`, `string`, `table`, `io`, `os`, `debug`, `coroutine`, `utf8`, and `socket`.

## Implemented Libraries

### 1. Base Library (`_G`)
- `print(...)`: Variadic print to stdout.
- `type(v)`: Returns type name string.
- `tostring(v)`, `tonumber(v)`: Conversion functions.
- `pcall(f, ...)`, `xpcall(f, msgh, ...)`: Protected calls.
- `assert(v, [msg])`: basic assertion.
- `error(msg, [level])`: Raises a runtime error.
- `collectgarbage([opt])`: Interface to the GC.
- `require(modname)`: Module loading system.
- `load(chunk, [name, mode, env])`: Dynamic compilation.

### 2. String Library (`string`)
- Full Lua **Pattern Matching** support: `find`, `match`, `gmatch`, `gsub`.
- Formatting: `format` (supports %s, %d, %f, %x, %q).
- Binary packing: `pack`, `unpack`, `packsize`.
- Utilities: `sub`, `len`, `byte`, `char`, `upper`, `lower`, `reverse`.

### 3. Table Library (`table`)
- `insert`, `remove`, `concat`.
- `pack`, `unpack` (multi-return support).
- `sort` (custom comparator support).
- `move`.

### 4. Math Library (`math`)
- Complete trig/log suite: `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `exp`, `log`.
- Utilities: `sqrt`, `abs`, `floor`, `ceil`, `min`, `max`, `random`, `randomseed`.
- Constants: `pi`, `huge`, `maxinteger`, `mininteger`.

### 5. I/O Library (`io` and `file`)
- **Multi-format reading**: `io.read` and `file:read` support "l", "L", "a", and specific byte counts (e.g., `f:read(4)`).
- File objects with method-call syntax: `f:write()`, `f:close()`, `f:seek()`, `f:lines()`.
- Pipes: `io.popen`.

### 6. OS Library (`os`)
- `os.date`, `os.time`, `os.difftime`.
- `os.execute`, `os.getenv`, `os.remove`, `os.rename`.
- `os.setlocale` (full category support).

### 7. Debug Library (`debug`)
- `debug.sethook`, `debug.gethook`.
- `debug.traceback`, `debug.getinfo`.
- `debug.getmetatable`, `debug.setmetatable` (can set metatables for non-table types).

### 8. Coroutine Library (`coroutine`)
- `create`, `resume`, `yield`, `status`, `close`, `running`.

### 9. UTF-8 Library (`utf8`)
- `char`, `codes`, `codepoint`, `len`, `offset`.

### 10. Socket Library (`socket`)
- Experimental built-in socket support for basic networking (TCP/UDP).

## Implementation Details

### Dot Notation
The VM supports full dot notation for table access (`math.sin(1)`) and method calls (`file:read()`). The parser and code generator handle these by emitting `OP_GET_TABLE` and `OP_SELF` instructions.

### String Interning
Native functions use interned strings for keys to ensure O(1) lookup in namespaces. Strings are interned globally across the VM state.

### Error Handling
Native functions should use `vm->runtimeError(message)` to report issues. This ensures the VM state is consistent and `pcall` can catch the error.
