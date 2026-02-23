-- test_coroutine.lua
print("Testing coroutines...")

function worker(a, b)
    print("Coroutine started", a, b)
    local c = coroutine.yield(a + b, "yielded")
    print("Coroutine resumed with", c)
    return a * b, "finished"
end

local co = coroutine.create(worker)

print("Status:", coroutine.status(co))

local ok, res1, res2 = coroutine.resume(co, 10, 20)
print("Resume 1:", ok, res1, res2)
print("Status:", coroutine.status(co))

local ok2, res3, res4 = coroutine.resume(co, 100)
print("Resume 2:", ok2, res3, res4)
print("Status:", coroutine.status(co))

print("DONE")
