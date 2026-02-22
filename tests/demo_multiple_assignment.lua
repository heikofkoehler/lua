-- Multiple Assignment Demo

print("=== Multiple Assignment Feature Demo ===")

-- Example 1: Basic multiple local declaration
print("1. Basic multiple local declaration:")
print("   local a, b, c = 1, 2, 3")
local a, b, c = 1, 2, 3
print(a)
print(b)
print(c)

-- Example 2: Padding with nil
print("2. More variables than values (padding):")
print("   local x, y, z = 10, 20")
local x, y, z = 10, 20
print(x)
print(y)
print(z)

-- Example 3: Discarding extras
print("3. More values than variables (discard):")
print("   local m = 100, 200, 300")
local m = 100, 200, 300
print(m)

-- Example 4: Swap idiom
print("4. Variable swap (classic use case):")
local v1, v2 = 1, 2
print("   Before:")
print(v1)
print(v2)
print("   After v1, v2 = v2, v1:")
v1, v2 = v2, v1
print(v1)
print(v2)

-- Example 5: Complex expressions
print("5. Multiple assignment with expressions:")
print("   local sum, diff, prod = 5+3, 10-2, 4*2")
local sum, diff, prod = 5+3, 10-2, 4*2
print(sum)
print(diff)
print(prod)

-- Example 6: Circular rotation
print("6. Circular rotation of values:")
local i, j, k = 1, 2, 3
print("   Before:")
print(i)
print(j)
print(k)
print("   After i, j, k = k, i, j:")
i, j, k = k, i, j
print(i)
print(j)
print(k)

-- Example 7: Global multiple assignment
print("7. Multiple global assignments:")
print("   p, q, r = 5, 6, 7")
p, q, r = 5, 6, 7
print(p)
print(q)
print(r)

print("=== Demo Complete ===")
