-- test_require.lua
print("Testing require...")

-- Test package.path
package.path = package.path .. ";tests/?.lua"

-- Test loading module
local mymod = require("mymodule")
print(mymod.hello("World"))
if mymod.value ~= 42 then
    print("FAIL: mymod.value (expected 42, got " .. tostring(mymod.value) .. ")")
end

-- Test package.loaded
local mymod2 = require("mymodule")
if mymod ~= mymod2 then
    print("FAIL: require should return the same table from package.loaded")
end

print("DONE")
