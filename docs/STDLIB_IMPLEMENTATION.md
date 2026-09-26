# Standard Library Implementation

The Lua VM implements the full Lua 5.5 standard library specification. Native functions are implemented in C++ and registered into global tables during VM initialization.

## Architecture

- **Native Function Signature**: `bool (*)(VM* vm, int argCount)`
- **Calling Convention**: Arguments are on the VM stack. The function is responsible for popping arguments and pushing results. It returns `true` on success and `false` on runtime error.
- **Namespaces**: Functions are organized into tables: `math`, `string`, `table`, `io`, `os`, `debug`, `coroutine`, `utf8`, `socket`, and the global environment `_G`.

## Implemented Libraries

### 1. Base Library (`_G`)
- `print(...)`: Variadic print to stdout.
- `type(v)`: Returns type name string.
- `tostring(v)`, `tonumber(v)`: Conversion functions.
- `pcall(f, ...)`, `xpcall(f, msgh, ...)`: Protected calls.
- `assert(v, [msg])`: Basic assertion.
- `error(msg, [level])`: Raises a runtime error.
- `collectgarbage([opt, ...])`: Interface to the GC (supports `"collect"`, `"stop"`, `"restart"`, `"count"`, `"step"`, `"isrunning"`, `"incremental"`, `"generational"`, `"param"`).
- `require(modname)`: Module loading system with `package.path` and C shared library loaders.
- `load(chunk, [name, mode, env])`: Dynamic compilation from strings or reader functions.
- `dofile([filename])`: Load and execute Lua file.
- `warn(msg1, ...)`: Warning emission system.
- `select(index, ...)`: Return or count variadic arguments.
- `pairs(t)`, `ipairs(t)`: Generic table iterators.
- `next(t, [k])`: Primitive table iterator.
- `rawget(t, k)`, `rawset(t, k, v)`, `rawequal(v1, v2)`, `rawlen(v)`: Metamethod-bypassing accessors.
- `getmetatable(v)`, `setmetatable(t, mt)`: Metatable management.

### 2. String Library (`string`)
- Full Lua **Pattern Matching** support: `find`, `match`, `gmatch`, `gsub`.
- Formatting: `format` (supports `%s`, `%d`, `%f`, `%x`, `%q`, `%p`, etc.).
- Binary packing: `pack`, `unpack`, `packsize` (supports integer sizes `1..16`, `sizeof(int)`, `sizeof(long)`, alignments `!n`, fixed/null-terminated/length-prefixed strings, IEEE floats).
- Utilities: `sub`, `len`, `byte`, `char`, `upper`, `lower`, `reverse`, `rep`, `dump`.

### 3. Table Library (`table`)
- `insert`, `remove`, `concat`, `move`.
- `pack`, `unpack` (multi-return support).
- `sort` (custom comparator support).
- `create(nseq, nrec)`: Lua 5.5 preallocated sequence and hash table initialization.

### 4. Math Library (`math`)
- Complete trig/log suite: `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `exp`, `log`, `deg`, `rad`.
- Utilities: `sqrt`, `abs`, `floor`, `ceil`, `min`, `max`, `random`, `randomseed`, `fmod`.
- Integer utilities: `tointeger`, `type`, `ult`.
- Constants: `pi`, `huge`, `maxinteger`, `mininteger`.

### 5. I/O Library (`io` and `file`)
- **Multi-format reading**: `io.read` and `file:read` support `"l"`, `"L"`, `"a"`, `"n"`, and specific byte counts (e.g., `f:read(4)`).
- File objects with method-call syntax: `f:write()`, `f:close()`, `f:seek()`, `f:lines()`, `f:flush()`.
- Standard streams: `io.stdin`, `io.stdout`, `io.stderr`, `io.input`, `io.output`.
- Pipes: `io.popen`.

### 6. OS Library (`os`)
- `os.date`, `os.time`, `os.difftime`.
- `os.execute`, `os.getenv`, `os.remove`, `os.rename`, `os.tmpname`.
- `os.setlocale` (full category support).
- `os.clock`, `os.exit`.

### 7. Debug Library (`debug`)
- `debug.sethook`, `debug.gethook`.
- `debug.traceback`, `debug.getinfo`.
- `debug.getlocal`, `debug.setlocal`, `debug.getupvalue`, `debug.setupvalue`, `debug.upvalueid`, `debug.upvaluejoin`.
- `debug.getmetatable`, `debug.setmetatable` (sets metatables for all types).

### 8. Coroutine Library (`coroutine`)
- `create`, `resume`, `yield`, `status`, `close`, `running`, `isyieldable`, `wrap`.

### 9. UTF-8 Library (`utf8`)
- `char`, `codes`, `codepoint`, `len`, `offset`.
- Lua 5.5 compliant: `utf8.offset` returns dual indices (inclusive start and end), lax mode option for lenient UTF-8 parsing, and validation up to `0x10FFFF`.

### 10. Socket Library (`socket`)
- Built-in Berkeley TCP socket primitives (`create`, `bind`, `listen`, `accept`, `send`, `receive`, `close`).

## Implementation Details

### Dot Notation
The VM supports full dot notation for table access (`math.sin(1)`) and method calls (`file:read()`). The parser and code generator handle these by emitting `OP_GET_TABLE` and `OP_SELF` instructions.

### String Interning
Native functions use interned strings for keys to ensure O(1) lookup in namespaces. Strings are interned globally across the VM state.

### Error Handling
Native functions should use `vm->runtimeError(message)` to report issues. This ensures the VM state is consistent and `pcall` can catch the error.
