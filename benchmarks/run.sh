#!/bin/bash
set -e

DIR="$(cd "$(dirname "$0")/.." && pwd)"
LUA_BIN="${1:-${DIR}/build/lua}"

if [ ! -x "${LUA_BIN}" ]; then
    echo "Error: Lua binary '${LUA_BIN}' not found or not executable." >&2
    echo "Please build the project first (e.g. cmake --build build)." >&2
    exit 1
fi

echo "=========================================================="
echo " Lua Implementation Performance Benchmark Suite"
echo " Date: $(date)"
echo " Binary: ${LUA_BIN}"
echo "=========================================================="

BENCHMARKS=(
    "benchmarks/fib.lua"
    "benchmarks/mandelbrot.lua"
    "benchmarks/binary_trees.lua"
    "benchmarks/table_sort.lua"
    "benchmarks/spectral_norm.lua"
    "benchmarks/fannkuch.lua"
)

for b in "${BENCHMARKS[@]}"; do
    echo -n "Running $(basename "$b")... "
    "${LUA_BIN}" "${DIR}/$b"
done

echo "=========================================================="
echo " Benchmark run complete!"
echo "=========================================================="
