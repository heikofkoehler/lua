-- Test error escalation in __close
print("Testing __close error escalation...")

local function create_exploding_closable(msg)
    return setmetatable({}, {
        __close = function()
            print("  - Throwing from __close: " .. msg)
            error(msg)
        end
    })
end

local function test_double_error()
    local x <close> = create_exploding_closable("error in close")
    error("main error")
end

local ok, err = pcall(test_double_error)
assert(ok == false)
print("Resulting error: " .. tostring(err))

-- In Lua 5.4, if an error happens during TBC cleanup of another error,
-- the new error replaces the old one or is wrapped.
-- Our current implementation will likely just propagate the last error.
assert(string.find(err, "error in close"))

print("Error escalation tests passed!")
