# Bytecode Loader Fuzzing

Mutational fuzzer for the bytecode deserializer (`Chunk::deserialize` /
`FunctionObject::deserialize`). Catches memory errors, hangs, and
uncaught exceptions on hostile/corrupted bytecode.

## Building

Requires a sanitizer build. The `build-asan` directory is configured with
ASan+UBSan:

```bash
cd build-asan && cmake --build . -j$(nproc) --target lua
```

Then compile the fuzz driver against the sanitizer objects:

```bash
OBJS=$(find build-asan/CMakeFiles/lua.dir -name "*.o" | grep -v "src/main.cpp.o")
g++ -fsanitize=address,undefined -fno-omit-frame-pointer \
    -fno-sanitize-recover=all -g -std=c++17 \
    -I src -I include fuzz/fuzz_load.cpp $OBJS -o /tmp/fuzz_load
```

## Running

```bash
LD_PRELOAD=$(gcc -print-file-name=libasan.so) \
  ASAN_OPTIONS=halt_on_error=1 \
  UBSAN_OPTIONS=print_stacktrace=1,halt_on_error=1 \
  /tmp/fuzz_load fuzz/seeds 100000
```

- Seeds: `fuzz/seeds/` contains dumped bytecode from real Lua files.
  Regenerate with: `lua --nojit -e "dump script.lua to fuzz/seeds/"`
  (see `fuzz/gen_seeds.sh`).
- Any sanitizer report, hang, or uncaught exception is a bug.
- Expected: inputs either deserialize or throw a catchable `std::exception`.

## What it found

- 2026-09-26: Corrupted `idCount` (1.8B) hung `load()` in a huge
  `std::string` allocation. Fixed with `checkCount` sanity bounds.
- 2026-09-26: Unchecked string `len` in `Value::deserialize` allowed
  multi-GB allocations. Fixed with `MAX_STRING_LEN` bound.
- 100k iterations: no sanitizer reports, no hangs.
