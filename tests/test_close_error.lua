-- Test <close> attribute with errors
print("Testing <close> attribute error handling...")

local closed = false
local function create_closable()
    return setmetatable({}, {
        __close = function()
            print("  - Successfully closed after error")
            closed = true
        end
    })
end

local function test_err()
    local x <close> = create_closable()
    error("intentional error")
end

local ok, err = pcall(test_err)
assert(ok == false)
assert(string.find(err, "intentional error"))
assert(closed == true)

print("<close> error handling test passed!")
