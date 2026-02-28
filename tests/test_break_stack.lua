-- test break popping
local function test_break()
    local x = 10
    while true do
        local y = 20
        if true then
            local z = 30
            break
        end
    end
    -- If break skips popping y and z, stack will be unbalanced
    -- We can check this by returning x
    return x
end

print(test_break())
