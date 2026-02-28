-- Test debug library
local function test_locals(a, b)
    local x = 30
    local y = 40
    
    print("--- Testing getlocal ---")
    local name, val = debug.getlocal(1, 1)
    print("Local 1:", name, val) -- a 10
    name, val = debug.getlocal(1, 2)
    print("Local 2:", name, val) -- b 20
    name, val = debug.getlocal(1, 3)
    print("Local 3:", name, val) -- x 30
    name, val = debug.getlocal(1, 4)
    print("Local 4:", name, val) -- y 40

    print("--- Testing setlocal ---")
    debug.setlocal(1, 3, 300)
    print("x after setlocal:", x) -- 300

    print("--- Testing getinfo ---")
    local info = debug.getinfo(test_locals)
    print("Info name:", info.name)
    print("Info what:", info.what)
    print("Info nups:", info.nups)

    print("--- Testing traceback ---")
    print(debug.traceback())

    print("--- Testing sethook (count) ---")
    local count = 0
    debug.sethook(function(event)
        count = count + 1
    end, "", 1) -- hook every instruction
    
    local sum = 0
    for i=1,10 do sum = sum + i end
    
    debug.sethook() -- disable
    print("Instruction count (approx):", count)
    if count > 10 then
        print("SUCCESS: Count hook triggered")
    else
        print("FAILURE: Count hook NOT triggered")
    end

    print("--- Testing sethook (line) ---")
    local lines = {}
    debug.sethook(function(event, line)
        lines[line] = true
    end, "l")
    
    print("Step 1")
    print("Step 2")
    
    debug.sethook()
    print("Lines visited:")
    for l, _ in pairs(lines) do print(l) end
end

test_locals(10, 20)
