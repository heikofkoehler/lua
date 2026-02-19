-- Test variadic functions

-- Test 1: Simple varargs function
function printAll(...)
    print(...)
end

print("Test 1: Simple varargs")
printAll(1)
printAll(2)
printAll(3)

-- Test 2: Function with regular params and varargs
function greet(name, ...)
    print(name)
    print(...)
end

print("Test 2: Mixed params and varargs")
greet("Alice", "Hello", "World")

-- Test 3: No varargs passed
function optional(...)
    print(...)
end

print("Test 3: No varargs")
optional()
