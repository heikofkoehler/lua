-- Verification test for all MVP features

-- Arithmetic expressions
print(2 + 3)                    -- Expected: 5
print((10 - 2) * 3)            -- Expected: 24

-- Operator precedence
print(2 + 3 * 4)               -- Expected: 14
print((2 + 3) * 4)             -- Expected: 20

-- Power (right-associative)
print(2 ^ 3 ^ 2)               -- Expected: 512

-- Comparisons
print(10 > 5)                  -- Expected: true
print(3 < 2)                   -- Expected: false

-- Booleans and nil
print(true)                    -- Expected: true
print(false)                   -- Expected: false
print(nil)                     -- Expected: nil

-- Complex expression
print((5 + 3) * 2 - 10 / 2)   -- Expected: 11
