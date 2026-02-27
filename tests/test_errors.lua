-- Test error handling and protected calls

print("Testing pcall basic...")
local status, err = pcall(function()
    return 10 + 20
end)
assert(status == true)
assert(err == 30)

print("Testing pcall error...")
local status2, err2 = pcall(function()
    error("my error")
end)
print("DEBUG: pcall returned", status2, err2)
assert(status2 == false)
assert(string.find(err2, "my error"))

print("Error tests passed!")
