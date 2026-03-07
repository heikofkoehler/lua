-- Test xpcall with arguments

print("Testing xpcall with arguments...")

local function test_func(a, b)
    assert(a == 10)
    assert(b == 20)
    return a + b
end

local function handler(err)
    return "error: " .. tostring(err)
end

local ok, res = xpcall(test_func, handler, 10, 20)
assert(ok == true)
assert(res == 30)

local function fail_func(a, b)
    error("failed with " .. a)
end

local ok2, res2 = xpcall(fail_func, handler, "test", 123)
print("DEBUG res2:", res2)
assert(ok2 == false)
assert(string.find(res2, "error: "))
assert(string.find(res2, "failed with test"))

print("xpcall arguments tests passed!")
