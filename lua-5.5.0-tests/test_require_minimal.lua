print("Testing require...")
local tracegc = require("tracegc")
print("tracegc loaded: " .. tostring(tracegc))
for k, v in pairs(tracegc) do
    print(k, v)
end
