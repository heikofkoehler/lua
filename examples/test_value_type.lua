-- Test what we get from string["len"]
print("Getting string table")
local s = string
print("String table retrieved")

print("Getting len from table")
local len = s["len"]
print("Len retrieved")

-- Try calling it
print("About to call len")
-- This should work or error
len("test")
print("Done")
