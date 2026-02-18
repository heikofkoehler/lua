# Lua VM Implementation (MVP)

A minimal Lua implementation in C++ featuring a stack-based bytecode virtual machine.

## Features

### Implemented
- **Stack-based Virtual Machine**: Bytecode interpreter with efficient execution
- **NaN-boxing Values**: 64-bit value representation supporting nil, boolean, and number types
- **Arithmetic Operations**: +, -, *, /, % (modulo), ^ (power)
- **Comparison Operations**: ==, ~=, <, <=, >, >= (Lua-compliant ~= operator)
- **Unary Operations**: - (negation), not
- **Control Flow**:
  - **If-Then-Elseif-Else-End**: Conditional branching with multiple elseif branches
  - **While-Do-End**: Conditional loops
  - **Repeat-Until**: Post-test loops
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
-- (Note: Full functionality requires variables)
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
│   │   └── value.cpp
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
- Fast type checking via bit operations

### Bytecode Instructions

| Opcode | Description |
|--------|-------------|
| OP_CONSTANT | Load constant from pool |
| OP_NIL, OP_TRUE, OP_FALSE | Push literals |
| OP_ADD, OP_SUB, OP_MUL, OP_DIV | Arithmetic |
| OP_MOD, OP_POW | Modulo, power |
| OP_NEG, OP_NOT | Unary operations |
| OP_EQUAL, OP_LESS, OP_GREATER, etc. | Comparisons |
| OP_JUMP | Unconditional jump |
| OP_JUMP_IF_FALSE | Conditional jump |
| OP_LOOP | Jump backward (for loops) |
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

### Phase 2: Variables & Control Flow (Partially Complete)
- ✅ If/then/elseif/else statements
- ✅ While loops
- ✅ Repeat-until loops
- ⏳ Local and global variables
- ⏳ For loops (numeric and generic)
- ⏳ Variable scoping
- ⏳ Break statement

### Phase 3: Functions
- Function declarations
- Function calls
- Return statements
- Call frames

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

## Success Criteria (MVP)

- ✅ Parse and execute arithmetic expressions
- ✅ Print function works correctly
- ✅ Operator precedence is correct
- ✅ Error messages include line numbers
- ✅ REPL works interactively
- ✅ Can run example scripts from files

## License

This is an educational implementation of a Lua-like language for learning purposes.
