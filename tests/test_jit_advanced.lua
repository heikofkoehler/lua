-- Advanced JIT tests: arithmetic, control flow, and nested loops

print("Testing advanced JIT features...")

-- 1. Tight nested loops
local function nested_loops(n)
    local count = 0
    for i = 1, n do
        for j = 1, n do
            count = count + 1
        end
    end
    return count
end

print("Running nested loops...")
for i = 1, 10 do nested_loops(1) end -- warm up
local res1 = nested_loops(50)
print("Nested loop result:", res1)
assert(res1 == 2500)

-- 2. Boolean logic and branching
local function logic_test(a, b)
    if a and b then
        return "both"
    elseif a or b then
        return "one"
    else
        return "none"
    end
end

print("Running logic tests...")
for i = 1, 60 do logic_test(true, true) end -- Trigger JIT
assert(logic_test(true, true) == "both")
assert(logic_test(true, false) == "one")
assert(logic_test(false, true) == "one")
assert(logic_test(false, false) == "none")

-- 3. Recursive calls within JITted code
local function fib(n)
    if n < 2 then return n end
    return fib(n - 1) + fib(n - 2)
end

print("Running recursive fib...")
-- Warm up
for i = 1, 10 do fib(5) end
local f10 = fib(10)
print("fib(10) =", f10)
assert(f10 == 55)

-- 4. JIT on/off toggling within a function (edge case)
local function mixed_jit(enable)
    local s = 0
    for i = 1, 100 do
        if i == 50 then
            vm_jit(enable)
        end
        s = s + i
    end
    return s
end

print("Testing dynamic JIT toggling...")
vm_jit("on")
assert(mixed_jit(false) == 5050)
assert(vm_jit() == false)
vm_jit("on")
assert(mixed_jit(true) == 5050)
assert(vm_jit() == true)

print("Advanced JIT tests passed!")
