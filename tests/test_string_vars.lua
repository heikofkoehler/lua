local greeting = "Hello"
local name = "Lua"
print(greeting)
print(name)

-- Test string interning (same string should be reused)
local str1 = "test"
local str2 = "test"
print(str1)
print(str2)
