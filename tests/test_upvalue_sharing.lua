-- Test upvalue sharing across coroutines
local x = 10

local co = coroutine.create(function()
    print("Coroutine: x =", x)
    x = x + 1
    print("Coroutine: x incremented to", x)
end)

print("Main: x =", x)
coroutine.resume(co)
print("Main: x after resume =", x)

if x == 11 then
    print("SUCCESS: Upvalue shared correctly")
else
    print("FAILURE: Upvalue NOT shared correctly")
end

-- Test multiple closures sharing same upvalue across coroutines
local y = 20
local function inc_y() y = y + 1 end

local co2 = coroutine.create(function()
    print("Coroutine 2: y =", y)
    inc_y()
    print("Coroutine 2: y incremented via function to", y)
end)

coroutine.resume(co2)
print("Main: y after resume =", y)

if y == 21 then
    print("SUCCESS: Shared closure upvalue working")
else
    print("FAILURE: Shared closure upvalue NOT working")
end
