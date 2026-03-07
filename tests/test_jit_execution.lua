-- Test JIT execution

print("Testing JIT...")

-- Simple loop to trigger JIT (hotness threshold is 50 in our VM)
local function tight_loop(n)
    local sum = 0
    for i = 1, n do
        sum = sum + i
    end
    return sum
end

-- Test with JIT on (default)
print("Running with JIT on...")
local res1 = tight_loop(100)
print("Result 1:", res1)
assert(res1 == 5050)

-- Turn JIT off via vm_jit("off")
print("Turning JIT off...")
vm_jit("off")
assert(vm_jit() == false)

local function loop2(n)
    local prod = 1
    for i = 1, n do
        prod = prod * 2
    end
    return prod
end

print("Running with JIT off...")
local res2 = loop2(10)
print("Result 2:", res2)
assert(res2 == 1024)

-- Turn JIT back on
print("Turning JIT back on...")
vm_jit("on")
assert(vm_jit() == true)

-- Test complex arithmetic in JIT
local function complex_math(a, b)
    local r1 = a * b
    local r2 = a / b
    local r3 = a % b
    local r4 = a // b
    local r5 = -a
    return r1, r2, r3, r4, r5
end

print("Testing complex arithmetic in JIT...")
-- Trigger JIT (hotness is 50)
for i = 1, 60 do
    complex_math(10, 3)
end

local r1, r2, r3, r4, r5 = complex_math(10, 3)
print("Results:", r1, r2, r3, r4, r5)
assert(r1 == 30)
assert(math.abs(r2 - 3.3333333333333) < 0.0001)
assert(r3 == 1)
assert(r4 == 3)
assert(r5 == -10)

print("JIT execution tests passed!")
