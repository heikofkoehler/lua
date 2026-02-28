-- Test userdata support
print("=== Testing Userdata ===")

local ud = __test_userdata()
print("Type of ud:", type(ud)) -- should be "userdata"

local mt = {
    __index = function(obj, key)
        if key == "hello" then
            return "world"
        end
        return nil
    end,
    __add = function(a, b)
        return "added"
    end
}

debug.setmetatable(ud, mt)

print("ud.hello:", ud.hello) -- should be "world"
print("ud + 1:", ud + 1)     -- should be "added"

-- Test gc
local weak = setmetatable({}, {__mode = "v"})
weak[1] = ud
print("Before GC, weak[1]:", type(weak[1]))
ud = nil
collectgarbage()
print("After GC, weak[1]:", type(weak[1])) -- should be nil
