print("=== Multiple Assignment Tests ===")

-- Test 1: Basic multiple local declaration
local a, b, c = 1, 2, 3
print(a)  -- 1
print(b)  -- 2
print(c)  -- 3

-- Test 2: More variables than values (pad with nil)
local x, y, z = 10, 20
print(x)  -- 10
print(y)  -- 20
print(z)  -- nil

-- Test 3: More values than variables (discard extras)
local m = 100, 200, 300
print(m)  -- 100

-- Test 4: Multiple global assignment
p, q, r = 5, 6, 7
print(p)  -- 5
print(q)  -- 6
print(r)  -- 7

-- Test 5: Swap idiom
local v1, v2 = 1, 2
v1, v2 = v2, v1
print(v1)  -- 2
print(v2)  -- 1

-- Test 6: All nil initialization
local n1, n2, n3
print(n1)  -- nil
print(n2)  -- nil
print(n3)  -- nil

-- Test 7: Mixed expressions
local e1, e2, e3 = 1 + 1, 2 * 3, 4 - 1
print(e1)  -- 2
print(e2)  -- 6
print(e3)  -- 3

print("=== Tests Complete ===")
