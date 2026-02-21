-- Test manual garbage collection
print("=== Testing collectgarbage() ===")

-- Create some objects
local t1 = {x = 1, y = 2}
local t2 = {a = 10, b = 20}
print("Created objects")

-- Manually trigger GC
print("Calling collectgarbage()")
collectgarbage()
print("GC complete")

-- Verify objects still work
print(t1.x)
print(t2.a)

-- Test stdlib still works after GC
print(string.upper("test"))
print(math.sqrt(25))

print("=== Test Complete ===")
