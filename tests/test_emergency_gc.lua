-- Test Emergency GC
print("=== Emergency GC ===")

-- Set a reasonable memory limit (1MB)
-- The standard library already takes some space
local ok, err = pcall(function()
    collectgarbage("setmemorylimit", 1024 * 1024)
end)

if not ok then
    print("Could not set 1MB limit:", err)
    -- Try larger
    collectgarbage("setmemorylimit", 5 * 1024 * 1024)
end

local t = {}
local count = 0

-- This should trigger GC
local success, err = pcall(function()
    print("Starting first test loop...")
    for i = 1, 10000 do
        t[i] = "string_" .. i
        count = i
        if i % 1000 == 0 then
            print("Iteration", i, "Allocated (KB):", collectgarbage("count"))
        end
    end
end)

if not success then
    print("Caught memory error:", err)
    print("Last count:", count)
else
    print("Finished successfully")
end

-- Set a VERY small limit that should definitely fail eventually
print("\n--- Testing very small limit ---")
local setmem_opt = "setmemorylimit"
collectgarbage(setmem_opt, collectgarbage("count") * 1024 + 1000) -- Current + 1KB
local success2, err2 = pcall(function()
    local x = {}
    for i=1,1000 do 
        x[i] = {a=1, b=2, c=3, d=4, e=5} 
    end
end)

collectgarbage(setmem_opt, 100 * 1024 * 1024) -- Reset immediately

print("Very small limit success:", success2)
if not success2 then
    print("Error message:", err2)
end
