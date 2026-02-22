-- Test garbage collection
print("=== Testing Garbage Collector ===")

-- Create many strings to trigger GC
print("Creating many strings...")
local i = 0
while i < 1000 do
    local s = string.upper("test string number")
    i = i + 1
end
print("Created 1000 strings")

-- Create many tables
print("Creating many tables...")
local j = 0
while j < 500 do
    local t = {x = j, y = j * 2}
    j = j + 1
end
print("Created 500 tables")

-- Test that GC doesn't break functionality
print("Testing functionality after GC...")
print(string.len("hello"))
print(math.sqrt(16))
local data = {a = 1, b = 2}
print(data.a)

print("=== GC Test Complete ===")
