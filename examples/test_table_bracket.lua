print("=== Table Library Tests ===")

local t = {10, 20, 30}

-- table.insert (append)
print("table.insert append:")
local insert = table["insert"]
insert(t, 40)
print(t[4])  -- 40

-- table.insert (at position)
print("table.insert at position:")
insert(t, 2, 15)
print(t[2])  -- 15
print(t[3])  -- 20

-- table.remove
print("table.remove:")
local remove = table["remove"]
print(remove(t, 1))  -- 10
print(t[1])  -- 15

-- table.concat
print("table.concat:")
local concat = table["concat"]
local t2 = {1, 2, 3}
print(concat(t2, ", "))  -- "1, 2, 3"
local t3 = {"a", "b", "c"}
print(concat(t3))  -- "abc"
