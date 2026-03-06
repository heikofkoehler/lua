#!/bin/bash
# Test REPL with simulated input

LUA_BIN="./build/lua"

echo "Testing multi-line input..."
echo -e "if true then
print('hello from multi-line')
end
exit" | $LUA_BIN -i | grep "hello from multi-line"

echo "Testing expression evaluation..."
echo -e "2 + 2
exit" | $LUA_BIN -i | grep "4"

echo "Testing meta-command '='..."
echo -e "=10 * 10
exit" | $LUA_BIN -i | grep "100"

echo "Testing 'globals' command..."
echo -e "my_special_global = 12345
globals
exit" | $LUA_BIN -i | grep "my_special_global" | grep "12345"

echo "All REPL logic tests passed (if greps succeeded)"
