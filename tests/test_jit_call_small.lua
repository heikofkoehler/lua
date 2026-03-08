function call_test() local function add(a, b) return a + b end; local sum = 0.0; for i = 1.0, 10.0 do sum = add(sum, i) end; return sum end; for i = 1, 10 do call_test() end; print(call_test())
