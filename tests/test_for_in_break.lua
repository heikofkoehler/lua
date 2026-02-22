-- Test break in generic for loop
function counter(n)
    if n == nil then
        return 1
    elseif n < 10 then
        return n + 1
    else
        return nil
    end
end

for i in counter do
    print(i)
    if i >= 5 then
        break
    end
end
print(100)
