# Lua VM Implementation

A Lua implementation in C++ featuring a stack-based bytecode virtual machine with functions, variables, and control flow.

## Features

### Implemented
- **Stack-based Virtual Machine**: Bytecode interpreter with efficient execution
- **NaN-boxing Values**: 64-bit value representation supporting nil, boolean, number, function, string, and table types
- **Strings**:
  - **String Literals**: Double and single quoted strings
  - **String Interning**: Automatic deduplication for memory efficiency
  - **Multi-line Support**: Strings can span multiple lines
- **Tables (Hash Maps)**:
  - **Associative Arrays**: Key-value storage with any value type as key (except nil)
  - **Table Constructors**: Create tables with initial values using `{}` syntax
  - **Array-style**: `{1, 2, 3}` creates numeric-indexed table
  - **Record-style**: `{x = 10, y = 20}` creates string-keyed table
  - **Computed keys**: `{[expr] = value}` for dynamic key expressions
  - **Indexing Operations**: Get and set values with `table[key]` syntax
  - **Lua Semantics**: Nil keys rejected, setting to nil removes key
- **Variables**:
  - **Local Variables**: Block-scoped with proper shadowing
  - **Global Variables**: Module-level scope
  - **Assignment**: Full assignment support for both local and global variables
  - **Multiple Assignment**: Assign multiple values to multiple variables in one statement
    - `local a, b, c = 1, 2, 3` - Multiple local declarations
    - `x, y, z = 10, 20, 30` - Multiple global assignments
    - `x, y = y, x` - Swap idiom (all RHS evaluated before assignment)
    - **Value Adjustment**: Pads with nil or discards extras to match variable count
- **Functions**:
  - **Function Declarations**: Named functions with parameters
  - **Function Calls**: Full call stack with proper argument passing
  - **Return Statements**: Multiple return value support
  - **Recursion**: Full recursive function support
  - **Closures**: Functions can capture variables from enclosing scopes (upvalues)
  - **Nested Functions**: Functions can be defined inside other functions
  - **Variadic Functions**: Functions can accept variable arguments with `...`
- **Arithmetic Operations**: +, -, *, /, % (modulo), ^ (power)
- **Comparison Operations**: ==, ~=, <, <=, >, >= (Lua-compliant ~= operator)
- **Unary Operations**: - (negation), not
- **Control Flow**:
  - **If-Then-Elseif-Else-End**: Conditional branching with multiple elseif branches
  - **While-Do-End**: Conditional loops
  - **Repeat-Until**: Post-test loops
  - **For Loops**: Numeric for loops with step support, and generic for-in loops with iterators
  - **Break Statement**: Exit loops early
- **Print Statement**: Built-in print() function
- **File I/O**:
  - **io_open(filename, mode)**: Open files in read, write, or append mode
  - **io_write(file, data)**: Write string data to files
  - **io_read(file)**: Read entire file contents
  - **io_close(file)**: Close file handles
  - **Multiple modes**: "r", "w", "a", "r+", "w+", "a+"
- **Garbage Collection**:
  - **Mark-and-Sweep**: Automatic memory management for all heap objects
  - **Automatic Triggering**: GC runs when memory threshold exceeded
  - **Manual Control**: `collectgarbage()` function for explicit collection
  - **Comprehensive Coverage**: All object types (strings, tables, closures, upvalues, files)
- **Standard Library**:
  - **String Library**: len, sub, upper, lower, reverse, byte, char
  - **Table Library**: insert, remove, concat
  - **Math Library**: sqrt, abs, floor, ceil, sin, cos, tan, exp, log, min, max, pi
  - **Dot Notation**: Clean syntax like `string.len("hello")` and `math.sqrt(16)`
- **REPL**: Interactive read-eval-print loop
- **File Execution**: Run Lua scripts from files

### Architecture

```
Lua Source Code
    ↓
Lexer (tokenization)
    ↓
Token Stream
    ↓
Parser (recursive descent)
    ↓
Abstract Syntax Tree (AST)
    ↓
Code Generator
    ↓
Bytecode Chunk
    ↓
VM Execution (stack-based)
    ↓
Output
```

