local t6 = setmetatable({ val = 123 }, {
    __tostring = function(t)
        return "Object(" .. t.val .. ")"
    end
})
local res = tostring(t6)
print("Type returned:", type(res))
print("Value returned:", res)
