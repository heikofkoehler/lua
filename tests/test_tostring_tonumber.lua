-- Test tostring and tonumber edge cases

print("Testing tostring/tonumber edge cases...")

-- 1. tonumber bases
assert(tonumber("1010", 2) == 10)
assert(tonumber("123", 10) == 123)
assert(tonumber("FF", 16) == 255)
assert(tonumber("ff", 16) == 255)
assert(tonumber("10", 36) == 36)
assert(tonumber("z", 36) == 35)

-- 2. tonumber invalid inputs
assert(tonumber("xyz") == nil)
assert(tonumber("123", 2) == nil)
assert(pcall(tonumber, "123", 37) == false) -- invalid base
assert(pcall(tonumber, "123", 1) == false)  -- invalid base

-- 3. tostring non-numbers
assert(tostring(nil) == "nil")
assert(tostring(true) == "true")
assert(tostring(false) == "false")
assert(tostring("hello") == "hello")

-- 4. tostring with metamethod
local t = {}
setmetatable(t, { __tostring = function() return "special table" end })
assert(tostring(t) == "special table")

-- 5. Multiple arguments to tonumber
assert(tonumber("  123  ") == 123)
assert(tonumber("0x10") == 16)

print("tostring/tonumber tests passed!")