## Building

### Prerequisites
- CMake 3.10+
- C++17 compatible compiler (GCC, Clang, or MSVC)

### Build Steps

```bash
mkdir build
cd build
cmake ..
make
```

### Debug Builds

Enable execution tracing:
```bash
cmake -DDEBUG_TRACE_EXECUTION=ON ..
make
```

Enable bytecode disassembly:
```bash
cmake -DDEBUG_PRINT_CODE=ON ..
make
```

## Usage

### Run a Lua File

```bash
./lua examples/test.lua
```

### Interactive REPL

```bash
./lua
```

Then type Lua expressions:
```
> print(2 + 3)
5
> print(2 ^ 10)
1024
> exit
```

## Example Programs

### Basic Arithmetic

```lua
print(2 + 3)          -- 5
print(10 - 4)         -- 6
print(5 * 6)          -- 30
print(20 / 4)         -- 5
print(10 % 3)         -- 1
print(2 ^ 3)          -- 8
```

### Operator Precedence

```lua
print(2 + 3 * 4)      -- 14 (multiplication first)
print((2 + 3) * 4)    -- 20 (parentheses override)
print(2 ^ 3 ^ 2)      -- 512 (right-associative: 2^9)
```

### Comparisons

```lua
print(5 < 10)         -- true
print(5 > 10)         -- false
print(5 == 5)         -- true
print(5 ~= 3)         -- true (Lua's not-equal operator)
```

### Boolean and Nil

```lua
print(true)           -- true
print(false)          -- false
print(nil)            -- nil
```

### Control Flow

#### If-Then-Else

```lua
if 5 > 3 then
    print(1)
end

if false then
    print(999)
else
    print(2)
end

-- Multiple branches with elseif
if 1 > 2 then
    print(999)
elseif 2 > 3 then
    print(999)
elseif 3 < 5 then
    print(3)
else
    print(999)
end
```

#### While Loops

```lua
-- Note: Without variables, these examples are limited
-- Full loop functionality requires variable support (coming in Phase 2)
while false do
    print(999)  -- Never executes
end
```

#### Repeat-Until Loops

```lua
-- Executes body at least once
repeat
    print(1)
    print(2)
until true

-- Loops until condition becomes true
```

#### Break Statement

```lua
-- Break out of loop early
for i = 1, 10 do
    print(i)
    if i >= 5 then
        break  -- Exit loop when i reaches 5
    end
end
-- Output: 1 2 3 4 5

-- Break in while loop
local i = 1
while true do
    print(i)
    if i >= 3 then
        break
    end
    i = i + 1
end
-- Output: 1 2 3
```

### Variables

#### Local Variables

```lua
local x = 10
local y = 20
print(x + y)  -- 30

-- Block scoping
local a = 1
if true then
    local a = 2  -- Shadows outer 'a'
    print(a)     -- 2
end
print(a)         -- 1
```

#### Global Variables

```lua
x = 100
y = 200
print(x + y)  -- 300
```

#### For Loops

```lua
-- Numeric for loop
for i = 1, 5 do
    print(i)
end
-- Output: 1 2 3 4 5

-- With step
for i = 0, 10, 2 do
    print(i)
end
-- Output: 0 2 4 6 8 10

-- Generic for loop with iterator
function countdown(n)
    if n == nil then
        return 5
    elseif n > 1 then
        return n - 1
    else
        return nil  -- Stop iteration
    end
end

for i in countdown do
    print(i)
end
-- Output: 5 4 3 2 1
```

### Functions

#### Basic Functions

```lua
function greet()
    print(42)
end

greet()  -- 42
```

#### Functions with Parameters

```lua
function add(a, b)
    return a + b
end

print(add(10, 32))  -- 42
```

#### Functions with Return Values

```lua
function double(x)
    return x * 2
end

local result = double(21)
print(result)  -- 42
```

#### Recursive Functions

