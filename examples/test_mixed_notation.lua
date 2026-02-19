-- Test mixing dot notation with other features
print("=== Mixed Notation Tests ===")

-- Test 1: Dot notation and bracket notation both work
print("Test 1: Both notations:")
print(string.len("hello"))
print(string["len"]("hello"))

-- Test 2: Store function in variable and call it
print("Test 2: Variable storage:")
local len = string.len
print(len("world"))

-- Test 3: Chain operations
print("Test 3: Chaining:")
local t = {x = {y = 10}}
print(t.x.y)
print(t["x"]["y"])
print(t.x["y"])
print(t["x"].y)

-- Test 4: Nested calls with dot notation
print("Test 4: Nested calls:")
print(string.upper(string.reverse("hello")))

-- Test 5: Math operations with dot notation
print("Test 5: Math operations:")
print(math.sqrt(math.abs(-16)))

print("=== All tests passed! ===")
