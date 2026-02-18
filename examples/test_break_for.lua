-- Test break in for loop
for i = 1, 10 do
    print(i)
    if i >= 5 then
        break
    end
end
print(100)
