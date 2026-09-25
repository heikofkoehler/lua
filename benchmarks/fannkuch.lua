-- The Computer Language Benchmarks Game
-- http://benchmarksgame.alioth.debian.org/

local function fannkuch(n)
    local p = {}
    local q = {}
    local s = {}
    local sign = 1
    local maxflips = 0
    local sum = 0
    local m = n - 1

    for i = 1, n do p[i] = i; q[i] = i; s[i] = i end

    repeat
        -- Copy and flip.
        local q1 = p[1] -- Cache for fast lookup
        if q1 ~= 1 then
            for i = 2, n do q[i] = p[i] end
            local flips = 1
            repeat
                local qq = q[q1]
                if qq == 1 then
                    sum = sum + sign * flips
                    if flips > maxflips then maxflips = flips end
                    break
                end
                q[q1] = q1
                if q1 >= 4 then
                    local i, j = 2, q1 - 1
                    repeat
                        q[i], q[j] = q[j], q[i]
                        i = i + 1
                        j = j - 1
                    until i >= j
                end
                q1 = qq
                flips = flips + 1
            until false
        end
        -- Permute.
        if sign == 1 then
            p[1], p[2] = p[2], p[1]
            sign = -1
        else
            p[2], p[3] = p[3], p[2]
            sign = 1
            local i = 3
            while i <= n do
                local sx = s[i]
                if sx ~= 1 then s[i] = sx - 1; break end
                if i == m then return sum, maxflips end -- Out of permutations.
                s[i] = i
                -- Rotate 1<-...<-i+1.
                local p1 = p[1]
                for j = 1, i do p[j] = p[j + 1] end
                p[i + 1] = p1
                i = i + 1
            end
        end
    until false
end

local n = 10
local start = os.clock()
local sum, flips = fannkuch(n)
local elapsed = os.clock() - start
print(string.format("fannkuch(%d): sum=%d, maxflips=%d in %.4fs", n, sum, flips, elapsed))
