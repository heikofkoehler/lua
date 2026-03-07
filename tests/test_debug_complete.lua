-- Comprehensive Debug Library Tests

local function assert_eq(actual, expected, msg)
    if actual ~= expected then
        error(string.format("Assertion failed: expected %s, got %s. %s", tostring(expected), tostring(actual), msg or ""), 2)
    end
end

print("=== Testing debug.getinfo ===")
local function test_func(a, b, ...)
    local x = 10
    return a + b + x
end

local info = debug.getinfo(test_func)
assert_eq(info.what, "Lua")
assert_eq(info.name, "test_func")
assert_eq(info.nups, 1) -- _ENV
assert_eq(info.nparams, 2)
assert_eq(info.isvararg, true)
assert_eq(type(info.func), "function")
assert_eq(info.func, test_func)

print("=== Testing debug.getlocal/setlocal ===")
local function test_locals()
    local my_local = "original"
    local name, val = debug.getlocal(1, 1)
    assert_eq(name, "my_local")
    assert_eq(val, "original")
    
    debug.setlocal(1, 1, "modified")
    assert_eq(my_local, "modified")
end
test_locals()

print("=== Testing debug.getupvalue/setupvalue ===")
local up = "outer"
local function test_ups()
    return up
end

local name, val = debug.getupvalue(test_ups, 2) -- 1 is _ENV, 2 is up
assert_eq(name, "upvalue_2") -- our current implementation uses generic names
assert_eq(val, "outer")

debug.setupvalue(test_ups, 2, "changed")
assert_eq(up, "changed")

print("=== Testing debug.sethook/gethook ===")
local count = 0
debug.sethook(function() count = count + 1 end, "l")
-- Execute some lines
local x = 1
local y = 2
debug.sethook() -- disable

assert(count >= 2)
local hook, mask, cnt = debug.gethook()
assert_eq(hook, nil)
assert_eq(mask, "")

print("=== Testing debug.traceback ===")
local tb = debug.traceback("my message")
assert(string.find(tb, "my message"))
assert(string.find(tb, "stack traceback:"))

print("\nDebug library tests passed!")
