-- Test Compiler Optimization #2: Constant Folding, Short-Circuit, Const Propagation, and Peephole Optimization

print("Testing constant folding arithmetic...")
do
    -- Basic arithmetic
    assert(1 + 2 == 3)
    assert(10 - 4 == 6)
    assert(6 * 7 == 42)
    assert(15 / 2 == 7.5)
    assert(15 // 2 == 7)
    assert(15 % 4 == 3)
    assert(2 ^ 10 == 1024.0)

    -- Floating point
    assert(1.5 + 2.5 == 4.0)
    assert(5.5 - 2.0 == 3.5)
    assert(2.5 * 4.0 == 10.0)
    assert(7.0 / 2.0 == 3.5)
    assert(7.5 // 2.0 == 3.0)
    assert(7.5 % 2.0 == 1.5)

    -- Mixed integer and float
    assert(1 + 2.5 == 3.5)
    assert(10.0 - 4 == 6.0)
    assert(3 * 1.5 == 4.5)

    -- Negative and modulo behavior
    assert(-7 // 2 == -4)
    assert(7 // -2 == -4)
    assert(-7 % 2 == 1)
    assert(7 % -2 == -1)

    -- Bitwise operations
    assert((0xFF & 0x0F) == 0x0F)
    assert((0xF0 | 0x0F) == 0xFF)
    assert((0xFF ~ 0x0F) == 0xF0)
    assert((1 << 4) == 16)
    assert((64 >> 2) == 16)
    assert((16 >> 2) == 4)

    -- Unary operations
    assert(-(-42) == 42)
    assert(not true == false)
    assert(not false == true)
    assert(not nil == true)
    assert(not 0 == false)
    assert(~0 == -1)
    assert(#"hello" == 5)
    assert(#"" == 0)

    -- Relational operations
    assert((10 < 20) == true)
    assert((20 < 10) == false)
    assert((10 <= 10) == true)
    assert((10 > 5) == true)
    assert((5 >= 5) == true)
    assert((10 == 10) == true)
    assert((10 ~= 20) == true)
    assert(("abc" == "abc") == true)
    assert(("abc" < "def") == true)

    -- Complex folded expression
    local res = ((10 + 20) * 3 - 10) // 4 + (2 ^ 3)
    assert(res == 28.0)
end

print("Testing constant propagation with <const>...")
do
    local a <const> = 10
    local b <const> = 20
    assert(a + b == 30)
    assert(a * b == 200)

    local s <const> = "antigravity"
    assert(#s == 11)

    local x <const> = 100
    do
        local x <const> = 50
        assert(x + 10 == 60)
    end
    assert(x + 10 == 110)
end

print("Testing short-circuit optimizations...")
do
    local sideEffect = 0
    local function f(v)
        sideEffect = sideEffect + 1
        return v
    end

    -- 'and' with falsey compile-time constant: right side should not be executed
    local r1 = false and f(1)
    assert(r1 == false)
    assert(sideEffect == 0)

    local r2 = nil and f(1)
    assert(r2 == nil)
    assert(sideEffect == 0)

    -- 'and' with truthy compile-time constant: compiles directly to right side
    local r3 = true and f(42)
    assert(r3 == 42)
    assert(sideEffect == 1)

    local r4 = 123 and f(99)
    assert(r4 == 99)
    assert(sideEffect == 2)

    -- 'or' with truthy compile-time constant: right side should not be executed
    local r5 = true or f(1)
    assert(r5 == true)
    assert(sideEffect == 2)

    local r6 = "hello" or f(1)
    assert(r6 == "hello")
    assert(sideEffect == 2)

    -- 'or' with falsey compile-time constant: compiles directly to right side
    local r7 = false or f(77)
    assert(r7 == 77)
    assert(sideEffect == 3)

    local r8 = nil or f(88)
    assert(r8 == 88)
    assert(sideEffect == 4)

    -- Short-circuit in multiple return / assignment context
    local function multi()
        return 10, 20, 30
    end

    local x, y = 3 and multi()
    assert(x == 10 and y == nil)

    local u, v = false or multi()
    assert(u == 10 and v == nil)

    local function return_or()
        return nil or multi()
    end
    local a, b = return_or()
    assert(a == 10 and b == nil)
end

print("Testing peephole jump chaining...")
do
    -- Complex nested if-else creating jump chains
    local function test_jumps(x)
        local res
        if x == 1 then
            res = "one"
        elseif x == 2 then
            res = "two"
        elseif x == 3 then
            res = "three"
        else
            res = "other"
        end
        return res
    end

    assert(test_jumps(1) == "one")
    assert(test_jumps(2) == "two")
    assert(test_jumps(3) == "three")
    assert(test_jumps(4) == "other")
end

print("All compiler optimization tests passed successfully!")
