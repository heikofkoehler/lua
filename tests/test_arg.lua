print("arg type:", type(arg))
print("arg:", arg)
if type(arg) == "table" then
    for k, v in pairs(arg) do
        print(k, v)
    end
end
