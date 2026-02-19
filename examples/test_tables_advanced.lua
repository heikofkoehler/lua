-- Test table as function return value
function makeTable()
    local t = {}
    t["x"] = 42
    return t
end

local result = makeTable()
print(result["x"])

-- Test table with numeric and string keys mixed
local mixed = {}
mixed[1] = "first"
mixed[2] = "second"
mixed["name"] = "test"
mixed["count"] = 99

print(mixed[1])
print(mixed[2])
print(mixed["name"])
print(mixed["count"])

-- Test table operations in expressions
local data = {}
data["a"] = 10
data["b"] = 20
print(data["a"] + data["b"])

-- Test table in loop
local scores = {}
for i = 1, 5 do
    scores[i] = i * 10
end

for i = 1, 5 do
    print(scores[i])
end
