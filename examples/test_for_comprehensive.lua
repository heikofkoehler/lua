-- Comprehensive for loop test
local sum = 0

-- Basic counting
for i = 1, 5 do
    sum = sum + i
end
print(sum)  -- Should print 15

-- Reset
sum = 0

-- With step
for i = 0, 10, 3 do
    sum = sum + i
end
print(sum)  -- Should print 18 (0+3+6+9)

-- Countdown
for i = 3, 1, -1 do
    print(i)
end

-- Nested loops for multiplication table
for i = 1, 3 do
    for j = 1, 3 do
        print(i * j)
    end
end
