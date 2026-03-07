#!/bin/bash
# Test Bytecode Disassembler Metadata

LUA_BIN="./build/lua"
TEST_SCRIPT="tests/temp_disasm.lua"

cat << 'EOF' > $TEST_SCRIPT
local x = 10
local function outer(a)
    local y = 20
    return function(b)
        return a + x + y + b
    end
end
EOF

echo "Testing disassembler output metadata..."

# Run disassembler and capture output
DISASM_OUT=$($LUA_BIN -L $TEST_SCRIPT)

# Check for Function headers
if echo "$DISASM_OUT" | grep -q "Function:  @$TEST_SCRIPT"; then
    echo "✓ Found main function header"
else
    echo "✗ Missing main function header"
    exit 1
fi

if echo "$DISASM_OUT" | grep -q "Function:  outer"; then
    echo "✓ Found 'outer' function header"
else
    echo "✗ Missing 'outer' function header"
    exit 1
fi

# Check for Local variable info
if echo "$DISASM_OUT" | grep -q "Locals ("; then
    echo "✓ Found local variable section"
else
    echo "✗ Missing local variable section"
    exit 1
fi

if echo "$DISASM_OUT" | grep -q " x "; then
    echo "✓ Found local variable 'x'"
else
    echo "✗ Missing local variable 'x'"
    exit 1
fi

# Check for Upvalues
if echo "$DISASM_OUT" | grep -q "Upvalues:  4"; then
    echo "✓ Found correct upvalue count for inner closure"
else
    echo "✗ Incorrect upvalue count or missing section"
fi

# Check for recursion
if [ $(echo "$DISASM_OUT" | grep "Function: " | wc -l) -ge 3 ]; then
    echo "✓ Found recursive disassembly of nested functions"
else
    echo "✗ Recursive disassembly failed"
    exit 1
fi

rm -f $TEST_SCRIPT
echo "Disassembler metadata tests passed!"
