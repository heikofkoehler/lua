-- Test bitwise operators
print("--- Bitwise AND ---")
print(5 & 3)   -- 101 & 011 = 001 (1)
print(5.0 & 3.0) -- 101 & 011 = 001 (1)
assert(not pcall(function() return 5.5 & 3.1 end))
print(12 & 7)  -- 1100 & 0111 = 0100 (4)

print("--- Bitwise OR ---")
print(5 | 3)   -- 101 | 011 = 111 (7)
print(12 | 7)  -- 1100 | 0111 = 1111 (15)

print("--- Bitwise XOR ---")
print(5 ~ 3)   -- 101 ^ 011 = 110 (6)
print(12 ~ 7)  -- 1100 ^ 0111 = 1011 (11)

print("--- Bitwise NOT ---")
print(~5)      -- ~0101 = ...1010 (should be -6)

print("--- Bitwise Shifts ---")
print(1 << 3)  -- 8
print(16 >> 2) -- 4
-- In 64-bit Lua, -1 is 0xFFFFFFFFFFFFFFFF.
-- Logical shift right by 1 should be 0x7FFFFFFFFFFFFFFF (9223372036854775807).
print(-1 >> 1)

print("--- Integer Division ---")
print(10 // 3)  -- 3
print(10 // -3) -- -4 (Lua floor division)
print(-10 // 3) -- -4

print("--- Precedence ---")
print(5 + 3 << 1) -- (5 + 3) << 1 = 16
print(5 | 3 & 1)  -- 5 | (3 & 1) = 5 | 1 = 5
print(10 // 2 * 3) -- (10 // 2) * 3 = 15
