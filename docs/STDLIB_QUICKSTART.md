# Standard Library Quick Start Guide

The Lua VM provides a comprehensive standard library. Functions are organized into namespaces (tables) and can be accessed using standard Lua dot notation.

## String Library

```lua
local s = "hello world"

print(string.len(s))       -- 11
print(string.upper(s))     -- HELLO WORLD
print(string.sub(s, 1, 5)) -- hello

-- Pattern Matching
print(string.match(s, "%w+")) -- hello
for word in string.gmatch(s, "%w+") do
    print(word)
end

-- Method syntax (available for strings)
print(s:reverse()) -- dlrow olleh
```

## Table Library

```lua
local t = {10, 20, 30}

table.insert(t, 40)    -- Append 40
table.insert(t, 1, 5)  -- Insert 5 at index 1
table.remove(t, 2)     -- Remove element at index 2

print(table.concat(t, ", ")) -- 5, 20, 30, 40
```

## Math Library

```lua
print(math.sqrt(16)) -- 4.0
print(math.sin(math.pi / 2)) -- 1.0
print(math.floor(3.7)) -- 3
print(math.random(1, 10)) -- Random integer between 1 and 10
```

## I/O Library

```lua
local f = io.open("test.txt", "w")
f:write("Hello Lua!")
f:close()

local f2 = io.open("test.txt", "r")
local content = f2:read("*a")
print(content) -- Hello Lua!
f2:close()
```

## OS Library

```lua
print(os.date()) -- Current date/time string
print(os.time()) -- Current timestamp
os.remove("test.txt")
```

## Debug Library

```lua
-- Print a stack traceback
print(debug.traceback())

-- Get info about a function
local info = debug.getinfo(print)
print(info.name) -- print
```

## Tips

1. **Dot Notation**: Use `math.sin(x)` instead of `math["sin"](x)`.
2. **Method Syntax**: Use `obj:method()` for file and string objects.
3. **1-Based Indexing**: All Lua libraries use 1-based indexing for strings and tables.
4. **Error Handling**: Use `pcall` to wrap standard library calls that might fail.
