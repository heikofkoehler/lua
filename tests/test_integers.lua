-- Test integer types

local x = 10
local y = 20

-- Basic arithmetic
assert(x + y == 30)
assert(x - y == -10)
assert(x * y == 200)

-- Modulo
assert(y % 3 == 2)
assert(-y % 3 == 1)

-- Types
assert(type(x) == "number")

-- Exceeding 48 bits (will fall back to float)
local big = 150000000000000
assert(big > 0)
assert(type(big) == "number")

print("Integer tests passed!")
