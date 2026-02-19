-- Test multiple return values
function getTwoValues()
    return 10, 20
end

function getThreeValues()
    return 1, 2, 3
end

-- Test returning multiple values
print(getTwoValues())
print(getThreeValues())

-- Test with variables (currently only uses first value)
local a = getTwoValues()
print(a)
