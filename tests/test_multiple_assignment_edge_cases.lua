print("=== Edge Case Tests ===")

-- Test 1: Single variable (should still work)
local single = 42
print(single)  -- 42

-- Test 2: Many variables
local a1, a2, a3, a4, a5 = 1, 2, 3, 4, 5
print(a1)  -- 1
print(a5)  -- 5

-- Test 3: No initializers (all nil)
local uninit1, uninit2
print(uninit1)  -- nil
print(uninit2)  -- nil

-- Test 4: Complex expressions in values
local sum, diff, prod = 5 + 3, 10 - 2, 4 * 2
print(sum)   -- 8
print(diff)  -- 8
print(prod)  -- 8

-- Test 5: Reassignment of locals
local x, y = 1, 2
x, y = 10, 20
print(x)  -- 10
print(y)  -- 20

-- Test 6: Mixed local and global
local loc1, loc2 = 100, 200
glob1, glob2 = 300, 400
print(loc1)   -- 100
print(glob1)  -- 300

-- Test 7: Chain of swaps
local i, j, k = 1, 2, 3
i, j, k = k, i, j
print(i)  -- 3
print(j)  -- 1
print(k)  -- 2

print("=== Edge Cases Complete ===")
