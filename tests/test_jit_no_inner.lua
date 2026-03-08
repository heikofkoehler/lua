function call_test() local function add(a, b) return a + b end; return add(1, 2) end; for i=1,10 do print(i); call_test() end; print('DONE')
