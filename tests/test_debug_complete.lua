-- Complete Debug Library Tests
print("Testing Debug Library...")

-- traceback
local tb = debug.traceback()
assert(type(tb) == "string")
assert(string.find(tb, "stack traceback"))

-- getinfo
local function test_func(a, b, ...) end
local info = debug.getinfo(test_func)
assert(info.nparams == 2)
assert(info.isvararg == true)
assert(info.func == test_func)

-- getlocal/setlocal
local function test_local()
    local x = 10
    local name, val = debug.getlocal(0, 1)
    if name ~= "x" then
        local info = debug.getinfo(0)
        print("getlocal(0, 1) failed: expected 'x', got '" .. tostring(name) .. "', value: " .. tostring(val))
        print("Function at level 0: " .. tostring(info.name) .. " from " .. tostring(info.source))
    end
    assert(name == "x")
    assert(val == 10)
    debug.setlocal(0, 1, 20)
    assert(x == 20)
end
test_local()

-- getupvalue/setupvalue
local up = 42
local function test_up()
    return up
end
local name, val = debug.getupvalue(test_up, 2) -- _ENV is 1
    assert(name == "upvalue_2")
    assert(val == 42)
    debug.setupvalue(test_up, 2, 100)
    assert(test_up() == 100)

-- upvalueid/upvaluejoin
local function f1() return up end
local function f2() return up end
assert(debug.upvalueid(f1, 1) == debug.upvalueid(f2, 1))

-- getregistry
local reg = debug.getregistry()
assert(type(reg) == "table")

-- sethook/gethook
local count = 0
local function hook(event) count = count + 1 end
debug.sethook(hook, "l")
local h, m, c = debug.gethook()
assert(h == hook)
assert(m == "l")
debug.sethook() -- disable
assert(debug.gethook() == nil)

print("Debug Library Tests Passed!")
