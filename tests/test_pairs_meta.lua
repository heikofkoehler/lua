-- Test __pairs metamethod

print("Testing __pairs...")
local t = setmetatable({}, {
    __pairs = function(obj)
        local function iter(o, k)
            if k == nil then return "key", "value" end
            return nil
        end
        return iter, obj, nil
    end
})

local count = 0
for k, v in pairs(t) do
    assert(k == "key")
    assert(v == "value")
    count = count + 1
end
assert(count == 1)
print("OK")

print("All __pairs tests passed!")
