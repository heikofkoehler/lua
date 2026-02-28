-- Test goto and labels
local function test_goto()
    local sum = 0
    local i = 1
    ::loop::
    if i > 5 then
        goto done
    end
    sum = sum + i
    i = i + 1
    goto loop
    
    ::done::
    return sum
end

local function test_goto_forward()
    local x = 1
    goto skip
    x = 2
    ::skip::
    return x
end

local function test_goto_forward_block()
    local a = 1
    do
        local b = 2
        goto skip
        a = 3
    end
    ::skip::
    return a
end

print("sum:", test_goto()) -- 1+2+3+4+5 = 15
print("x:", test_goto_forward()) -- 1
print("a:", test_goto_forward_block()) -- 1
