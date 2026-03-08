local co = coroutine.create(function()
    local a, b = coroutine.yield(1, 2)
    return a, b
end)

local ok, r1, r2 = coroutine.resume(co)
print("Resume 1:", ok, r1, r2)
assert(ok == true)
assert(r1 == 1)
assert(r2 == 2)

local ok, r1, r2 = coroutine.resume(co, "a", "b")
print("Resume 2:", ok, r1, r2)
assert(ok == true)
assert(r1 == "a")
assert(r2 == "b")
print("Coroutine multi-value test passed!")
