function simple_test()
  local x = 1
  return x
end

for i = 1, 100 do
  simple_test()
end
print(simple_test())
