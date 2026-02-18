# Lua VM Implementation

A Lua implementation in C++ featuring a stack-based bytecode virtual machine with functions, variables, and control flow.

## Features

### Implemented
- **Stack-based Virtual Machine**: Bytecode interpreter with efficient execution
- **NaN-boxing Values**: 64-bit value representation supporting nil, boolean, number, and function types
- **Variables**:
  - **Local Variables**: Block-scoped with proper shadowing
  - **Global Variables**: Module-level scope
  - **Assignment**: Full assignment support for both local and global variables
- **Functions**:
  - **Function Declarations**: Named functions with parameters
  - **Function Calls**: Full call stack with proper argument passing
  - **Return Statements**: Single return value support
  - **Recursion**: Full recursive function support
- **Arithmetic Operations**: +, -, *, /, % (modulo), ^ (power)
- **Comparison Operations**: ==, ~=, <, <=, >, >= (Lua-compliant ~= operator)
- **Unary Operations**: - (negation), not
- **Control Flow**:
  - **If-Then-Elseif-Else-End**: Conditional branching with multiple elseif branches
  - **While-Do-End**: Conditional loops
  - **Repeat-Until**: Post-test loops
  - **For Loops**: Numeric for loops with step support
- **Print Statement**: Built-in print() function
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
│   │   └── function.hpp        # Function objects
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
- Other types: Special NaN patterns with type tags
  - Nil: TAG_NIL
  - Boolean: TAG_BOOL
  - Function: TAG_FUNCTION (stores function index)
- Fast type checking via bit operations
- Functions stored as indices into chunk's function pool for safe pointer management

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
| OP_JUMP | Unconditional jump |
| OP_JUMP_IF_FALSE | Conditional jump |
| OP_LOOP | Jump backward (for loops) |
| OP_CLOSURE | Load function constant |
| OP_CALL | Call function with arguments |
| OP_RETURN_VALUE | Return from function with value |
| OP_PRINT | Print value |
| OP_POP | Discard stack top |
| OP_RETURN | End execution |

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
- ⏳ Break statement
- ⏳ Generic for loops (ipairs, pairs)

### Phase 3: Functions ✅ COMPLETE
- ✅ Function declarations
- ✅ Function calls
- ✅ Return statements
- ✅ Call frames and recursion
- ⏳ Closures and upvalues
- ⏳ Multiple return values
- ⏳ Variadic functions (...)

### Phase 4: Objects & Memory
- String objects with interning
- Table objects (hash maps)
- Garbage collection (mark-and-sweep)

### Phase 5: Advanced Features
- Closures and upvalues
- For loops
- Metatables
- Standard library

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
