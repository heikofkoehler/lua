-- Simple iterator that counts from n down to 1
function countdown(n)
    if n == nil then
        return 5  -- Start from 5
    elseif n > 1 then
        return n - 1
    else
        return nil  -- Stop iteration
    end
end

for i in countdown do
    print(i)
end
