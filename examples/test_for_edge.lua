-- Edge cases for for loops
print(100)

-- Loop that doesn't execute (start > end with positive step)
for i = 5, 1 do
    print(999)
end

print(200)

-- Loop that doesn't execute (start < end with negative step)
for i = 1, 5, -1 do
    print(888)
end

print(300)

-- Loop with zero iterations
for i = 1, 0 do
    print(777)
end

print(400)
