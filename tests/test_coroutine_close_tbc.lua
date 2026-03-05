-- Test coroutine.close with <close> variables
print("Testing coroutine.close with <close> variables...")

local closed = false
local function create_closable()
    return setmetatable({}, {
        __close = function()
            print("  - Coroutine variable successfully closed")
            closed = true
        end
    })
end

local co = coroutine.create(function()
    local x <close> = create_closable()
    coroutine.yield()
end)

coroutine.resume(co)
assert(closed == false)
print("  - Coroutine suspended, variable not yet closed")

local ok, err = coroutine.close(co)
assert(ok == true)
assert(closed == true)
print("OK: coroutine.close triggered __close")

print("Coroutine TBC cleanup tests passed!")
