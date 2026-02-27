-- Advanced coroutine tests

print("Testing coroutine yield/resume with multiple values...")
local co = coroutine.create(function(a, b)
    print("co: started with", a, b)
    local r1, r2 = coroutine.yield(a + b, a - b)
    print("co: resumed with", r1, r2)
    return r1 * r2, r1 / r2
end)

local s1, v1, v2 = coroutine.resume(co, 10, 5)
assert(s1 == true)
assert(v1 == 15)
assert(v2 == 5)

local s2, v3, v4 = coroutine.resume(co, 8, 2)
assert(s2 == true)
assert(v3 == 16)
assert(v4 == 4)
assert(coroutine.status(co) == "dead")

print("Testing coroutine with varargs...")
local co2 = coroutine.create(function(...)
    local args = table.pack(...)
    coroutine.yield(args.n)
    return ...
end)

local s3, n = coroutine.resume(co2, "a", "b", "c")
assert(s3 == true)
assert(n == 3)

local s4, a, b, c = coroutine.resume(co2)
assert(s4 == true)
assert(a == "a")
assert(b == "b")
assert(c == "c")

print("Testing coroutine.wrap...")
local f = coroutine.wrap(function(n)
    for i = 1, n do
        coroutine.yield(i * i)
    end
    return "done"
end)

assert(f(3) == 1)
assert(f() == 4)
assert(f() == 9)
assert(f() == "done")

print("Advanced coroutine tests passed!")
