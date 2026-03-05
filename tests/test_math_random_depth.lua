-- Test math.random depth
print("Testing math.random edge cases...")

-- 1. Error for n > m
local ok, err = pcall(function() math.random(10, 1) end)
assert(ok == false)
assert(string.find(err, "interval is empty"))
print("OK: n > m error")

-- 2. Large integer range
local min = math.mininteger
local max = math.maxinteger
-- math.random(min, max) might be too huge for some implementations, 
-- but let's try a large sub-range
local r1 = math.random(max - 100, max)
assert(r1 >= max - 100 and r1 <= max)
print("OK: large integer range")

-- 3. math.random() without args
local r2 = math.random()
assert(r2 >= 0 and r2 < 1)
print("OK: random float")

print("math.random depth tests passed!")
