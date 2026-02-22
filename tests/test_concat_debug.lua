-- Test table.concat with strings
local concat = table["concat"]
local t = {"a", "b", "c"}
print("Before concat")
print(t[1])
print(t[2])
print(t[3])
print("Calling concat")
local result = concat(t)
print("Result:")
print(result)
