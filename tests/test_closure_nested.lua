function outer()
    local x = 1
    function middle()
        local y = 2
        function inner()
            return x + y
        end
        return inner
    end
    return middle
end

local m = outer()
local i = m()
print(i())
