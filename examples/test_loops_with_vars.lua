-- Test control flow with variables

-- Counter with if
x = 0
x = x + 1
print(x)
x = x + 1
print(x)
x = x + 1
print(x)

-- Conditional with variables
if x > 2 then
    print(999)
end

if x == 3 then
    print(100)
end

-- Variables in repeat
local counter = 0
repeat
    counter = counter + 1
    print(counter)
until counter == 3
