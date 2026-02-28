-- Test invalid goto
local function test_invalid_goto()
    goto skip
    local a = 1
    ::skip::
    return a
end
