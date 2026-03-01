print("=== Test Incremental GC ===")

local function report(msg)
    print(msg .. ": " .. collectgarbage("count") .. " KB")
end

report("Start memory")

-- Create significant garbage
local t = {}
print("Allocating 50,000 objects...")
for i = 1, 50000 do
    t[i] = { id = i, data = "string_" .. i }
end

report("Memory after allocations")

-- Break references
print("Breaking references...")
for i = 1, 50000 do
    t[i] = nil
end

-- Trigger incremental GC
print("Triggering incremental steps through more allocations...")
for i = 1, 20000 do
    local _ = { dummy = i }
end

report("Memory after incremental steps")

-- Force full collection
print("Forcing full collection...")
collectgarbage("collect")
collectgarbage("collect") -- Run twice to collect objects that became unreachable during MARK phase
report("Final memory")

-- Verify write barriers
print("\n--- Verifying Write Barriers ---")
collectgarbage("collect") -- Start clean
local black = { name = "black" }
-- Do enough work to potentially blacken 'black'
for i=1,10000 do local _ = { work = i } end

local white = { name = "white" }
black.ref = white -- Barrier!

collectgarbage("collect")
if black.ref and black.ref.name == "white" then
    print("SUCCESS: Write barrier kept white object alive.")
else
    print("FAILURE: Write barrier failed or object lost.")
end
