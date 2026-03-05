-- Complete Coroutine Library Tests
print("Testing Coroutine Library...")

-- Basic operations
local co = coroutine.create(function(a, b)
    local x = coroutine.yield(a + b)
    return x * 2
end)

assert(coroutine.status(co) == "suspended")
local ok, res = coroutine.resume(co, 10, 20)
assert(ok == true)
assert(res == 30)
assert(coroutine.status(co) == "suspended")

local ok2, res2 = coroutine.resume(co, 5)
assert(ok2 == true)
assert(res2 == 10)
assert(coroutine.status(co) == "dead")

-- isyieldable
assert(coroutine.isyieldable() == false) -- main thread
local co2 = coroutine.create(function()
    assert(coroutine.isyieldable() == true)
end)
coroutine.resume(co2)

-- wrap
local f = coroutine.wrap(function(x)
    return x + 1
end)
assert(f(10) == 11)

-- running
local co_running = coroutine.running()
assert(type(co_running) == "thread")

-- close
local co3 = coroutine.create(function() 
    coroutine.yield()
end)
coroutine.resume(co3)
assert(coroutine.status(co3) == "suspended")
coroutine.close(co3)
assert(coroutine.status(co3) == "dead")

print("Coroutine Library Tests Passed!")
