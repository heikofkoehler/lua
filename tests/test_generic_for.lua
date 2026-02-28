-- Test generic for loop
local function mypairs(t)
    local function iter(t, k)
        local nk, nv = next(t, k)
        if nk then return nk, nv end
    end
    return iter, t, nil
end

local t = {a = 1, b = 2, c = 3}
local sum = 0
for k, v in mypairs(t) do
    if type(v) == "number" then
        sum = sum + v
    end
end
print(sum) -- should be 6