```lua
function fib(n)
    if n < 2 then
        return n
    end
    return fib(n - 1) + fib(n - 2)
end

print(fib(6))  -- 8
```

#### Multiple Return Values

```lua
function getTwoNumbers()
    return 10, 20
end

function getThreeNumbers()
    return 100, 200, 300
end

-- Assignment captures only first value
local a = getTwoNumbers()
print(a)  -- 10

local b = getThreeNumbers()
print(b)  -- 100
```

#### Closures

```lua
-- Counter with enclosed state
function makeCounter()
    local count = 0
    function increment()
        count = count + 1
        return count
    end
    return increment
end

local counter = makeCounter()
print(counter())  -- 1
print(counter())  -- 2
print(counter())  -- 3

-- Multiple closures sharing state
function makeBox(initial)
    local value = initial
    function get()
        return value
    end
    function set(v)
        value = v
    end
    return get, set
end

local getter, setter = makeBox(42)
print(getter())  -- 42
setter(100)
print(getter())  -- 100
```

### File I/O

#### Writing to Files

```lua
-- Open file for writing
local file = io_open("output.txt", "w")
io_write(file, "Hello from Lua!")
io_write(file, " ")
io_write(file, "This is line 2")
io_close(file)
```

#### Reading from Files

```lua
-- Open file for reading
local file = io_open("input.txt", "r")
local content = io_read(file)
io_close(file)

print(content)  -- Prints entire file contents
```

#### Append Mode

```lua
-- Open file for appending
local file = io_open("log.txt", "a")
io_write(file, "New log entry")
io_close(file)
```

#### Complete Example

```lua
-- Write data
local outfile = io_open("data.txt", "w")
io_write(outfile, "Line 1")
io_write(outfile, " Line 2")
io_close(outfile)

-- Read it back
local infile = io_open("data.txt", "r")
local data = io_read(infile)
io_close(infile)

print(data)  -- Line 1 Line 2
```

### Variadic Functions

#### Function with Only Varargs

```lua
-- Accepts any number of arguments
function printAll(...)
    print(...)
end

printAll()           -- nil
printAll(1)          -- 1
printAll(1, 2, 3)    -- 1 2 3
```

#### Mixed Parameters and Varargs

```lua
-- Regular parameters followed by varargs
function greet(name, ...)
    print(name)
    print(...)
end

greet("Alice")              -- Alice nil
greet("Bob", "Hello")       -- Bob Hello
greet("Charlie", 1, 2, 3)   -- Charlie 1 2 3
```

#### Using Varargs in Expressions

```lua
-- Get first vararg
function getFirst(...)
    local first = ...
    return first
end

print(getFirst(42))        -- 42
print(getFirst(10, 20))    -- 10

-- Count demonstration
function acceptAny(...)
    print(1)  -- Always prints 1 regardless of args
end

acceptAny()
acceptAny(1)
acceptAny(1, 2, 3)
-- Output: 1 1 1
```

#### Varargs Notes

- The `...` must be the last parameter in function signature
- In expressions, `...` expands to all varargs
- With zero varargs, `...` evaluates to nil
- Varargs work with closures and nested functions

### Tables (Hash Maps)

#### Creating Tables

```lua
-- Empty table
local t = {}

-- Array-style (implicit numeric keys 1, 2, 3, ...)
local arr = {10, 20, 30}
print(arr[1])  -- 10
print(arr[2])  -- 20

-- Record-style (string keys)
local person = {name = "Alice", age = 30}
print(person["name"])  -- Alice
print(person["age"])   -- 30

-- Mixed style
local mixed = {100, 200, x = 10, y = 20}
print(mixed[1])    -- 100 (array index)
print(mixed["x"])  -- 10 (string key)

-- Computed keys
local key = "color"
local obj = {[key] = "blue", [1+1] = "two"}
print(obj["color"])  -- blue
print(obj[2])        -- two

-- Nested tables
local nested = {
    inner = {a = 1, b = 2},
    values = {10, 20, 30}
}
print(nested["inner"]["a"])   -- 1
print(nested["values"][1])    -- 10
```

