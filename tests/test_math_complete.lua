-- Complete Math Library Tests
print("Testing Math Library...")

-- Basic functions
assert(math.sqrt(16) == 4)
assert(math.abs(-5) == 5)
assert(math.abs(5) == 5)
assert(math.abs(-5.5) == 5.5)
assert(math.floor(3.7) == 3)
assert(math.floor(-3.7) == -4)
assert(math.ceil(3.2) == 4)
assert(math.ceil(-3.2) == -3)

-- Trigonometry
assert(math.sin(0) == 0)
assert(math.cos(0) == 1)
assert(math.tan(0) == 0)
assert(math.asin(0) == 0)
assert(math.acos(1) == 0)
assert(math.atan(0) == 0)
assert(math.deg(math.pi) == 180)
assert(math.rad(180) == math.pi)

-- Logarithms and Exponents
assert(math.exp(0) == 1)
assert(math.log(math.exp(1)) == 1)
assert(math.log(100, 10) == 2)

-- Min/max
assert(math.min(5, 2, 8, 1) == 1)
assert(math.max(5, 2, 8, 1) == 8)

-- Fmod and Modf
assert(math.fmod(10, 3) == 1)
assert(math.fmod(-10, 3) == -1)
local i, f = math.modf(3.5)
assert(i == 3)
assert(f == 0.5)

-- Constants
assert(math.pi > 3.14 and math.pi < 3.15)
assert(math.huge > 1e300)
assert(math.maxinteger == 2147483647)
assert(math.mininteger == -2147483648)

-- Integer utilities
assert(math.type(1) == "integer")
assert(math.type(1.5) == "float")
assert(math.type("1") == nil)
assert(math.tointeger(1.0) == 1)
assert(math.tointeger(1.5) == nil)
assert(math.tointeger("1") == nil)
assert(math.ult(1, 2) == true)
assert(math.ult(2, 1) == false)
assert(math.ult(-1, 1) == false) -- -1 is large when unsigned

-- Random
local s1, s2 = math.randomseed(42, 100)
assert(type(s1) == "number")
assert(type(s2) == "number")
local r1 = math.random()
assert(r1 >= 0 and r1 < 1)
local r2 = math.random(10)
assert(r2 >= 1 and r2 <= 10)
local r3 = math.random(10, 20)
assert(r3 >= 10 and r3 <= 20)

print("Math Library Tests Passed!")
