-- Test nested generic for loops
function small(n)
    if n == nil then
        return 1
    elseif n < 3 then
        return n + 1
    else
        return nil
    end
end

for i in small do
    print(i)
    for j in small do
        print(j * 10)
    end
end
print(100)