#### Table Assignment and Access

```lua
local scores = {}

-- Numeric keys
scores[1] = 100
scores[2] = 95
scores[3] = 87

print(scores[1])  -- 100
print(scores[2])  -- 95

-- String keys
local person = {}
person["name"] = "Alice"
person["age"] = 30

print(person["name"])  -- Alice
print(person["age"])   -- 30

-- Mixed keys
local mixed = {}
mixed[1] = "first"
mixed["key"] = "value"
mixed[true] = "boolean key"
```

#### Tables as Function Return Values

```lua
function makeData()
    local t = {}
    t["x"] = 10
    t["y"] = 20
    return t
end

local data = makeData()
print(data["x"] + data["y"])  -- 30
```

#### Tables in Loops

```lua
local array = {}

-- Populate array
for i = 1, 5 do
    array[i] = i * i
end

-- Print array
for i = 1, 5 do
    print(array[i])
end
-- Output: 1 4 9 16 25
```

#### Removing Keys

```lua
local t = {}
t["key"] = "value"
print(t["key"])  -- value

-- Set to nil to remove key
t["key"] = nil
print(t["key"])  -- nil
```

### Standard Library

#### String Functions

```lua
-- String length
print(string.len("hello"))  -- 5

-- Substring extraction
print(string.sub("hello", 2, 4))  -- "ell"
print(string.sub("hello", -3))    -- "llo" (negative index from end)

-- Case conversion
print(string.upper("hello"))  -- "HELLO"
print(string.lower("WORLD"))  -- "world"

-- String reversal
print(string.reverse("abc"))  -- "cba"

-- Character code conversion
print(string.byte("A"))           -- 65
print(string.char(72, 105))       -- "Hi"
```

#### Table Functions

```lua
local t = {10, 20, 30}

-- Insert at end
table.insert(t, 40)
print(t[4])  -- 40

-- Insert at position
table.insert(t, 2, 15)
print(t[2])  -- 15

-- Remove element
local removed = table.remove(t, 1)
print(removed)  -- 10

-- Concatenate array elements
local arr = {"Hello", " ", "World"}
print(table.concat(arr))  -- "Hello World"
print(table.concat({1, 2, 3}, ", "))  -- "1, 2, 3"
```

#### Math Functions

```lua
-- Basic math operations
print(math.sqrt(16))   -- 4
print(math.abs(-5))    -- 5
print(math.floor(3.7)) -- 3
print(math.ceil(3.2))  -- 4

-- Trigonometric functions
print(math.sin(math.pi / 2))  -- 1
print(math.cos(0))            -- 1
print(math.tan(0))            -- 0

-- Exponential and logarithm
print(math.exp(1))      -- 2.71828...
print(math.log(2.71828)) -- ~1

-- Min and max
print(math.min(5, 2, 8, 1))  -- 1
print(math.max(5, 2, 8, 1))  -- 8

-- Constants
print(math.pi)  -- 3.14159...
```

#### Garbage Collection

```lua
-- Create some objects
local t1 = {x = 1, y = 2}
local t2 = {a = 10, b = 20}

-- Manually trigger garbage collection
collectgarbage()

-- Objects still accessible after GC
print(t1.x)  -- 1
print(t2.a)  -- 10
```

## Project Structure

```
lua/
├── CMakeLists.txt              # Build configuration
├── src/
│   ├── common/                 # Shared utilities
│   │   ├── common.hpp
│   │   └── common.cpp
│   ├── value/                  # Value representation
│   │   ├── value.hpp
│   │   ├── value.cpp
│   │   ├── function.hpp        # Function objects
│   │   ├── closure.hpp         # Closure objects (function + upvalues)
│   │   ├── upvalue.hpp         # Upvalue objects (captured variables)
│   │   ├── string.hpp          # String objects with interning
│   │   ├── table.hpp           # Table objects (hash maps)
│   │   ├── file.hpp            # File objects for I/O
│   │   └── file.cpp
│   ├── compiler/               # Compilation pipeline
│   │   ├── token.hpp
│   │   ├── lexer.hpp
│   │   ├── lexer.cpp
│   │   ├── ast.hpp
│   │   ├── parser.hpp
│   │   ├── parser.cpp
│   │   ├── chunk.hpp
│   │   ├── chunk.cpp
│   │   ├── codegen.hpp
│   │   └── codegen.cpp
│   ├── vm/                     # Virtual machine
│   │   ├── opcode.hpp
│   │   ├── vm.hpp
│   │   └── vm.cpp
│   └── main.cpp                # Entry point
├── examples/                   # Test scripts
│   ├── test.lua
│   └── simple.lua
└── build/                      # Build artifacts
```

