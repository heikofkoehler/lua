function outer()
    function inner()
        print(42)
    end
    print(99)
end

outer()
