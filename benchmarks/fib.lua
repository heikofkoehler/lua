local function fib(n)
    if n < 2 then return n end
    return fib(n - 1) + fib(n - 2)
end

local n = 34
local start = os.clock()
local res = fib(n)
local elapsed = os.clock() - start
print(string.format("fib(%d) = %d in %.4f s", n, res, elapsed))
