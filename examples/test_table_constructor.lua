-- Test table constructors with initial values

-- Array-style (implicit numeric keys)
local arr = {10, 20, 30}
print(arr[1])  -- 10
print(arr[2])  -- 20
print(arr[3])  -- 30

-- Record-style (string keys)
local person = {name = "Alice", age = 30}
print(person["name"])  -- Alice
print(person["age"])   -- 30

-- Mixed style
local mixed = {100, 200, x = 10, y = 20}
print(mixed[1])    -- 100
print(mixed[2])    -- 200
print(mixed["x"])  -- 10
print(mixed["y"])  -- 20

-- Computed keys
local key = "color"
local obj = {[key] = "blue", [1+1] = "two"}
print(obj["color"])  -- blue
print(obj[2])        -- two

-- Nested tables
local nested = {
    inner = {a = 1, b = 2},
    values = {10, 20, 30}
}
print(nested["inner"]["a"])     -- 1
print(nested["values"][1])      -- 10
