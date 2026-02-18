-- Test break in nested loops
for i = 1, 3 do
    print(i)
    for j = 1, 5 do
        print(j)
        if j >= 2 then
            break
        end
    end
    print(10)
end
print(100)
