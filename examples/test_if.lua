-- Test if-then-else statements

-- Simple if
if true then
    print(1)
end

-- If with else
if false then
    print(999)
else
    print(2)
end

-- If-elseif-else
if 5 < 3 then
    print(999)
elseif 5 > 3 then
    print(3)
else
    print(999)
end

-- Nested if
if true then
    if true then
        print(4)
    end
end

-- Complex condition
if (2 + 2) == 4 then
    print(5)
end
