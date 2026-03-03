-- Comprehensive test for multiple return values

-- Test 1: Basic multiple returns
function getTwoNumbers()
    return 10, 20
end

function getThreeNumbers()
    return 100, 200, 300
end

-- Test 2: Assignment (takes only first value)
local a = getTwoNumbers()
print(a)  -- Should print 10

local b = getThreeNumbers()
print(b)  -- Should print 100

-- Test 3: Empty return (returns nil)
function returnNothing()
    return
end

local n = returnNothing()
print(n)  -- Should print nil

-- Test 4: Nested function calls with returns
function double(x)
    return x * 2
end

function getAndDouble()
    return double(5), double(10)
end

local x = getAndDouble()
print(x)  -- Should print 10 (first return value)

-- Test 5: Function that returns different counts conditionally
function conditional(flag)
    if flag then
        return 1, 2, 3
    end
    return 99
end

local r1 = conditional(true)
print(r1)  -- Should print 1

local r2 = conditional(false)
print(r2)  -- Should print 99

-- TODO: Multiple assignment (local a, b = getTwoNumbers())
local a1, b1 = getTwoNumbers()
assert(a1 == 10 and b1 == 20, "multiple assignment should capture both return values")

-- Return expansion in function calls
function identity(...) return ... end
local x1, x2 = identity(getTwoNumbers())
assert(x1 == 10 and x2 == 20, "return expansion should pass all values through")

-- Return values in last position of function call
local function wrap(a,b) return a,b end
local w1, w2 = wrap(1, getTwoNumbers())
assert(w1 == 1 and w2 == 10, "only first return should be used when not last argument")

-- (original TODO comments removed)

