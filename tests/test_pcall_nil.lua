local ok, err = pcall(nil)
print("ok:", ok)
print("err:", err)

local function test()
    pcall(nil)
end

ok, err = pcall(test)
print("protected ok:", ok)
print("protected err:", err)
