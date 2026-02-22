-- Iterator that generates range 1 to 5
function range(n)
    if n == nil then
        return 1
    elseif n < 5 then
        return n + 1
    else
        return nil
    end
end

for i in range do
    print(i)
end
print(100)
