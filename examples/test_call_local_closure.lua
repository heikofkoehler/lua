function makeFunc()
    function inner()
        return 42
    end
    return inner
end

local f = makeFunc()
local result = f()
print(result)
