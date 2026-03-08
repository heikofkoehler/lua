function call_test()
  local function add(a, b) return a + b end
  local sum = 0
  for i = 1, 100 do
    sum = add(sum, i)
  end
  return sum
end

for i = 1, 100 do
  if i % 10 == 0 then print("Iteration " .. i) end
  call_test()
end
print(call_test())
