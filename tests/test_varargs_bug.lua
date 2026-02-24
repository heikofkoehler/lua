print("=== Varargs Bug Test ===")

function test_vararg_locals(...)
    local dummy = 999
    local x = ...
    print(dummy)
    print(x)
end

test_vararg_locals(100, 200, 300)
