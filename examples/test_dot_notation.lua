-- Test dot notation for standard library functions
print("=== Testing Dot Notation ===")

-- String library with dot notation
print("String functions:")
print(string.len("hello"))
print(string.upper("world"))
print(string.lower("HELLO"))
print(string.reverse("lua"))
print(string.sub("hello", 2, 4))

-- Table library with dot notation
print("Table functions:")
local t = {10, 20, 30}
table.insert(t, 40)
print(t[4])

table.insert(t, 2, 15)
print(t[2])

local removed = table.remove(t, 1)
print(removed)

local items = {"a", "b", "c"}
print(table.concat(items, ", "))

-- Math library with dot notation
print("Math functions:")
print(math.sqrt(16))
print(math.abs(-42))
print(math.floor(3.7))
print(math.ceil(3.2))
print(math.sin(0))
print(math.cos(0))
print(math.min(5, 2, 8, 1))
print(math.max(5, 2, 8, 1))
print(math.pi)

print("=== All tests passed! ===")
