#!/bin/bash
# Test CLI flags -o and --output

LUA_BIN="./build/lua"
SCRIPT="tests/temp_test.lua"
echo "print('hello cli')" > $SCRIPT

cleanup() {
    rm -f $SCRIPT out1.luac out2.luac out3.luac list.txt combo1.luac combo2.luac out.luac post_script.luac binary.luac binary_list.txt
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

echo "Testing -L with binary file..."
$LUA_BIN -o binary.luac $SCRIPT
$LUA_BIN -L binary.luac > binary_list.txt
if grep -q "OP_RETURN" binary_list.txt; then
    echo "✓ -L with binary file worked"
else
    echo "✗ -L with binary file failed"
    cat binary_list.txt
    exit 1
fi

echo "Testing -c -o combination..."
$LUA_BIN -c -o combo1.luac $SCRIPT
if [ -f combo1.luac ]; then
    echo "✓ -c -o worked"
else
    echo "✗ -c -o failed"
    exit 1
fi

echo "Testing -o -c combination (order independence)..."
$LUA_BIN -o combo2.luac -c $SCRIPT
if [ -f combo2.luac ]; then
    echo "✓ -o -c worked"
else
    echo "✗ -o -c failed"
    exit 1
fi

echo "Testing -c default output (out.luac)..."
$LUA_BIN -c $SCRIPT
if [ -f out.luac ]; then
    echo "✓ -c default output worked"
    rm out.luac
else
    echo "✗ -c default output failed"
    exit 1
fi

echo "Testing flag after script (user request)..."
$LUA_BIN -c $SCRIPT --output post_script.luac
if [ -f post_script.luac ]; then
    echo "✓ flag after script worked"
    rm post_script.luac
else
    echo "✗ flag after script failed"
    exit 1
fi

echo "All CLI flag tests passed!"
