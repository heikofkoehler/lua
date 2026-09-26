#!/bin/bash
set -e

DIR="$(cd "$(dirname "$0")/.." && pwd)"
LUA_BIN="${1:-${DIR}/build/lua}"
LUAC_BIN="${2:-${DIR}/build/luac}"

# Convert to absolute paths
LUA_BIN="$(cd "$(dirname "$LUA_BIN")" && pwd)/$(basename "$LUA_BIN")"
LUAC_BIN="$(cd "$(dirname "$LUAC_BIN")" && pwd)/$(basename "$LUAC_BIN")"

if [ ! -x "${LUAC_BIN}" ]; then
    echo "Error: luac binary '${LUAC_BIN}' not found or not executable." >&2
    exit 1
fi

echo "=== Running luac Test Suite ==="

TMPDIR=$(mktemp -d)
trap 'rm -rf "${TMPDIR}"' EXIT

# Test 1: Version flag
echo -n "Test 1: luac -v... "
VERSION_OUT=$("${LUAC_BIN}" -v)
if [[ "${VERSION_OUT}" == *"Lua 5.5"* ]]; then
    echo "PASS"
else
    echo "FAIL (unexpected output: ${VERSION_OUT})"
    exit 1
fi

# Test 2: Disassembly listing
echo -n "Test 2: luac -l... "
cat << 'EOF' > "${TMPDIR}/test1.lua"
local x = 10
local y = 20
print(x + y)
EOF
LIST_OUT=$("${LUAC_BIN}" -l "${TMPDIR}/test1.lua")
if [[ "${LIST_OUT}" == *"OP_ADD"* ]]; then
    echo "PASS"
else
    echo "FAIL"
    exit 1
fi

# Test 3: Output compilation and execution
echo -n "Test 3: luac compilation & execution... "
"${LUAC_BIN}" -o "${TMPDIR}/test1.luac" "${TMPDIR}/test1.lua"
RUN_OUT=$("${LUA_BIN}" "${TMPDIR}/test1.luac")
if [ "${RUN_OUT}" = "30" ]; then
    echo "PASS"
else
    echo "FAIL (expected 30, got ${RUN_OUT})"
    exit 1
fi

# Test 4: Stripping debug information
echo -n "Test 4: luac -s (strip debug info)... "
"${LUAC_BIN}" -s -o "${TMPDIR}/test1_stripped.luac" "${TMPDIR}/test1.lua"
RUN_STRIPPED_OUT=$("${LUA_BIN}" "${TMPDIR}/test1_stripped.luac")
if [ "${RUN_STRIPPED_OUT}" = "30" ]; then
    echo "PASS"
else
    echo "FAIL"
    exit 1
fi

# Test 5: Parse-only (-p)
echo -n "Test 5: luac -p (parse-only)... "
rm -f "${TMPDIR}/luac.out"
(cd "${TMPDIR}" && "${LUAC_BIN}" -p "${TMPDIR}/test1.lua")
if [ -f "${TMPDIR}/luac.out" ]; then
    echo "FAIL (luac.out was created during parse-only)"
    exit 1
fi
echo "PASS"

# Test 6: Syntax error handling
echo -n "Test 6: syntax error reporting... "
cat << 'EOF' > "${TMPDIR}/invalid.lua"
function broken(
EOF
if "${LUAC_BIN}" -p "${TMPDIR}/invalid.lua" 2>/dev/null; then
    echo "FAIL (expected error on broken syntax)"
    exit 1
fi
echo "PASS"

# Test 7: Multi-file combination
echo -n "Test 7: multi-file combination... "
cat << 'EOF' > "${TMPDIR}/a.lua"
print("hello from a")
EOF
cat << 'EOF' > "${TMPDIR}/b.lua"
print("hello from b")
EOF
"${LUAC_BIN}" -o "${TMPDIR}/combined.luac" "${TMPDIR}/a.lua" "${TMPDIR}/b.lua"
COMBINED_OUT=$("${LUA_BIN}" "${TMPDIR}/combined.luac")
EXPECTED_COMBINED=$(printf "hello from a\nhello from b")
if [ "${COMBINED_OUT}" = "${EXPECTED_COMBINED}" ]; then
    echo "PASS"
else
    echo "FAIL (got: ${COMBINED_OUT})"
    exit 1
fi

# Test 8: Stdin processing
echo -n "Test 8: stdin compilation... "
STDIN_OUT=$(echo "print('from stdin')" | "${LUAC_BIN}" -o "${TMPDIR}/stdin.luac" - && "${LUA_BIN}" "${TMPDIR}/stdin.luac")
if [ "${STDIN_OUT}" = "from stdin" ]; then
    echo "PASS"
else
    echo "FAIL (got: ${STDIN_OUT})"
    exit 1
fi

echo "=== All luac Tests Passed! ==="
