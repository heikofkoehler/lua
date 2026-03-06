#!/bin/bash
# Test CLI flags -o and --output

LUA_BIN="./build/lua"
SCRIPT="tests/temp_test.lua"
echo "print('hello cli')" > $SCRIPT

cleanup() {
    rm -f $SCRIPT out1.luac out2.luac out3.luac list.txt
}

trap cleanup EXIT

echo "Testing -o with space..."
$LUA_BIN -o out1.luac $SCRIPT
if [ -f out1.luac ]; then
    echo "✓ -o with space worked"
else
    echo "✗ -o with space failed"
    exit 1
fi

echo "Testing -o attached..."
$LUA_BIN -oout2.luac $SCRIPT
if [ -f out2.luac ]; then
    echo "✓ -o attached worked"
else
    echo "✗ -o attached failed"
    exit 1
fi

echo "Testing --output with space..."
$LUA_BIN --output out3.luac $SCRIPT
if [ -f out3.luac ]; then
    echo "✓ --output with space worked"
else
    echo "✗ --output with space failed"
    exit 1
fi

echo "Testing -L -o redirection..."
$LUA_BIN -L -o list.txt $SCRIPT
if grep -q "OP_RETURN" list.txt; then
    echo "✓ -L -o redirection worked"
else
    echo "✗ -L -o redirection failed"
    cat list.txt
    exit 1
fi

echo "All CLI flag tests passed!"
