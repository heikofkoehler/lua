-- Test __gc metamethod

print("Testing __gc...")
local finalized = false
do
    local t = setmetatable({}, {
        __gc = function()
            finalized = true
            print("Finalizer called!")
        end
    })
    t = nil
end

collectgarbage()
collectgarbage() -- Second cycle to ensure it's processed

assert(finalized == true)
print("OK")

print("All __gc tests passed!")
