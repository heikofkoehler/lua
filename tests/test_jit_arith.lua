function math_ops(a, b)
  local c = a + b
  local d = c - b
  local e = c * d
  local f = e / 2
  local g = -f
  local h = not false
  local i = a < b
  local j = a > b
  return c, d, e, f, g, h, i, j
end

for i = 1, 100 do
  math_ops(10.5, 5.5)
end
local c, d, e, f, g, h, i, j = math_ops(10.5, 5.5)
print(c, d, e, f, g, h, i, j)
