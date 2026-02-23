# Standard Library Quick Start Guide

## Accessing Functions

Due to parser limitations, use bracket notation to access standard library functions:

```lua
-- Get function from namespace
local len = string["len"]

-- Call the function
local result = len("hello")
print(result)  -- 5
```

## String Functions

```lua
-- Length
local len = string["len"]
print(len("hello"))  -- 5

-- Case conversion
local upper = string["upper"]
local lower = string["lower"]
print(upper("hello"))  -- HELLO
print(lower("WORLD"))  -- world

-- Substring (1-indexed, inclusive)
local sub = string["sub"]
print(sub("hello", 2, 4))  -- ell
print(sub("hello", -3))    -- llo (negative = from end)

-- Reverse
local reverse = string["reverse"]
print(reverse("lua"))  -- aul

-- Byte/Char conversion
local byte = string["byte"]
local char = string["char"]
print(byte("A"))      -- 65
print(char(72, 105))  -- Hi
```

## Table Functions

```lua
-- Insert at end
local insert = table["insert"]
local t = {10, 20, 30}
insert(t, 40)
print(t[4])  -- 40

-- Insert at position
insert(t, 2, 15)
print(t[2])  -- 15

-- Remove
local remove = table["remove"]
local val = remove(t, 1)
print(val)  -- 10

-- Concatenate array elements
local concat = table["concat"]
local items = {"a", "b", "c"}
print(concat(items, ", "))  -- a, b, c
print(concat(items))        -- abc (no separator)
```

## Math Functions

```lua
-- Basic operations
local sqrt = math["sqrt"]
local abs = math["abs"]
local floor = math["floor"]
local ceil = math["ceil"]
print(sqrt(16))    -- 4
print(abs(-5))     -- 5
print(floor(3.7))  -- 3
print(ceil(3.2))   -- 4

-- Trigonometry (radians)
local sin = math["sin"]
local cos = math["cos"]
local tan = math["tan"]
print(sin(0))  -- 0
print(cos(0))  -- 1

-- Exponential/Logarithm
local exp = math["exp"]
local log = math["log"]
print(exp(1))  -- 2.718...
print(log(2.718...))  -- ~1

-- Min/Max (variadic)
local min = math["min"]
local max = math["max"]
print(min(5, 2, 8, 1))  -- 1
print(max(5, 2, 8, 1))  -- 8

-- Constants
local pi = math["pi"]
print(pi)  -- 3.14159...
```

## Tips

1. **Cache functions**: Store function references in local variables for repeated use
2. **Argument validation**: Functions check argument types and counts, errors halt execution
3. **Lua semantics**: String indices are 1-based, negative indices count from end
4. **Variadic functions**: `min` and `max` accept any number of arguments

## Running Examples

```bash
cd build
./lua ../tests/stdlib_demo_simple.lua
```

See also:
- `test_string_bracket.lua` - String library tests
- `test_table_bracket.lua` - Table library tests
- `test_math_bracket.lua` - Math library tests
- `STDLIB_IMPLEMENTATION.md` - Technical implementation details
