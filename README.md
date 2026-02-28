# Lua VM Implementation

A Lua implementation in C++ featuring a stack-based bytecode virtual machine with functions, variables, and control flow.

## Features

### Implemented
- **Stack-based Virtual Machine**: Bytecode interpreter with efficient execution
- **NaN-boxing Values**: 64-bit value representation supporting nil, boolean, number, function, string, table, and **userdata** types
- **Strings**:
  - **String Literals**: Double and single quoted strings
  - **String Escape Sequences**: Full support for `\n`, `\t`, `\r`, `\\`, `\"`, `\'`, `\xXX` (hex), and `\ddd` (decimal)
  - **String Interning**: Automatic deduplication for memory efficiency
  - **Multi-line Support**: Strings can span multiple lines
- **Tables (Hash Maps)**:
  - **Associative Arrays**: Key-value storage with any value type as key (except nil)
  - **Table Constructors**: Create tables with initial values using `{}` syntax
  - **Weak Tables**: Support for `__mode = "k"`, `"v"`, and ephemerons (Lua 5.2+ semantics)
  - **Lua Semantics**: Nil keys rejected, setting to nil removes key
- **Variables**:
  - **Local Variables**: Block-scoped with proper shadowing
  - **Global Variables**: Module-level scope
  - **Assignment**: Full assignment support for both local and global variables
  - **Multiple Assignment**: Assign multiple values to multiple variables in one statement
- **Functions**:
  - **Function Declarations**: Named functions with parameters
  - **Return Statements**: Multiple return value support
  - **Closures**: Functions can capture variables from enclosing scopes (upvalues)
  - **Variadic Functions**: Functions can accept variable arguments with `...`
- **Arithmetic & Bitwise Operations**:
  - **Arithmetic**: +, -, *, /, % (modulo), ^ (power), // (integer division)
  - **Bitwise**: &, |, ~ (XOR), <<, >>, ~ (NOT) (Lua 5.3+)
  - **Length**: # (length operator) for strings and tables
- **Comparison Operations**: ==, ~=, <, <=, >, >=
- **Control Flow**:
  - **If-Then-Elseif-Else-End**: Conditional branching
  - **While-Do-End** / **Repeat-Until** / **Numeric For** / **Generic For**
  - **Goto and Labels**: Support for `goto label` and `::label::` (Lua 5.2+)
  - **Do-End Blocks**: Explicit block scoping with `do ... end`
  - **Break Statement**: Exit loops early
- **Module System**:
  - **require(modname)**: Load and cache modules
  - **package.path**: Customizable search path for modules
- **Coroutines**:
  - **First-class Threads**: Coroutines as values
  - **Yield/Resume**: transfer control and pass values
- **Garbage Collection**:
  - **Mark-and-Sweep**: Automatic memory management
  - **Ephemeron Support**: Correct handling of weak keys/values
- **Standard Library**:
  - **String Library**: len, sub, upper, lower, reverse, byte, char, **find, match, gmatch, gsub** (Full pattern matching support)
  - **Table Library**: insert, remove, concat, pack, unpack, pairs, ipairs, next
  - **Math Library**: sqrt, abs, floor, ceil, sin, cos, tan, exp, log, min, max, pi
  - **Debug Library**: sethook, setmetatable (set metatables for any type)
- **Bytecode Serialization**:
  - **Compilation**: Compile Lua source to binary `.luac` files
  - **Portability**: All function metadata and constants preserved

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
    Bytecode Chunk  ←─── Deserialize (Binary .luac)
          ↓
    Serialize (Binary .luac)
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

## Usage

### Run a Lua File
```bash
./lua tests/test.lua
```

### Interactive REPL
```bash
./lua
```

## Example Programs

### Bitwise Operators (Lua 5.3+)
```lua
print(5 & 3)   -- 1 (101 & 011 = 001)
print(5 | 3)   -- 7 (101 | 011 = 111)
print(5 ~ 3)   -- 6 (101 ~ 011 = 110)
print(1 << 3)  -- 8
print(~5)      -- -6 (Bitwise NOT)
print(10 // 3) -- 3 (Integer division)
```

### Pattern Matching
```lua
local s = "hello world 123"
print(s:match("%w+"))      -- hello
print(s:gsub("%d", "x"))   -- hello world xxx
for word in s:gmatch("%w+") do
    print(word)
end
```

### Goto and Labels
```lua
local i = 1
::loop::
print(i)
i = i + 1
if i <= 3 then goto loop end
```

### Weak Tables & Userdata
```lua
local weak = setmetatable({}, {__mode = "v"})
local obj = {}
weak[1] = obj
obj = nil
collectgarbage()
print(weak[1]) -- nil (collected)
```

## Metatables

Supported metamethods:
- **Arithmetic**: `__add`, `__sub`, `__mul`, `__div`, `__idiv`, `__mod`, `__pow`, `__unm`
- **Bitwise**: `__band`, `__bor`, `__bxor`, `__bnot`, `__shl`, `__shr`
- **Comparison**: `__eq`, `__lt`, `__le`
- **Miscellaneous**: `__index`, `__newindex`, `__call`, `__concat`, `__len`

## License

MIT License - see [LICENSE](LICENSE) file for details.
