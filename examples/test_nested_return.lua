function outer()
    function inner()
        return 42
    end
    return inner()
end

print(outer())
