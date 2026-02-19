# Dot Notation Implementation

## Overview

Added support for dot notation (`.`) to access table fields and call methods, making the standard library and user code more readable and closer to standard Lua syntax.

## Changes Made

### 1. Parser Enhancements
- **Modified `postfix()` method** in `parser.cpp` to handle dot operator
- Dot notation creates an `IndexExprNode` with a string literal key
- Example: `string.len` becomes `IndexExpr(string, "len")`

### 2. AST Node Refactoring
- **Updated `CallExprNode`** in `ast.hpp`
  - Changed from storing function name (string) to storing callee expression
  - Now supports calling any expression, not just named variables
  - Enables patterns like `string.len("hello")` and `table[key](args)`

### 3. Code Generator Updates
- **Modified `visitCall()`** in `codegen.cpp`
  - Compiles callee expression instead of looking up function name
  - Still handles built-in IO functions as special cases (via variable name check)
  - Cleaner separation between expression evaluation and function calls

### 4. Bug Fixes
- **Fixed string pool lookup order** in stdlib functions
  - Runtime strings (TAG_RUNTIME_STRING) must be checked before compile-time strings
  - `isString()` returns true for both types, causing incorrect pool lookups
  - Fixed in: `stdlib_string.cpp`, `stdlib_table.cpp`

## New Capabilities

### Before (Bracket Notation Only)
```lua
local len = string["len"]
print(len("hello"))

local sqrt = math["sqrt"]
print(sqrt(16))
```

### After (Dot Notation Supported)
```lua
-- Direct calls with dot notation
print(string.len("hello"))
print(math.sqrt(16))

-- Chaining works naturally
local data = {user = {name = "Alice"}}
print(data.user.name)

-- Nested calls
print(string.upper(string.reverse("hello")))

-- Both notations work
print(string.len("test"))      -- dot notation
print(string["len"]("test"))   -- bracket notation
```

## Technical Details

### Operator Precedence
Dot notation has the same precedence as bracket notation (postfix level):
1. `primary()` - literals, variables, parentheses
2. `postfix()` - function calls `()`, indexing `[]`, field access `.`
3. `unary()` - negation, logical not
4. ...higher precedence operators

### Parsing Order
For expression `string.len("hello")`:
1. Parse `string` as primary (VariableExprNode)
2. Match `.` token in postfix loop
3. Parse `len` as identifier
4. Create IndexExprNode(string, "len")
5. Match `(` token in postfix loop
6. Parse arguments
7. Create CallExprNode(IndexExpr(...), args)

### Code Generation
For `string.len("hello")`:
```
OP_GET_GLOBAL("string")     # Load string table
OP_CONSTANT("len")          # Load key
OP_GET_TABLE                # Get string["len"] → function value
OP_CONSTANT("hello")        # Load argument
OP_CALL(1)                  # Call function with 1 arg
```

## Compatibility

- **Backward Compatible**: Bracket notation still works
- **First-Class Functions**: Can store and pass functions as values
- **Mixed Usage**: Can combine dot and bracket notation freely

## Examples

See test files:
- `examples/test_dot_notation.lua` - Basic dot notation tests
- `examples/test_mixed_notation.lua` - Mixed bracket/dot usage
- `examples/dot_notation_demo.lua` - Comprehensive feature demo

## Future Enhancements

Potential improvements:
- Colon syntax for method calls: `obj:method(args)` → `obj.method(obj, args)`
- String concatenation operator `..` to make demos more idiomatic
- Local function declarations: `local function foo() ... end`

## Files Modified

1. `src/compiler/ast.hpp` - Updated CallExprNode to accept expression
2. `src/compiler/parser.cpp` - Added dot notation handling
3. `src/compiler/codegen.cpp` - Updated function call code generation
4. `src/vm/stdlib_string.cpp` - Fixed string pool lookup order
5. `src/vm/stdlib_table.cpp` - Fixed string pool lookup order

Total changes: ~50 lines modified, major improvement in usability
