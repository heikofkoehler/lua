function outer()
    function inner()
        print(42)
    end
    inner()
end

outer()
