-- test_coroutine_complex.lua
print("Testing complex coroutines...")

-- 1. Producer-Consumer Pattern
function producer_body(n)
    for i = 1, n do
        coroutine.yield(i * 10)
    end
end

function producer(n)
    local co = coroutine.create(producer_body)
    return co
end

print("Testing Producer-Consumer...")
local p = producer(5)
local sum = 0
-- First resume passes 'n'
local ok, val = coroutine.resume(p, 5)
while ok and coroutine.status(p) ~= "dead" do
    if val then
        sum = sum + val
        print("Received:", val)
    end
    ok, val = coroutine.resume(p)
end

if sum ~= 150 then
    print("FAIL: Producer-Consumer sum (expected 150, got " .. tostring(sum) .. ")")
else
    print("PASS: Producer-Consumer")
end

-- 2. Deep Recursion then Yield
function recursive_yield(depth, max_depth)
    if depth >= max_depth then
        -- Return values passed to resume
        local ret = coroutine.yield("reached depth " .. tostring(depth))
        return ret
    end
    return recursive_yield(depth + 1, max_depth)
end

print("Testing Deep Stack Yield...")
function depth_worker()
    return recursive_yield(1, 20)
end

local co_depth = coroutine.create(depth_worker)

local ok, msg = coroutine.resume(co_depth)
print("Yielded message:", msg)
local ok2, final = coroutine.resume(co_depth, "continuation_value")
print("Final return:", final)

if final ~= "continuation_value" then
    print("FAIL: yield return value (expected continuation_value, got " .. tostring(final) .. ")")
else
    print("PASS: Deep Stack Yield")
end

print("DONE complex coroutines")
