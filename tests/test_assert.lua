-- Test assert() function

print("Testing assert(true)...")
assert(true)
print("OK")

print("Testing assert(100)...")
assert(100)
print("OK")

print('Testing assert("hello")...')
assert("hello")
print("OK")

print("Testing assert return values...")
local a, b, c = assert(1, 2, 3)
-- Print results for debugging
print("a:", a, "b:", b, "c:", c)

if a == 1 and b == 2 and c == 3 then
    print("Return values OK")
else
    print("Return values FAILED")
end

-- Positive test for custom message
assert(true, "this should not fail")

print("All positive assert tests passed!")
