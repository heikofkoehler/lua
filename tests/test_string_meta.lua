-- Test string object metatable and colon syntax

print("Testing string:len()...")
local s = "hello"
assert(s:len() == 5)
assert(("world"):len() == 5)

print("Testing string:sub()...")
assert(s:sub(1, 2) == "he")
assert(s:sub(-3) == "llo")

print("Testing string:upper() and string:lower()...")
assert(s:upper() == "HELLO")
assert(("LUA"):lower() == "lua")

print("Testing string:reverse()...")
assert(s:reverse() == "olleh")

print("Testing string:byte() and string:char()...")
assert(s:byte(1) == 104) -- 'h'
assert(string.char(s:byte(1)) == "h")

print("Testing getmetatable on strings...")
local mt = getmetatable("")
assert(mt ~= nil)
assert(type(mt) == "table")
assert(mt.__index == string)

print("String metatable tests passed!")