## Technical Details

### Value Representation

Uses **NaN-boxing** technique:
- All values stored in 64-bit words
- Numbers: IEEE 754 double-precision floats
- Other types: Special NaN patterns with type tags (4-bit tag field)
  - Nil: TAG_NIL (1)
  - Boolean: TAG_BOOL (2)
  - Function: TAG_FUNCTION (3) - stores function index
  - String: TAG_STRING (4) - stores compile-time string index (from chunk)
  - Table: TAG_TABLE (5) - stores table index
  - Closure: TAG_CLOSURE (6) - stores closure index (function + captured upvalues)
  - File: TAG_FILE (7) - stores file handle index
  - Runtime String: TAG_RUNTIME_STRING (8) - stores runtime string index (from VM pool)
  - Native Function: TAG_NATIVE_FUNCTION (9) - stores C++ function pointer index
- Fast type checking via bit operations
- Objects stored as indices into pools for safe pointer management
- No raw pointers in values (prevents corruption)
- Dual string system: compile-time strings in chunk, runtime strings in VM pool
- Native functions enable standard library implementation in C++

### Bytecode Instructions

| Opcode | Description |
|--------|-------------|
| OP_CONSTANT | Load constant from pool |
| OP_NIL, OP_TRUE, OP_FALSE | Push literals |
| OP_ADD, OP_SUB, OP_MUL, OP_DIV | Arithmetic |
| OP_MOD, OP_POW | Modulo, power |
| OP_NEG, OP_NOT | Unary operations |
| OP_EQUAL, OP_LESS, OP_GREATER, etc. | Comparisons |
| OP_GET_GLOBAL, OP_SET_GLOBAL | Global variable access |
| OP_GET_LOCAL, OP_SET_LOCAL | Local variable access |
| OP_GET_UPVALUE, OP_SET_UPVALUE | Upvalue (closure) access |
| OP_CLOSE_UPVALUE | Close upvalue when exiting scope |
| OP_JUMP | Unconditional jump |
| OP_JUMP_IF_FALSE | Conditional jump |
| OP_LOOP | Jump backward (for loops) |
| OP_CLOSURE | Create closure from function with upvalues |
| OP_CALL | Call function with arguments and return count |
| OP_RETURN_VALUE | Return from function with values (supports multiple) |
| OP_NEW_TABLE | Create new empty table |
| OP_GET_TABLE | Get value from table (table[key]) |
| OP_SET_TABLE | Set value in table (table[key] = value) |
| OP_PRINT | Print value |
| OP_POP | Discard stack top |
| OP_DUP | Duplicate stack top |
| OP_IO_OPEN | Open file: pop mode and filename, push file handle |
| OP_IO_WRITE | Write to file: pop data and file handle |
| OP_IO_READ | Read from file: pop file handle, push contents |
| OP_IO_CLOSE | Close file: pop file handle |
| OP_GET_VARARG | Push all varargs from current frame (or nil if none) |
| OP_RETURN | End execution |

### Closures and Upvalues

Implements lexical scoping with closures:
- **Three-level variable resolution**: local → upvalue → global
- **Open upvalues**: Point to stack locations while parent function is active
- **Closed upvalues**: Heap-allocated when parent function returns
- **Upvalue deduplication**: Multiple closures capturing the same variable share the upvalue
- **Nested closures**: Support for multiple levels of function nesting (2+ levels)

