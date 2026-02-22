-- Test multiple return values more directly
function getTwoValues()
    return 10, 20
end

-- This should store first value in a
local a = getTwoValues()
print(a)

-- Test returning nothing (implicit nil)
function returnNothing()
    return
end

print(returnNothing())

-- Test returning one value
function returnOne()
    return 42
end

print(returnOne())

-- Test returning two values
function returnTwo()
    return 100, 200
end

-- Just print first value for now
local x = returnTwo()
print(x)
