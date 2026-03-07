-- Test NaN and Infinity handling in Value representation

print("Testing NaN and Infinity...")

local inf = math.huge
local ninf = -math.huge
local nan = 0/0

-- Basic type checks
assert(type(inf) == "number")
assert(type(ninf) == "number")
assert(type(nan) == "number")

-- Infinity checks
assert(inf > 1e300)
assert(ninf < -1e300)
assert(inf == math.huge)
assert(ninf == -math.huge)
assert(inf ~= ninf)

-- Arithmetic with infinity
assert(inf + 1 == inf)
assert(inf * 2 == inf)
assert(inf / 2 == inf)
assert(1 / inf == 0)
assert(1 / ninf == 0)

-- NaN checks
-- In IEEE 754, nan ~= nan
assert(nan ~= nan)
assert(nan ~= 0)
assert(nan ~= inf)

-- tostring checks
assert(tostring(inf) == "inf")
assert(tostring(ninf) == "-inf")
assert(tostring(nan) == "nan")

print("NaN and Infinity tests passed!")