Implementation details:
- `ClosureObject`: Combines function with array of upvalue indices
- `UpvalueObject`: Manages open/closed state with stack index or heap value
- Compiler tracks captured variables with `isCaptured` flag
- VM maintains sorted list of open upvalues for efficient closing
- `OP_CLOSURE` instruction includes upvalue descriptors (isLocal + index pairs)

### Multiple Return Values

Functions can return multiple values:
- **Return syntax**: `return expr1, expr2, expr3`
- **Single-value context**: `local x = func()` takes only first value
- **Return count tracking**: `OP_CALL` includes expected return count parameter
- **Value adjustment**: Runtime pads with nil or truncates based on context

Current implementation:
- Function calls always request single value (retCount=1)
- Extra return values are discarded at call site
- Future: Multiple assignment, return value expansion in arguments

### Parser

Recursive descent with proper operator precedence:
1. Logical OR (lowest)
2. Logical AND
3. Equality (==, !=)
4. Comparison (<, <=, >, >=)
5. Addition, Subtraction
6. Multiplication, Division, Modulo
7. Power (right-associative)
8. Unary (-, not)
9. Primary (literals, grouping) (highest)

## Future Enhancements

### Phase 2: Variables & Control Flow ✅ COMPLETE
- ✅ If/then/elseif/else statements
- ✅ While loops
- ✅ Repeat-until loops
- ✅ Local and global variables
- ✅ For loops (numeric)
- ✅ Variable scoping
- ✅ Break statement
- ✅ Generic for loops (iterator-based)

### Phase 3: Functions ✅ COMPLETE
- ✅ Function declarations
- ✅ Function calls
- ✅ Return statements (with multiple return values)
- ✅ Call frames and recursion
- ✅ Closures and upvalues
- ✅ Multiple return values (in single-value contexts)
- ✅ Variadic functions (...)
- ⏳ Multiple assignment (local a, b = func())

### Phase 4: Objects & Memory ✅ COMPLETE
- ✅ String objects with interning
- ✅ Table objects (hash maps) with constructor `{}`, indexing `t[key]`, and assignment `t[key] = value`
- ✅ Table constructor with initial values: `{1, 2, 3}`, `{x = 10}`, `{[expr] = val}`
- ✅ File I/O: `io_open`, `io_write`, `io_read`, `io_close`
- ✅ Garbage collection (mark-and-sweep with automatic and manual triggering)
- ✅ Native function system for C++ built-ins
- ✅ Dot notation for table field access (`table.field`)

### Phase 5: Standard Library ✅ COMPLETE
- ✅ Base library: `collectgarbage()`
- ✅ String library: `string.len`, `string.sub`, `string.upper`, `string.lower`, `string.reverse`, `string.byte`, `string.char`
- ✅ Table library: `table.insert`, `table.remove`, `table.concat`
- ✅ Math library: `math.sqrt`, `math.abs`, `math.floor`, `math.ceil`, `math.sin`, `math.cos`, `math.tan`, `math.exp`, `math.log`, `math.min`, `math.max`, `math.pi`

### Phase 6: Advanced Features
- Metatables and metamethods
- String escape sequences (\n, \t, etc.)
- Module system (require/module)
- Coroutines
- Iterators for tables (pairs, ipairs)

## Testing

Run the test suite:
```bash
cd build
./lua ../examples/test.lua
```

Expected output:
```
5
6
30
5
-15
20
14
20
8
512
1
true
false
true
true
true
false
nil
```

## Success Criteria

### MVP ✅ COMPLETE
- ✅ Parse and execute arithmetic expressions
- ✅ Print function works correctly
- ✅ Operator precedence is correct
- ✅ Error messages include line numbers
- ✅ REPL works interactively
- ✅ Can run example scripts from files

### Beyond MVP ✅ COMPLETE
- ✅ Local and global variables with proper scoping
- ✅ Functions with parameters and return values
- ✅ Recursive function calls
- ✅ For loops with numeric ranges
- ✅ Complex control flow with variables

## License

This is an educational implementation of a Lua-like language for learning purposes.
