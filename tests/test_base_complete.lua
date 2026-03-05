-- Complete Base Library Tests
print("Testing Base Library...")

-- Basic functions
assert(type(42) == "number")
assert(type(4.2) == "number")
assert(type("hi") == "string")
assert(type({}) == "table")
assert(type(print) == "function")
assert(type(nil) == "nil")
assert(type(true) == "boolean")
assert(type(coroutine.create(function() end)) == "thread")

assert(tostring(42) == "42")
assert(tonumber("123") == 123)
assert(tonumber("123.45") == 123.45)
assert(tonumber("ff", 16) == 255)

assert(assert(true, "err") == true)
local ok, err = pcall(function() error("fail") end)
assert(ok == false)
assert(string.find(err, "fail"))

-- Metatables
local t = {}
local mt = { __index = { a = 1 } }
setmetatable(t, mt)
assert(getmetatable(t) == mt)
assert(t.a == 1)

-- Raw access
local t2 = { a = 1 }
setmetatable(t2, { __index = function() return 2 end })
assert(t2.a == 1)
assert(t2.b == 2)
assert(rawget(t2, "b") == nil)
rawset(t2, "c", 3)
assert(t2.c == 3)
assert(rawlen("abc") == 3)
assert(rawequal(t2, t2) == true)
assert(rawequal(t2, {}) == false)

-- Select
assert(select("#", 1, 2, 3) == 3)
assert(select(2, "a", "b", "c") == "b")

-- Pairs/Ipairs
local t3 = { a = 1, b = 2 }
local count = 0
for k, v in pairs(t3) do count = count + 1 end
assert(count == 2)

local t4 = { 10, 20, 30 }
local sum = 0
for i, v in ipairs(t4) do sum = sum + v end
assert(sum == 60)

-- Next
local k, v = next(t3)
assert(k ~= nil)

-- Version and _G
assert(type(_VERSION) == "string")
assert(_G == _G._G)
assert(_G.print == print)

-- Load/Dofile
local f = load("return 1+1")
assert(f() == 2)

local test_file = "tests/temp_base_test.lua"
local fh = io.open(test_file, "w")
fh:write("return 42")
fh:close()
assert(dofile(test_file) == 42)
os.remove(test_file)

-- Collectgarbage
collectgarbage("collect")
local count1 = collectgarbage("count")
assert(count1 > 0)

print("Base Library Tests Passed!")
