-- Debug varargs

function test(a, ...)
    print("Parameter a:")
    print(a)
    print("Varargs:")
    print(...)
end

test("first", "second", "third")
