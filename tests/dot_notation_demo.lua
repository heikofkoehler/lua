-- Comprehensive Dot Notation Demo
print("=== Dot Notation Feature Demo ===")
print("")

-- Feature 1: Clean, readable syntax
print("1. Clean syntax with dot notation:")
print(string.len("hello"))
print(math.sqrt(16))
print("")

-- Feature 2: Works alongside bracket notation
print("2. Both notations work:")
print(string.len("test"))
print(string["len"]("test"))
print("")

-- Feature 3: Function chaining
print("3. Chaining table access:")
local data = {user = {name = "Alice", age = 30}}
print(data.user.name)
print(data.user.age)
print("")

-- Feature 4: Nested function calls
print("4. Nested stdlib calls:")
local text = "hello world"
print(text)
print(string.reverse(text))
print(string.upper(text))
print(string.upper(string.reverse(text)))
print("")

-- Feature 5: Store and call functions
print("5. Functions as first-class values:")
local sqrt = math.sqrt
local abs = math.abs
print(sqrt(25))
print(abs(-10))
print("")

-- Feature 6: Complex expressions
print("6. Complex expressions:")
local numbers = {5, 2, 8, 1, 9}
print(math.min(5, 2, 8, 1, 9))
print(math.max(5, 2, 8, 1, 9))
table.insert(numbers, math.floor(math.sqrt(16)))
print(numbers[6])
print("")

-- Feature 7: String manipulation
print("7. String manipulation:")
local message = "  LUA  "
print(message)
print(string.lower(message))
print(string.reverse(string.lower(message)))
print("")

print("=== All features demonstrated successfully! ===")
