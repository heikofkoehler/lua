function table_ops()
  local t = {}
  t.a = 1
  t["b"] = 2
  local x = t.a + t.b
  
  local function outer()
    local y = 10
    return function()
      y = y + 1
      return y
    end
  end
  
  local f = outer()
  local z = f()
  
  return x, z, "hello" .. " world"
end

for i = 1, 100 do
  table_ops()
end
local x, z, s = table_ops()
print(x, z, s)
