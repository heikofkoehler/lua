function outer()
    local x = 10
    function middle()
        function inner()
            print(x)
        end
        inner()
    end
    middle()
end

outer()
