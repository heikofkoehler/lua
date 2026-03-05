-- Complete IO Library Tests
print("Testing IO Library...")

local test_file = "temp_io_test.txt"

-- Basic file ops
local f = io.open(test_file, "w")
assert(f ~= nil)
f:write("line 1\n", "line 2\n")
f:flush()
f:close()

local f2 = io.open(test_file, "r")
assert(f2:read("*l") == "line 1")
assert(f2:read("*l") == "line 2")
assert(f2:read("*a") == "")
f2:close()

-- seek
local f3 = io.open(test_file, "r")
f3:seek("set", 5)
assert(f3:read(1) == "1")
f3:close()

-- lines
local lines = {}
local iter = io.lines(test_file)
assert(type(iter) == "function")
for line in iter do
    table.insert(lines, line)
end
assert(#lines == 2)
assert(lines[1] == "line 1")

-- input/output
io.output(test_file)
io.write("new content")
io.output():close()

io.input(test_file)
assert(io.read("*a") == "new content")
io.input():close()

-- cleanup
os.remove(test_file)

print("IO Library Tests Passed!")
