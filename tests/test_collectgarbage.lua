-- Test collectgarbage with different modes
print("Testing collectgarbage modes...")

local function alloc()
    local t = {}
    for i=1,100 do t[i] = i end
    return t
end

-- 1. Test isrunning
assert(collectgarbage("isrunning") == true)
print("OK: isrunning")

-- 2. Test incremental mode (default)
local old = collectgarbage("incremental")
assert(old == "incremental" or old == "generational")
print("OK: switched to incremental")

-- 3. Test generational mode
local old2 = collectgarbage("generational")
print("OK: switched to generational")

-- 4. Test stop/restart
collectgarbage("stop")
assert(collectgarbage("isrunning") == false)
collectgarbage("restart")
assert(collectgarbage("isrunning") == true)
print("OK: stop/restart")

-- 5. Test basic collect
collectgarbage("collect")
print("OK: collect")

print("collectgarbage tests passed!")
