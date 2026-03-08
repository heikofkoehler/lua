function closure_test()
  local function add(a, b) return a + b end
  return add
end

for i = 1, 100 do
  closure_test()
end
print(closure_test())
