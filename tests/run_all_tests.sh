#!/bin/bash
# Test runner for Lua VM test suite
# Usage: ./run_all_tests.sh [lua_binary_path]
# Default lua_binary_path: ../build/lua

set -e

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Get lua binary path from argument or use default
LUA_BIN="${1:-../build/lua}"

# Verify lua binary exists
if [ ! -f "$LUA_BIN" ]; then
    echo -e "${RED}Error: Lua binary not found at '$LUA_BIN'${NC}"
    echo "Usage: $0 [lua_binary_path]"
    exit 1
fi

# Get script directory (the tests folder)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Initialize counters
PASS=0
FAIL=0
TESTS=()

echo "=== Lua VM Test Suite ==="
echo "Binary: $LUA_BIN"
echo "Tests folder: $SCRIPT_DIR"
echo ""

# Run each test file
for test_file in $(ls *.lua | sort); do
    # Skip the test runner script itself if it exists
    if [ "$test_file" = "run_all_tests.sh" ]; then
        continue
    fi
    
    # Run test and capture exit code
    if "$LUA_BIN" "$test_file" >/dev/null 2>&1; then
        echo -e "${GREEN}✓${NC} $test_file"
        ((PASS++))
    else
        echo -e "${RED}✗${NC} $test_file"
        ((FAIL++))
        TESTS+=("$test_file")
    fi
done

# Print summary
echo ""
echo "=== Test Summary ==="
TOTAL=$((PASS + FAIL))
echo "Total:  $TOTAL"
echo -e "Passed: ${GREEN}$PASS${NC}"
echo -e "Failed: ${RED}$FAIL${NC}"

# Print failed tests if any
if [ $FAIL -gt 0 ]; then
    echo ""
    echo -e "${YELLOW}Failed tests:${NC}"
    for test in "${TESTS[@]}"; do
        echo "  - $test"
    done
    exit 1
fi

exit 0
