local N = 100000
local t = {}
local start = os.clock()

-- Fill table with pseudo-random numbers
local a = 1103515245
local c = 12345
local m = 2147483648
local seed = 42

for i = 1, N do
    seed = (a * seed + c) % m
    t[i] = seed
end
local fill_time = os.clock() - start

-- Sort table
local sort_start = os.clock()
table.sort(t)
local sort_time = os.clock() - sort_start

-- Validate sorted
for i = 1, N - 1 do
    assert(t[i] <= t[i + 1])
end

local total_time = os.clock() - start
print(string.format("table_sort: N=%d, fill=%.4fs, sort=%.4fs, total=%.4fs", N, fill_time, sort_time, total_time))
