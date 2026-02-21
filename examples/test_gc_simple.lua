-- Simple GC test
print("=== Simple GC Test ===")

-- Create some objects
local t1 = {x = 1}
local t2 = {y = 2}
local s1 = string.upper("hello")
local s2 = string.lower("WORLD")

print("Created objects:")
print(t1.x)
print(t2.y)
print(s1)
print(s2)

-- Create more to trigger potential GC
local t3 = {a = 1, b = 2, c = 3}
local t4 = {d = 4, e = 5}
local t5 = {f = 6}

print("Created more objects")
print(t3.a)
print(t4.d)
print(t5.f)

print("=== Test Complete ===")
