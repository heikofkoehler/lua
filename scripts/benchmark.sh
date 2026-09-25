#!/bin/bash
set -e

DIR="$(cd "$(dirname "$0")/.." && pwd)"
LUA_BIN="${DIR}/build/lua"

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
