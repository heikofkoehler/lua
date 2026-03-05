-- Test integer overflow wrap-around (Lua 5.4 style)
print("Testing integer overflow...")

local max = math.maxinteger
local min = math.mininteger

-- 1. Addition overflow
assert(max + 1 == min)
print("OK: Addition overflow")

-- 2. Subtraction underflow
assert(min - 1 == max)
print("OK: Subtraction underflow")

-- 3. Multiplication overflow
assert(max * 2 == -2) 
-- (2^63 - 1) * 2 = 2^64 - 2. In 64-bit 2's complement this is -2.
print("OK: Multiplication overflow")

-- 4. Negation of mininteger
assert(-min == min)
print("OK: Negation overflow")

print("Integer overflow tests passed!")
