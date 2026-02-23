-- Test iterators (pairs, ipairs, next)

function assert_eq(a, b, msg)
    if a ~= b then
        print("FAIL: " .. msg .. " (expected " .. tostring(b) .. ", got " .. tostring(a) .. ")")
    end
end

print("Testing iterators...")

-- Test ipairs
print("Testing ipairs...")
local t = {10, 20, 30}
local sum = 0
for i, v in ipairs(t) do
    sum = sum + v
end
assert_eq(sum, 60, "ipairs sum")
