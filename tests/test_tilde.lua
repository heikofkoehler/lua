-- Test ~= operator (Lua's not-equal)

print(5 ~= 3)      -- true (5 is not equal to 3)
print(5 ~= 5)      -- false (5 equals 5)
print(true ~= false)  -- true
print(nil ~= nil)  -- false

-- Test that != still works (for backward compatibility)
print(5 != 3)      -- true
print(5 != 5)      -- false

-- Mixed with other operators
print((2 + 3) ~= 5)  -- false
print((2 + 2) ~= 5)  -- true
