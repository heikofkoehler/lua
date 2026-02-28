-- Test Tail Call Optimization

local function count_down(n)
    if n == 0 then
        return "done"
    end
    return count_down(n - 1)
end

print("Starting TCO test...")
-- This would crash with stack overflow if TCO is not working, since FRAMES_MAX is 64
local result = count_down(1000)
assert(result == "done")
print("TCO test passed!")
