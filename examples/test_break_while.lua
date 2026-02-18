-- Test break in while loop
local i = 1
while true do
    print(i)
    if i >= 3 then
        break
    end
    i = i + 1
end
print(100)
