-- Test local function and recursion

local function fact(n)
    if n <= 1 then return 1 end
    return n * fact(n - 1)
end

print("Factorial of 5:", fact(5))
assert(fact(5) == 120)

local function fib(n)
    if n < 2 then return n end
    return fib(n - 1) + fib(n - 2)
end

print("Fibonacci of 7:", fib(7))
assert(fib(7) == 13)

print("Local function tests passed!")
