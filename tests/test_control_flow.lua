-- Comprehensive control flow test

print(1)

-- Test if statement
if 5 > 3 then
    print(2)
end

-- Test if-else
if false then
    print(999)
else
    print(3)
end

-- Test if-elseif-else
if 1 > 2 then
    print(999)
elseif 2 > 3 then
    print(999)
elseif 3 < 5 then
    print(4)
else
    print(999)
end

-- Test nested if
if true then
    if 10 == 10 then
        print(5)
    else
        print(999)
    end
end

-- Test repeat-until
repeat
    print(6)
until true

-- Test repeat with multiple statements
repeat
    print(7)
    print(8)
until 1 == 1

-- Test complex expressions in conditions
if (2 + 3) * 2 == 10 then
    print(9)
end

if (5 ~= 6) and (3 < 4) then
    print(10)
end

print(11)
