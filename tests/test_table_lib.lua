print("=== Table Library Tests ===")

local t = {10, 20, 30}

-- table.insert (append)
print("table.insert append:")
table.insert(t, 40)
print(t[4])  -- 40

-- table.insert (at position)
print("\ntable.insert at position:")
table.insert(t, 2, 15)
print(t[2])  -- 15
print(t[3])  -- 20

-- table.remove
print("\ntable.remove:")
print(table.remove(t, 1))  -- 10
print(t[1])  -- 15

-- table.concat
print("\ntable.concat:")
local t2 = {1, 2, 3}
print(table.concat(t2, ", "))  -- "1, 2, 3"
local t3 = {"a", "b", "c"}
print(table.concat(t3))  -- "abc"
