-- Test __tostring metamethod

print("Testing __tostring...")
local t = setmetatable({}, {
    __tostring = function()
        return "custom table"
    end
})

assert(tostring(t) == "custom table")
print("print(t) should show custom table:")
print(t)
print("OK")

print("Testing __tostring for numbers (should not work in standard Lua without debug.setmetatable, but let's check our impl)...")
-- In standard Lua, you can't set metamethods for numbers directly.
-- But our VM might allow it via getTypeMetatable or debug.setmetatable.
debug.setmetatable(1, {
    __tostring = function(n)
        return "number " .. n
    end
})
assert(tostring(42) == "number 42")
print("OK")

print("All __tostring tests passed!")
