#!/bin/bash
set -e

DIR="$(cd "$(dirname "$0")/.." && pwd)"
LUA_BIN="${1:-${DIR}/build/lua}"
LSP_BIN="${2:-${DIR}/build/lua-lsp}"

# Convert to absolute paths
LUA_BIN="$(cd "$(dirname "$LUA_BIN")" && pwd)/$(basename "$LUA_BIN")"
LSP_BIN="$(cd "$(dirname "$LSP_BIN")" && pwd)/$(basename "$LSP_BIN")"

if [ ! -x "${LSP_BIN}" ]; then
    echo "Error: lua-lsp binary '${LSP_BIN}' not found or not executable." >&2
    exit 1
fi

echo "=== Running lua-lsp Test Suite ==="

# CLI Flag Tests
echo -n "Test CLI 1: lua-lsp -v... "
VERSION_OUT=$("${LSP_BIN}" -v)
if [[ "${VERSION_OUT}" == *"Lua Language Server"* ]]; then
    echo "PASS"
else
    echo "FAIL: ${VERSION_OUT}"
    exit 1
fi

echo -n "Test CLI 2: lua-lsp -h... "
HELP_OUT=$("${LSP_BIN}" -h)
if [[ "${HELP_OUT}" == *"Language Server Protocol"* ]]; then
    echo "PASS"
else
    echo "FAIL: ${HELP_OUT}"
    exit 1
fi

echo -n "Test CLI 3: lua --lsp... "
python3 -c "
import subprocess, json, sys
p = subprocess.Popen(['${LUA_BIN}', '--lsp'], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
req = json.dumps({'jsonrpc': '2.0', 'id': 1, 'method': 'initialize', 'params': {}})
p.stdin.write(f'Content-Length: {len(req)}\r\n\r\n{req}'.encode())
p.stdin.flush()
header = p.stdout.readline().decode()
p.stdout.readline()
body = p.stdout.read(int(header.split(':')[1]))
data = json.loads(body.decode())
assert data['id'] == 1 and 'capabilities' in data['result']
p.kill()
" && echo "PASS"

# Run JSON-RPC Protocol Test Suite
python3 "${DIR}/tests/test_lsp.py" "${LSP_BIN}"
