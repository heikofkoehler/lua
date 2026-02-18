-- Test script for Lua VM MVP
-- Tests basic arithmetic and print functionality

print(2 + 3)
print(10 - 4)
print(5 * 6)
print(20 / 4)
print(-15)
print((2 + 3) * 4)

-- Test operator precedence
print(2 + 3 * 4)
print((2 + 3) * 4)

-- Test power operator
print(2 ^ 3)
print(2 ^ 3 ^ 2)

-- Test modulo
print(10 % 3)

-- Test comparison
print(5 < 10)
print(5 > 10)
print(5 == 5)
print(5 != 3)

-- Test boolean literals
print(true)
print(false)
print(nil)
