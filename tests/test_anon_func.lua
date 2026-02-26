-- Test anonymous functions

print("Testing simple anonymous function...")
local f = function(a, b) return a + b end
assert(f(10, 20) == 30)
print("OK")

print("Testing anonymous function in table...")
local t = {
    add = function(a, b) return a + b end,
    sub = function(a, b) return a - b end
}
assert(t.add(5, 5) == 10)
assert(t.sub(10, 3) == 7)
print("OK")

print("Testing anonymous function as argument...")
function apply(f, x, y)
    return f(x, y)
end

local res = apply(function(a, b) return a * b end, 6, 7)
assert(res == 42)
print("OK")

print("Testing closure with anonymous function...")
function counter()
    local count = 0
    return function()
        count = count + 1
        return count
    end
end

local c = counter()
assert(c() == 1)
assert(c() == 2)
assert(c() == 3)
print("OK")

print("Testing coroutine.wrap...")
local next_val = coroutine.wrap(function(start)
    coroutine.yield(start + 1)
    coroutine.yield(start + 2)
    return start + 3
end)

assert(next_val(10) == 11)
assert(next_val() == 12)
assert(next_val() == 13)
print("OK")

print("Anonymous functions OK")
