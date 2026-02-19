function makeFunc()
    function inner()
        return 42
    end
    return inner
end

print(makeFunc())
