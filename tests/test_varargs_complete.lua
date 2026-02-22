-- Variadic functions test

print("=== Variadic Functions Test ===")

-- Test 1: Function accepts any number of arguments
function acceptAny(...)
    print(1)
end

print("Test 1: Function with only varargs")
acceptAny()
acceptAny(1)
acceptAny(1, 2, 3)

-- Test 2: Mix of regular params and varargs
function greet(name, ...)
    print(name)
end

print("Test 2: Regular param + varargs")
greet("Alice")
greet("Bob", 1, 2)

-- Test 3: Varargs can be used in expressions
function count(...)
    local x = ...
    print(x)
end

print("Test 3: Using first vararg")
count(42)
count(100, 200, 300)

print("=== All tests complete ===")
