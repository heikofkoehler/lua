# Lua 5.5 Compliance: Gap Analysis and Implementation Plan

## 1. Executive Summary

This document outlines the architectural gap analysis, root cause diagnoses, and phased implementation roadmap to bring the custom C++ Lua virtual machine into full compliance with the **Lua 5.5 specification**, with the ultimate acceptance criteria being the successful execution of the official test suite (`lua-5.5.0-tests/all.lua`).

### Current Status
- Current VM Version: `_VERSION = "Lua 5.5"`
- Existing Internal Test Suite: **207 / 207 tests passing** (100% pass rate across lexer, parser, codegen, VM, stdlib, GC, and C API).
- Real-World Validation: Successfully executes complex multi-file applications including the `http.lua` HTTP/1.1 web server with coroutines, sockets, and standard libraries.
- Official Lua 5.5 Test Suite: **3 / 34 test files passing out of the box** (`bwcoercion.lua`, `code.lua`, `tracegc.lua`). The remaining tests encounter specific, well-isolated failure classes.

---

## 2. Test Suite Diagnostic Matrix

Each test file in `lua-5.5.0-tests/` was systematically evaluated against `./build/lua`. The table below details the observed status, primary symptom, and underlying root cause:

| Test File | Status | Primary Symptom | Root Cause Category |
| :--- | :--- | :--- | :--- |
| `all.lua` | FAIL | Master test runner; fails on sub-tests | Cascade from child test suites |
| `api.lua` | FAIL | `:511: Too many constants in one chunk` | Constant pool 8-bit limit (8-bit operand) |
| `attrib.lua` | FAIL | `attrib.lua:35: assertion failed!` | `package.searchpath` only replaces first `?` |
| `big.lua` | TIMEOUT | Stress test with very large table/code sizes | Constant limits + non-JIT iteration overhead |
| `bitwise.lua` | FAIL | `bitwise.lua:12: assertion failed!` | `math.mininteger` 32-bit; C++ arithmetic vs logical shifts |
| `bwcoercion.lua`| **PASS** | Completed successfully | Fully compliant bitwise/string coercion |
| `calls.lua` | FAIL | `calls.lua:27: Expected expression near '='` | Global declaration syntax: `global fact = false` |
| `closure.lua` | TIMEOUT | Hangs in `while x[1] do` loop | Weak tables: `blackenObject` marks all keys/values |
| `code.lua` | **PASS** | Completed successfully | Bytecode optimizer & constant folding compliant |
| `constructs.lua`| FAIL | `assertion failed: checkload("for x do", "expected")` | Syntax error message phrasing; combinatorial explosion |
| `coroutine.lua`| FAIL | `coroutine.lua:292: Expected '*' or variable name...` | Global declaration attribute syntax: `global <const> *` |
| `cstack.lua` | FAIL | Uncaught C++ `RuntimeError` during `__close` | Exception propagation across protected call boundaries |
| `db.lua` | FAIL | `db.lua:394: Expected '*' or variable name...` | Global declaration syntax: `global *` in inner block |
| `errors.lua` | FAIL | `:537: Too many constants in one chunk` | Constant pool 8-bit limit in error-generating chunks |
| `events.lua` | FAIL | `:10: attempt to assign to const variable '_ENV'` | `struct Upvalue::isConstant` uninitialized memory |
| `files.lua` | FAIL | `files.lua:843: Expected expression near '='` | Global assignment syntax: `global D = os.date(...)` |
| `gc.lua` | FAIL | `gc.lua:64: assertion failed!` | `collectgarbage("param")` return value; step size logic |
| `gengc.lua` | CRASH | Segfault (Exit 139) at line 106 | Dead coroutine GC sweep without closing open upvalues |
| `goto.lua` | FAIL | `goto.lua:4: Expected '*' or variable name...` | Global declaration syntax: `global <const> *` |
| `heavy.lua` | TIMEOUT | Exhaustive stress tests | Expected performance profile in unoptimized runs |
| `literals.lua` | FAIL | `attempt to compare nil and number` at loop | `native_load` does not reset coroutine `lastResultCount` |
| `locals.lua` | FAIL | `locals.lua:313: Expected ')' after parameters near 't'` | Lua 5.5 named varargs: `function f(a, ...t)` |
| `main.lua` | FAIL | `main.lua:1: attempt to get length of a nil value` | Lexer skips `#!`, but standard Lua treats `#` on line 1 as comment |
| `math.lua` | FAIL | `math.lua:11: Expected '*' or variable name...` | Global declaration syntax |
| `memerr.lua` | FAIL | `memerr.lua:129: Expected ')' after parameters near 't'` | Lua 5.5 named varargs: `function f(a, ...t)` |
| `nextvar.lua` | FAIL | `nextvar.lua:77: attempt to call a nil value` | Missing `table.create(nseq, nrec)` in stdlib |
| `pm.lua` | FAIL | `:262: Too many constants in one chunk` | Constant pool 8-bit limit in pattern matching stress |
| `sort.lua` | FAIL | `sort.lua:20: attempt to call a nil value` | Missing `table.create(nseq, nrec)` in stdlib |
| `strings.lua` | FAIL | `strings.lua:551: Expected ')' after parameters near 't'` | Lua 5.5 named varargs: `function f(a, ...t)` |
| `tpack.lua` | FAIL | `tpack.lua:25: assertion failed!` | `string.packsize`: `'i'` (int) is 8 and `'l'` (long) is 4 |
| `tracegc.lua` | **PASS** | Completed successfully | GC trace hooks and basic finalization compliant |
| `utf8.lua` | FAIL | `utf8.lua:111: assertion failed!` | `utf8.len` accepts codepoints > 0x10FFFF; offset multi-ret |
| `vararg.lua` | FAIL | `vararg.lua:6: Expected ')' after parameters near 't'` | Lua 5.5 named varargs: `function f(a, ...t)` |
| `verybig.lua` | FAIL | `:37: Too many constants in one chunk` | Constant pool 8-bit limit |

