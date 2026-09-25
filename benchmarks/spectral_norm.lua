-- The Computer Language Benchmarks Game
-- http://benchmarksgame.alioth.debian.org/

local function A(i, u)
    local ij = i + u - 1
    return 1.0 / (ij * (ij - 1) * 0.5 + i)
end

local function Av(x, y, N)
    for i = 1, N do
        local a = 0
        for j = 1, N do a = a + x[j] * A(i, j) end
        y[i] = a
    end
end

local function Atv(x, y, N)
    for i = 1, N do
        local a = 0
        for j = 1, N do a = a + x[j] * A(j, i) end
        y[i] = a
    end
end

local function AtAv(x, y, t, N)
    Av(x, t, N)
    Atv(t, y, N)
end

local N = 300
local u = {}
local v = {}
local t = {}
for i = 1, N do u[i] = 1.0; v[i] = 0.0; t[i] = 0.0 end

local start = os.clock()
for i = 1, 10 do
    AtAv(u, v, t, N)
    AtAv(v, u, t, N)
end

local vBv = 0
local vv = 0
for i = 1, N do
    local ui = u[i]
    local vi = v[i]
    vBv = vBv + ui * vi
    vv = vv + vi * vi
end

local res = math.sqrt(vBv / vv)
local elapsed = os.clock() - start
print(string.format("spectral_norm(%d) = %0.9f in %.4fs", N, res, elapsed))
