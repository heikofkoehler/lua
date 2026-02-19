-- Test table creation
local t = {}
print("Created table")

-- Test table assignment
t[1] = 10
t[2] = 20
t["key"] = 30

-- Test table access
print(t[1])
print(t[2])
print(t["key"])

-- Test nil access (should print nil)
print(t[999])

-- Test removing key by setting to nil
t[1] = nil
print(t[1])

-- Test nested expressions
local x = 5
t[x] = 100
print(t[5])
print(t[x])
