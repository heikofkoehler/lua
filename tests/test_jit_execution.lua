function test_jit()
    local x = 0.5
    for i=1.1, 10.1, 1.1 do
        x = x + 1.1
    end
    return x
end

-- Call many times to trigger JIT
local result
for i=1,60 do
    result = test_jit()
end

print("JIT Result: " .. tostring(result))
local expected = 0.5
for i=1.1, 10.1, 1.1 do expected = expected + 1.1 end
if math.abs(result - expected) < 0.0001 then
    print("SUCCESS")
else
    print("FAILURE, expected " .. tostring(expected))
end
