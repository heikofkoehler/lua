-- Test debug.getinfo

print("Testing debug.getinfo Lua...")
local function test(a, b, ...)
    local x = 10
    local info = debug.getinfo(1)
    assert(info.what == "Lua")
    assert(info.name == "test")
    assert(info.nups == 1) -- captures _ENV
    assert(info.nparams == 2)
    assert(info.isvararg == true)
    print("OK")
end

test(1, 2, 3)

print("Testing debug.getinfo C...")
local info = debug.getinfo(print)
assert(info.what == "C")
print("OK")

print("All debug.getinfo tests passed!")
