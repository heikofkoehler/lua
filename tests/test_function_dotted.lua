local t = {}
function t.f(x)
    return x * 2
end
print(t.f(21))

t.sub = {}
function t.sub.g(y)
    return y + 10
end
print(t.sub.g(32))

local mt = {}
function mt:m(z)
    return z + 100
end
print(mt:m(50))
