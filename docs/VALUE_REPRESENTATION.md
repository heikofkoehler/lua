# Value Representation (NaN-Boxing)

The Lua VM uses an efficient **NaN-boxing** technique to represent all Lua values in a single 64-bit `double`. This minimizes memory usage and improves cache locality compared to traditional tagged unions.

## Concept

In the IEEE 754 floating-point standard, any value where all exponent bits are set (0x7FF) and at least one mantissa bit is set is considered a **NaN** (Not-a-Number). Double-precision floats have 52 mantissa bits, leaving plenty of space to store a type tag and a payload (pointer or integer) when the value is not a "real" number.

## Implementation Details

The implementation is found in `src/value/value.hpp`.

### Floating Point Numbers
All standard Lua numbers are stored directly as IEEE 754 doubles. As long as the exponent is not 0x7FF, the value is treated as a plain number.

### Tagged Values
If the value is a NaN, the lower bits are used to store the type and payload. We use a "Quiet NaN" with the sign bit set to distinguish our tagged values from potential hardware-generated NaNs.

| Type | Tag (Hex) | Payload |
|------|-----------|---------|
| Nil | 0xFFF1 | None |
| Boolean | 0xFFF2 | 0 (false) or 1 (true) |
| Integer | 0xFFF3 | 32-bit signed integer |
| String | 0xFFF4 | Index into string pool |
| Table | 0xFFF5 | Pointer to TableObject |
| Closure | 0xFFF6 | Pointer to ClosureObject |
| Function | 0xFFF7 | Pointer to FunctionObject |
| Native Function | 0xFFF8 | Index into native function table |
| Userdata | 0xFFF9 | Pointer to UserdataObject |
| Coroutine | 0xFFFA | Pointer to CoroutineObject |
| Upvalue | 0xFFFB | Pointer to UpvalueObject |
| C Function | 0xFFFC | Raw C function pointer |

### Pointer Storage
On 64-bit systems, only 48 bits of the address space are typically used. This allows us to store the pointer directly in the lower 48 bits of the 64-bit double, with the upper 16 bits acting as the NaN-box and type tag.

## Performance Benefits
- **Zero Overhead for Numbers**: No extra tagging or branching needed for basic arithmetic.
- **Stack Efficiency**: The VM stack is a simple `std::vector<Value>`, which is just a vector of `uint64_t` internally. This is very cache-friendly.
- **Small Footprint**: Every Lua value, no matter how complex, is always exactly 8 bytes.

## JIT Assembly Considerations

When generating native code for arithmetic operations, the JIT compiler must handle NaN-boxed values directly in ARM64 assembly:

### Double Extraction
For arithmetic operations, the JIT extracts the double value from a NaN-boxed value:
```arm64
// Check if value is a double (exponent != 0x7FF)
tst x_value, #0x7FF0000000000000
b.eq handle_non_double

// Value is already a double, use directly
fmov d_result, x_value
```

### Type Validation
Before arithmetic, the JIT validates that operands are numbers:
```arm64
// Extract type tag from NaN-box
ubfx x_tag, x_value, #48, #16
cmp x_tag, #0xFFF3  // Check for integer tag
b.eq convert_integer_to_double
cmp x_tag, #0x7FF   // Check for double (no NaN bits set)
b.ne type_error
```

### Boxing Results
After arithmetic, results are stored back as NaN-boxed doubles:
```arm64
// Result is already in d_result register
fmov x_result, d_result
// No additional boxing needed for doubles
```

This approach ensures the JIT maintains full compatibility with the interpreter's value representation while achieving native performance for hot numeric code paths.
