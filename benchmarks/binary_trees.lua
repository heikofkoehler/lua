-- The Computer Language Benchmarks Game
-- http://benchmarksgame.alioth.debian.org/

local function bottomuptree(depth)
    if depth > 0 then
        depth = depth - 1
        return {bottomuptree(depth), bottomuptree(depth)}
    else
        return {}
    end
end

local function itemcheck(tree)
    if tree[1] then
        return 1 + itemcheck(tree[1]) + itemcheck(tree[2])
    else
        return 1
    end
end

local N = 12
local mindepth = 4
local maxdepth = mindepth + 2
if maxdepth < N then maxdepth = N end

local start = os.clock()

do
    local stretchdepth = maxdepth + 1
    local stretchtree = bottomuptree(stretchdepth)
    -- print(string.format("stretch tree of depth %d\t check: %d", stretchdepth, itemcheck(stretchtree)))
end

local longlivedtree = bottomuptree(maxdepth)

for depth = mindepth, maxdepth, 2 do
    local iterations = 2 ^ (maxdepth - depth + mindepth)
    local check = 0
    for i = 1, iterations do
        check = check + itemcheck(bottomuptree(depth))
    end
end

local check = itemcheck(longlivedtree)
local elapsed = os.clock() - start
print(string.format("binary_trees(N=%d) in %.4fs", N, elapsed))
