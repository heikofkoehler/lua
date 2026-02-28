-- Test weak tables
print("=== Weak Tables ===")

local wk = setmetatable({}, {__mode = "k"})
local wv = setmetatable({}, {__mode = "v"})
local wkv = setmetatable({}, {__mode = "kv"})

local k1 = {}
local v1 = {}
local k2 = {}
local v2 = {}
local k3 = {}
local v3 = {}

wk[k1] = v1
wv[k2] = v2
wkv[k3] = v3

-- Store values so we can verify they exist before GC
local function check()
    local has_wk = false
    local has_wv = false
    local has_wkv = false
    
    for k,v in pairs(wk) do has_wk = true end
    for k,v in pairs(wv) do has_wv = true end
    for k,v in pairs(wkv) do has_wkv = true end
    
    return has_wk, has_wv, has_wkv
end

local c1, c2, c3 = check()
print("Before GC:", c1, c2, c3) -- should be true true true

-- Drop references
k1 = nil
v2 = nil
k3 = nil

collectgarbage()

local a1, a2, a3 = check()
print("After GC:", a1, a2, a3) -- should be false false false

-- Test Ephemeron behavior:
local ephem = setmetatable({}, {__mode = "k"})
local key = {}
local val = {}
-- Add a cycle
val.ref = key
ephem[key] = val

-- We drop 'key', so the only thing keeping 'key' alive is 'val'
-- But 'val' is only kept alive by the ephemeron table if 'key' is alive!
-- Thus, they should both be collected.
key = nil
val = nil
collectgarbage()

local has_ephem = false
for k,v in pairs(ephem) do has_ephem = true end
print("Ephemeron collected:", not has_ephem) -- should be true
