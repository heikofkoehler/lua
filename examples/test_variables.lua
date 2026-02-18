-- Test global variables
x = 10
print(x)

y = 20
print(y)

-- Assignment and arithmetic
z = x + y
print(z)

-- Reassignment
x = 5
print(x)

-- Local variables
local a = 100
print(a)

local b = a + 50
print(b)

-- Locals shadow globals
x = 999
local x = 42
print(x)

-- Global still accessible
y = x + 10
print(y)
