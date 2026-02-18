-- Debug comprehensive test
local sum = 0

for i = 1, 5 do
    sum = sum + i
end
print(sum)  -- Should print 15

-- Reset - this is the problem?
print(999)
sum = 0
print(sum)  -- Should print 0

for i = 0, 10, 3 do
    print(i)
    sum = sum + i
    print(sum)
end
print(sum)  -- Should print 18
