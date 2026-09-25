local N = 200
local start = os.clock()
local count = 0

for y = 0, N - 1 do
    local Civ = (2 * y / N) - 1.0
    for x = 0, N - 1 do
        local Crv = (2 * x / N) - 1.5
        local Zrv = Crv
        local Ziv = Civ
        local trv = Crv * Crv
        local tiv = Civ * Civ
        local i = 0
        while i < 50 and (trv + tiv <= 4.0) do
            Ziv = 2.0 * Zrv * Ziv + Civ
            Zrv = trv - tiv + Crv
            trv = Zrv * Zrv
            tiv = Ziv * Ziv
            i = i + 1
        end
        if trv + tiv <= 4.0 then
            count = count + 1
        end
    end
end

local elapsed = os.clock() - start
print(string.format("mandelbrot(%d): count=%d in %.4fs", N, count, elapsed))
