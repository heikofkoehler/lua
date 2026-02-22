-- Simple table constructor tests

-- Empty table still works
local t1 = {}
t1[1] = 99
print(t1[1])  -- 99

-- Array with values
local t2 = {1, 2, 3, 4, 5}
print(t2[3])  -- 3

-- Record with values
local t3 = {x = 10, y = 20, z = 30}
print(t3["y"])  -- 20

-- Expression values
local a = 5
local t4 = {a * 2, a + 10}
print(t4[1])  -- 10
print(t4[2])  -- 15
