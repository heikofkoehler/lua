function makeCounter()
    local count = 0
    function increment()
        count = count + 1
        return count
    end
    return increment
end

local counter = makeCounter()
print(counter())
print(counter())
print(counter())