---

## 3. Deep-Dive Gap Analysis & Technical Solutions

### Gap 1: Lexer Line 1 Comment / Shebang Handling
- **Location:** [src/compiler/lexer.cpp](file:///Users/heikokoehler/lua/src/compiler/lexer.cpp#L40-L46)
- **Problem:**
  ```cpp
  // Current logic only checks for '#!'
  if (current_ + 1 < source_.length() && source_[current_] == '#' && source_[current_ + 1] == '!') {
      while (current_ < source_.length() && source_[current_] != '\n') current_++;
  }
  ```
  In Lua 5.5, *any* line starting with `#` on line 1 is treated as a comment. In `main.lua:1`, `# testing special comment on first line` is scanned as `TokenType::HASH` (length operator), leading to the VM evaluating `#testing` (length of nil), resulting in `attempt to get length of a nil value`.
- **Solution:**
  Update the condition to:
  ```cpp
  if (current_ < source_.length() && source_[current_] == '#') {
      while (current_ < source_.length() && source_[current_] != '\n') current_++;
  }
  ```

---

### Gap 2: Lua 5.5 Named Vararg Tables (`...name`)
- **Location:** [src/compiler/parser.cpp](file:///Users/heikokoehler/lua/src/compiler/parser.cpp#L601-L617), [src/compiler/ast.hpp](file:///Users/heikokoehler/lua/src/compiler/ast.hpp), [src/compiler/codegen.cpp](file:///Users/heikokoehler/lua/src/compiler/codegen.cpp)
- **Problem:**
  Lua 5.5 introduces named varargs: `function f(a, ...t)`.
  1. The parameter `...t` creates a local variable `t` bound to a newly created table containing all variable arguments.
  2. The table field `t.n` is set to the total count of varargs (preserving trailing `nil` values, e.g. `vararg(nil, nil).n == 2`).
  3. The classic `...` expression remains fully valid within the function body alongside `t`.
  Currently, `Parser::functionBody()` assumes `...` is immediately followed by `)`:
  ```cpp
  if (match(TokenType::DOT_DOT_DOT)) {
      fb.hasVarargs = true;
      break; // ... must be last parameter
  }
  ```
  When it encounters an identifier after `...`, it breaks and expects `)` next, throwing `Expected ')' after parameters near 't'`.
- **Solution:**
  1. Extend `FunctionBody` in `ast.hpp` with `std::string varargName; bool hasNamedVarargs;`.
  2. In `Parser::functionBody()`:
     ```cpp
     if (match(TokenType::DOT_DOT_DOT)) {
         fb.hasVarargs = true;
         if (check(TokenType::IDENTIFIER)) {
             fb.hasNamedVarargs = true;
             fb.varargName = current_.lexeme;
             advance();
         }
         break;
     }
     ```
  3. In `CodeGenerator::visitFunctionDecl` / `visitFunctionExpr`:
     When `hasNamedVarargs` is true, emit bytecode prologue at function entry to construct table `varargName` initialized with `table.pack(...)` semantics, capturing all varargs and setting field `n`.

---

### Gap 3: Global Variable Declarations & Attributes
- **Location:** [src/compiler/parser.cpp](file:///Users/heikokoehler/lua/src/compiler/parser.cpp#L316-L365)
- **Problem:**
  Lua 5.5 expands global scoping and strictness modes:
  - `global <const> *`: all globals default to `<const>`.
  - `global *`: wildcard allowing all globals.
  - `global none`: prohibits undeclared globals in current lexical scope.
  - `global name [ <attr> ] [, name2 [ <attr> ] ...]`: declares specific globals.
  - `global var = expr, ...`: global declaration with immediate initialization (e.g. `global fact = false`, `global D = os.date(...)`).
  - `global function f(...)`: global function declaration.
  Current parser only accepts `global <const> *` and bare variable lists without `=` initializers. It fails on `global fact = false` (`Expected expression near '='`) and rejects `<const>` attributes on variable names.
- **Solution:**
  1. Support `global <attr> *` and `global <attr> var1, var2`.
  2. In `Parser::globalDeclaration()`, support optional `=` followed by an expression list (matching `Parser::localDeclaration()`).
  3. When an initializer exists, emit assignments to the declared global names via `_ENV`.

---

### Gap 4: Extended Constant Pool Limit (> 255 Constants)
- **Location:** [src/compiler/codegen.cpp](file:///Users/heikokoehler/lua/src/compiler/codegen.cpp#L271-L275), [src/compiler/codegen.cpp#L355-L359](file:///Users/heikokoehler/lua/src/compiler/codegen.cpp#L355-L359), [src/compiler/codegen.cpp#L551-L555](file:///Users/heikokoehler/lua/src/compiler/codegen.cpp#L551-L555), [src/compiler/codegen.cpp#L1372-L1376](file:///Users/heikokoehler/lua/src/compiler/codegen.cpp#L1372-L1376)
- **Problem:**
  `CodeGenerator::emitConstant()` already supports 24-bit constant indexing via `OP_CONSTANT_LONG`:
  ```cpp
  if (index <= UINT8_MAX) {
      emitBytes(static_cast<uint8_t>(OpCode::OP_CONSTANT), static_cast<uint8_t>(index));
  } else if (index <= 0xFFFFFF) {
      emitBytes(static_cast<uint8_t>(OpCode::OP_CONSTANT_LONG), ...);
  }
  ```
  However, `visitVariable()`, `visitAssignmentStmt()`, `visitMultipleAssignmentStmt()`, and `visitGlobalDeclStmt()` manually call `currentChunk()->addConstant(...)` and assert `if (nameIndex > UINT8_MAX) throw CompileError("Too many constants in one chunk")`, followed by emitting single-byte `OP_CONSTANT`.
  Tests with large chunks (`api.lua`, `errors.lua`, `pm.lua`, `verybig.lua`) fail compilation once chunk constants exceed 255.
- **Solution:**
  Refactor all global and property identifier loads to use `emitConstant(Value::string(internString(name)))` instead of hardcoding single-byte operand emission.

---

### Gap 5: Uninitialized State & Coroutine Calling Conventions
- **Locations:**
  1. [src/compiler/codegen.hpp](file:///Users/heikokoehler/lua/src/compiler/codegen.hpp#L69-L74) & [src/compiler/codegen.cpp](file:///Users/heikokoehler/lua/src/compiler/codegen.cpp#L20-L25):
     `struct Upvalue { bool isConstant; }` is not initialized when `_ENV` is seeded as upvalue 0. In release builds, `env.isConstant` contains stack garbage. When compiling `_ENV = ...` (`events.lua:10`), the compiler checks `upvalues_[0].isConstant` and erroneously throws `:10: attempt to assign to const variable '_ENV'`.
  2. [src/vm/stdlib_base.cpp](file:///Users/heikokoehler/lua/src/vm/stdlib_base.cpp#L820-L828):
     In `native_load()`, successful text compilation pushes the closure but omits setting `vm->currentCoroutine()->lastResultCount = 1;`. When invoked in `assert(load(x), "")()`, subsequent calls compute corrupted `actualArgCount = fixedArgCount + lastResultCount`, shifting the stack pointer and causing subsequent local variables (such as numeric for-loop induction variables in `literals.lua:142`) to read `nil`, triggering `attempt to compare nil and number`.
- **Solution:**
  1. Default-initialize all members in `struct Upvalue` (`bool isConstant = false;`).
  2. Explicitly set `vm->currentCoroutine()->lastResultCount = 1;` in `native_load()` text chunk success path.

---

### Gap 6: 64-bit Integer Arithmetic & Logical Bitwise Shifts
- **Locations:** [src/vm/stdlib_math.cpp](file:///Users/heikokoehler/lua/src/vm/stdlib_math.cpp#L396-L399), [src/vm/vm.cpp](file:///Users/heikokoehler/lua/src/vm/vm.cpp#L972-L986)
- **Problem:**
  1. `math.mininteger` and `math.maxinteger` are populated with 32-bit limits:
     ```cpp
     mathTable->set("maxinteger", Value::integer(std::numeric_limits<int32_t>::max()));
     mathTable->set("mininteger", Value::integer(std::numeric_limits<int32_t>::min()));
     ```
     In Lua 5.3+, integers are standard 64-bit (`int64_t`). In `bitwise.lua:12`, `(1 << 63) == math.mininteger` fails.
  2. `VM::shiftLeft` and `VM::shiftRight` execute native C++ `<<` and `>>` on signed `int64_t`. In C++, signed right shift is arithmetic (sign-extended) rather than logical (zero-filled), shifting by $\ge 64$ is Undefined Behavior, and negative displacements are UB. In Lua 5.3+:
     - Shifts are logical zero-filled.
     - Displacement $y < 0$ shifts in opposite direction: `a << -b == a >> b`.
     - Shift displacement with $|b| \ge 64$ produces 0.
- **Solution:**
  1. Change integer limits to `int64_t`:
     ```cpp
     mathTable->set("maxinteger", Value::integer(std::numeric_limits<int64_t>::max()));
     mathTable->set("mininteger", Value::integer(std::numeric_limits<int64_t>::min()));
     ```
  2. Implement standard Lua shift function:
     ```cpp
     static int64_t lua_shift(int64_t x, int64_t y) {
         if (y < 0) {
             if (y <= -64) return 0;
             return static_cast<int64_t>(static_cast<uint64_t>(x) >> (-y));
         } else {
             if (y >= 64) return 0;
             return static_cast<int64_t>(static_cast<uint64_t>(x) << y);
         }
     }
     ```

---

### Gap 7: Weak Table GC Sweeping Logic
- **Location:** [src/vm/gc_impl.cpp](file:///Users/heikokoehler/lua/src/vm/gc_impl.cpp#L215-L220)
- **Problem:**
  In `blackenObject()`, when a `TableObject` is traversed:
  ```cpp
  case GCObject::Type::TABLE: {
      TableObject* table = static_cast<TableObject*>(object);
      vm->markValue(table->getMetatable());
      for (const auto& pair : table->data()) {
          vm->markValue(pair.first);
          vm->markValue(pair.second); // Unconditionally marks value!
      }
      break;
  }
  ```
  Every value in every table is marked `Color::BLACK` during standard mark propagation, even if the table has `__mode = "v"` or `"kv"`. Later, when `removeUnmarkedWeakEntries()` checks `if (weakValues && pair.second.asObj()->color() == Color::WHITE)`, the value is *never* white. Consequently, weak tables never collect dead references, causing infinite loops in tests like `closure.lua:39` (`while x[1] do ... end`).
- **Solution:**
  In `blackenObject()`, inspect the table's metatable `__mode` attribute:
  - If mode contains `'k'`, skip marking keys during blackening.
  - If mode contains `'v'`, skip marking values during blackening.
  - Push the weak table to `weakTables_` to be resolved during atomic ephemeron and weak sweep phases.

---

### Gap 8: Generational GC & Coroutine Upvalue Invariant
- **Location:** [src/vm/gc_impl.cpp](file:///Users/heikokoehler/lua/src/vm/gc_impl.cpp), [src/vm/vm.cpp](file:///Users/heikokoehler/lua/src/vm/vm.cpp)
- **Problem:**
  In `gengc.lua:106`, a coroutine captures a local variable into an open upvalue inside a closure `f`. The coroutine reference is set to `nil` (`co = nil`) while `f` remains live. When the GC collects the coroutine object, the coroutine's stack memory is freed, but the upvalue remained open, leaving a dangling pointer into freed memory. When `f()` is subsequently called, a segmentation fault occurs (Exit 139).
- **Solution:**
  Before freeing a `CoroutineObject` during GC sweep, ensure all open upvalues belonging to that coroutine are closed (`closeUpvalues(co, co->stack.data())`), converting stack-allocated values into heap-preserved closed upvalues.

---

### Gap 9: Standard Library Additions & Behavioral Conformance
1. **`table.create(nseq [, nrec])`:**
   - Lua 5.5 introduces `table.create(nseq, nrec)` for preallocating array and hash capacities.
   - Currently absent in `stdlib_table.cpp`, causing `sort.lua:20` and `nextvar.lua:77` to fail on `attempt to call a nil value`.
2. **`utf8.offset(s, n [, i])` Multi-Return:**
   - In Lua 5.5, `utf8.offset` returns two values: the start byte offset and the ending byte offset of the codepoint.
   - Codepoints greater than `0x10FFFF` must be rejected as invalid UTF-8.
3. **`string.packsize` Specifier Sizes:**
   - In `stdlib_string.cpp:797`, `'i'` is hardcoded to 8 bytes, while `'l'` is hardcoded to 4 bytes.
   - Compliance requires `'i'` to be `sizeof(int)` (4 bytes) and `'l'` to be `sizeof(long)` (8 bytes on 64-bit systems), ensuring `sizeshort <= sizeint <= sizelong`.
4. **`collectgarbage("param", name [, newvalue])`:**
   - In `stdlib_base.cpp:83`, setting a parameter returns the *new* value instead of the *previous* value as required by standard Lua.
5. **`package.searchpath` Template Replacement:**
   - In `stdlib_base.cpp:943`, only the first occurrence of `'?'` in a template was replaced. Standard Lua replaces all occurrences of `'?'`.

---

## 4. Phased Implementation Roadmap

### Phase 1: Critical Core VM & Lexer Fixes (Immediate)
- [x] Update `src/compiler/lexer.cpp`: Accept any line starting with `#` on line 1 as a comment.
- [x] Update `src/compiler/codegen.hpp`: Initialize `Upvalue::isConstant = false`.
- [x] Update `src/vm/stdlib_base.cpp`: In `native_load()`, set `lastResultCount = 1` upon successful text chunk compilation.
- [x] Update `src/vm/stdlib_math.cpp`: Set `math.mininteger` and `math.maxinteger` to 64-bit `INT64_MIN` / `INT64_MAX`.
- [x] Update `src/vm/vm.cpp`: Rewrite `shiftLeft` and `shiftRight` with logical zero-fill and standard displacement semantics.
- [x] Implement 64-bit integer NaN-boxing with `Int64Object` and inline 48-bit storage.
- *Milestone Check:* `main.lua`, `bitwise.lua`, `events.lua`, and `literals.lua` pass (All PASSED 100%).

### Phase 2: Compiler Constant Pool Expansion
- [x] Refactor `src/compiler/codegen.cpp`:
  - Replace manual `addConstant()` + `UINT8_MAX` checks in `visitVariable()`, `visitAssignmentStmt()`, `visitMultipleAssignmentStmt()`, and `visitGlobalDeclStmt()` with `emitConstant()` and `emitGetTabUp()` / `emitSetTabUp()`.
  - Ensure 24-bit indexing (`OP_CONSTANT_LONG`, `OP_GET_TABUP_LONG`, `OP_SET_TABUP_LONG`, `OP_CLOSURE_LONG`) is uniformly emitted across all expression and statement types and supported in VM and JIT.
  - Implement $O(1)$ constant deduplication via `constantMap_` on exact `Value::bits()`.
  - Fix 64k constant limit in NaN-boxing representation using `FLAG_COMPILE_TIME` (bit 47) instead of `0x10000` heuristic.
  - Optimize `TableObject::get` / `TableObject::has` and eliminate linear string scans on table misses.
- *Milestone Check:* `api.lua`, `errors.lua`, `pm.lua`, and `verybig.lua` compile and execute without constant overflow (`api.lua` Clean PASS, `verybig.lua` Clean PASS in ~2s). Phase 1 milestone tests (`main.lua`, `bitwise.lua`, `events.lua`, `literals.lua`) and all 207 internal test suite tests pass 100%.

### Phase 3: Parser & Language Extensions for Lua 5.5
- [x] Implement Named Varargs (`...name`):
  - Extend AST in `src/compiler/ast.hpp`.
  - Update `Parser::functionBody()` in `src/compiler/parser.cpp`.
  - In `CodeGenerator`, emit bytecode prologue initializing local vararg table with count field `n`.
- [x] Implement Full `global` Syntax:
  - Support `global *`, `global none`, `global <const> *`.
  - Support initialized globals: `global var1 = expr1, var2 = expr2`.
  - Support global function declarations: `global function name(...) ... end`.
- *Milestone Check:* `vararg.lua`, `locals.lua`, `strings.lua`, `calls.lua`, `goto.lua`, `coroutine.lua`, `math.lua`, `files.lua` pass (100% Clean PASS).

### Phase 4: Standard Library Conformance
- [x] Add `table.create(nseq, nrec)` to `src/vm/stdlib_table.cpp` and support preallocated hash/array capacities in `TableObject`.
- [x] Update `utf8.offset`, `utf8.len`, `utf8.codes`, and `utf8.codepoint` in `src/vm/stdlib_utf8.cpp` to return dual indices, support lax mode, and validate Unicode `0x10FFFF` limit.
- [x] Implement full `string.pack`, `string.unpack`, and `string.packsize` in `src/vm/stdlib_string.cpp` supporting native `sizeof(int)` and `sizeof(long)`, 1..16 byte integer widths, power-of-two alignment (`!n`), alignment items (`Xop`), strings (`s[n]`, `z`, `c[n]`), and floating point (`f`, `d`, `n`).
- [x] Fix `collectgarbage("param")` in `src/vm/stdlib_base.cpp` to return the previous value.
- [x] Fix `package.searchpath` in `src/vm/stdlib_base.cpp` to replace all `'?'` characters in search templates.
- [x] Implement Lua C API bridge in `src/api/` (`lua.h`, `lauxlib.h`, `luaconf.h`, `lua_api.cpp`) for C dynamic library modules and package loading in `attrib.lua`.
- [x] Pre-evaluate table and key expressions for indexed targets in `MultipleAssignmentStmtNode` to prevent mutation conflicts during store.
- *Milestone Check:* `sort.lua`, `nextvar.lua`, `utf8.lua`, `tpack.lua`, `attrib.lua` pass (100% Clean PASS). Phase 1-3 milestone tests (14 suites) and all 207 internal tests continue to pass 100%.

### Phase 5: Garbage Collector & Memory Hardening
- [ ] Refactor `blackenObject()` in `src/vm/gc_impl.cpp` to respect weak table keys/values (`__mode`).
- [ ] Verify ephemeron table processing and sweep cleanup in `removeUnmarkedWeakEntries()`.
- [ ] Add open upvalue closing during coroutine finalization / GC sweep to eliminate dangling pointers.
- [ ] Wrap runtime execution hooks to ensure Lua errors in `__close` metamethods are caught by enclosing `pcall` protected frames.
- *Milestone Check:* `closure.lua`, `gengc.lua`, `gc.lua`, `cstack.lua` pass without timeouts or segfaults.

### Phase 6: Master Suite Verification & Regression Testing
- [ ] Run complete master suite: `./build/lua lua-5.5.0-tests/all.lua`.
- [ ] Run internal regression test suite: `ctest --output-on-failure`.
- [ ] Verify real-world web server (`examples/http_server.lua`) continues executing without regressions.

---

## 5. Verification Plan

### Automated Regression Testing
All changes will be continuously verified against existing test targets:
```bash
cmake --build build
cd build && ctest --output-on-failure
```

### Official Lua 5.5 Test Suite Execution
Tests in `lua-5.5.0-tests/` will be run individually and as a complete batch:
```bash
# Individual test execution
./build/lua lua-5.5.0-tests/bitwise.lua
./build/lua lua-5.5.0-tests/vararg.lua
./build/lua lua-5.5.0-tests/utf8.lua

# Master test runner
cd lua-5.5.0-tests && ../build/lua all.lua
```
