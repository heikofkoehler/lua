#ifndef LUA_OPCODE_HPP
#define LUA_OPCODE_HPP

#include "common/common.hpp"

// Bytecode instruction set for stack-based VM
// Each instruction is a single byte (opcode) possibly followed by operands

enum class OpCode : uint8_t {
    // Constants
    OP_CONSTANT,    // Load constant from constant pool [index: uint8_t]
    OP_NIL,         // Push nil
    OP_TRUE,        // Push true
    OP_FALSE,       // Push false

    // Variables
    OP_GET_GLOBAL,  // Get global variable [name_index: uint8_t]
    OP_SET_GLOBAL,  // Set global variable [name_index: uint8_t]
    OP_GET_LOCAL,   // Get local variable [slot: uint8_t]
    OP_SET_LOCAL,   // Set local variable [slot: uint8_t]
    OP_GET_UPVALUE, // Get upvalue [index: uint8_t]
    OP_SET_UPVALUE, // Set upvalue [index: uint8_t]
    OP_GET_TABUP,   // Get table from upvalue [upIndex: uint8_t, constIndex: uint8_t]
    OP_SET_TABUP,   // Set table from upvalue [upIndex: uint8_t, constIndex: uint8_t]
    OP_CLOSE_UPVALUE, // Close upvalue at top of stack

    // Arithmetic operations (binary)
    OP_ADD,         // Addition: pop b, pop a, push a + b
    OP_SUB,         // Subtraction: pop b, pop a, push a - b
    OP_MUL,         // Multiplication: pop b, pop a, push a * b
    OP_DIV,         // Division: pop b, pop a, push a / b
    OP_IDIV,        // Integer division: pop b, pop a, push a // b
    OP_MOD,         // Modulo: pop b, pop a, push a % b
    OP_POW,         // Power: pop b, pop a, push a ^ b
    OP_BAND,        // Bitwise AND: pop b, pop a, push a & b
    OP_BOR,         // Bitwise OR: pop b, pop a, push a | b
    OP_BXOR,        // Bitwise XOR: pop b, pop a, push a ~ b
    OP_SHL,         // Bitwise left shift: pop b, pop a, push a << b
    OP_SHR,         // Bitwise right shift: pop b, pop a, push a >> b
    OP_CONCAT,      // Concatenation: pop b, pop a, push a .. b

    // Unary operations
    OP_NEG,         // Negation: pop a, push -a
    OP_NOT,         // Logical not: pop a, push !a
    OP_BNOT,        // Bitwise NOT: pop a, push ~a
    OP_LEN,         // Length: pop a, push #a

    // Comparison operations
    OP_EQUAL,       // Equality: pop b, pop a, push a == b
    OP_LESS,        // Less than: pop b, pop a, push a < b
    OP_LESS_EQUAL,  // Less or equal: pop b, pop a, push a <= b
    OP_GREATER,     // Greater than: pop b, pop a, push a > b
    OP_GREATER_EQUAL, // Greater or equal: pop b, pop a, push a >= b

    // I/O operations (MVP)
    OP_PRINT,       // Print top of stack (doesn't pop)

    // Control flow
    OP_POP,         // Pop and discard top of stack
    OP_DUP,         // Duplicate top of stack
    OP_SWAP,        // Swap top two values on stack
    OP_ROTATE,      // Rotate top N values on stack [n: uint8_t]
    OP_JUMP,        // Unconditional jump [offset: uint16_t]
    OP_JUMP_IF_FALSE, // Jump if top of stack is falsey [offset: uint16_t]
    OP_LOOP,        // Jump backward [offset: uint16_t]

    // Functions
    OP_CLOSURE,     // Load function constant [index: uint8_t]
    OP_CALL,          // call function [arg_count: uint8_t, ret_count: uint8_t]
    OP_CALL_MULTI,    // call with variable args (last arg was multires) [fixed_arg_count: uint8_t, ret_count: uint8_t]
    OP_TAILCALL,      // tail call function [arg_count: uint8_t] (always returns all results to caller)
    OP_TAILCALL_MULTI,// tail call with variable args [fixed_arg_count: uint8_t]
    OP_RETURN_VALUE,  // return with values from function [count: uint8_t] (0 means all from lastResultCount)

    // Tables
    OP_NEW_TABLE,   // Create new table, push onto stack
    OP_GET_TABLE,   // Get table[key]: pop key, pop table, push value
    OP_SET_TABLE,   // Set table[key] = value: pop value, pop key, pop table
    OP_SET_TABLE_MULTI, // Set table[key] = lastResultCount values (for table constructors)

    // File I/O
    OP_IO_OPEN,     // Open file: pop mode, pop filename, push file handle (or nil)
    OP_IO_WRITE,    // Write to file: pop data, pop file handle
    OP_IO_READ,     // Read from file: pop file handle, push data
    OP_IO_CLOSE,    // Close file: pop file handle

    // Varargs
    OP_GET_VARARG,  // Push varargs onto stack [ret_count: uint8_t] (0 means all)

    OP_YIELD,       // Yield from coroutine [args: uint8_t, returns: uint8_t]

    OP_RETURN,      // Return from current chunk
};

// Get human-readable name for opcode
inline const char* opcodeName(OpCode op) {
    switch (op) {
        case OpCode::OP_CONSTANT:    return "OP_CONSTANT";
        case OpCode::OP_NIL:         return "OP_NIL";
        case OpCode::OP_TRUE:        return "OP_TRUE";
        case OpCode::OP_FALSE:       return "OP_FALSE";
        case OpCode::OP_GET_GLOBAL:  return "OP_GET_GLOBAL";
        case OpCode::OP_SET_GLOBAL:  return "OP_SET_GLOBAL";
        case OpCode::OP_GET_LOCAL:     return "OP_GET_LOCAL";
        case OpCode::OP_SET_LOCAL:     return "OP_SET_LOCAL";
        case OpCode::OP_GET_UPVALUE:   return "OP_GET_UPVALUE";
        case OpCode::OP_SET_UPVALUE:   return "OP_SET_UPVALUE";
        case OpCode::OP_GET_TABUP:     return "OP_GET_TABUP";
        case OpCode::OP_SET_TABUP:     return "OP_SET_TABUP";
        case OpCode::OP_CLOSE_UPVALUE: return "OP_CLOSE_UPVALUE";
        case OpCode::OP_ADD:           return "OP_ADD";
        case OpCode::OP_SUB:         return "OP_SUB";
        case OpCode::OP_MUL:         return "OP_MUL";
        case OpCode::OP_DIV:           return "OP_DIV";
        case OpCode::OP_IDIV:          return "OP_IDIV";
        case OpCode::OP_MOD:           return "OP_MOD";
        case OpCode::OP_POW:           return "OP_POW";
        case OpCode::OP_BAND:          return "OP_BAND";
        case OpCode::OP_BOR:           return "OP_BOR";
        case OpCode::OP_BXOR:          return "OP_BXOR";
        case OpCode::OP_SHL:           return "OP_SHL";
        case OpCode::OP_SHR:           return "OP_SHR";
        case OpCode::OP_CONCAT:        return "OP_CONCAT";
        case OpCode::OP_NEG:           return "OP_NEG";
        case OpCode::OP_NOT:           return "OP_NOT";
        case OpCode::OP_BNOT:          return "OP_BNOT";
        case OpCode::OP_LEN:           return "OP_LEN";

        case OpCode::OP_EQUAL:         return "OP_EQUAL";
        case OpCode::OP_LESS:          return "OP_LESS";
        case OpCode::OP_LESS_EQUAL:    return "OP_LESS_EQUAL";
        case OpCode::OP_GREATER:       return "OP_GREATER";
        case OpCode::OP_GREATER_EQUAL: return "OP_GREATER_EQUAL";
        case OpCode::OP_PRINT:         return "OP_PRINT";
        case OpCode::OP_POP:           return "OP_POP";
        case OpCode::OP_DUP:           return "OP_DUP";
        case OpCode::OP_SWAP:          return "OP_SWAP";
        case OpCode::OP_ROTATE:        return "OP_ROTATE";
        case OpCode::OP_JUMP:          return "OP_JUMP";
        case OpCode::OP_JUMP_IF_FALSE: return "OP_JUMP_IF_FALSE";
        case OpCode::OP_LOOP:          return "OP_LOOP";
        case OpCode::OP_CLOSURE:       return "OP_CLOSURE";
        case OpCode::OP_CALL:          return "OP_CALL";
        case OpCode::OP_CALL_MULTI:    return "OP_CALL_MULTI";
        case OpCode::OP_TAILCALL:      return "OP_TAILCALL";
        case OpCode::OP_TAILCALL_MULTI: return "OP_TAILCALL_MULTI";
        case OpCode::OP_RETURN_VALUE:  return "OP_RETURN_VALUE";
        case OpCode::OP_NEW_TABLE:     return "OP_NEW_TABLE";
        case OpCode::OP_GET_TABLE:     return "OP_GET_TABLE";
        case OpCode::OP_SET_TABLE:     return "OP_SET_TABLE";
        case OpCode::OP_SET_TABLE_MULTI: return "OP_SET_TABLE_MULTI";
        case OpCode::OP_IO_OPEN:       return "OP_IO_OPEN";
        case OpCode::OP_IO_WRITE:      return "OP_IO_WRITE";
        case OpCode::OP_IO_READ:       return "OP_IO_READ";
        case OpCode::OP_IO_CLOSE:      return "OP_IO_CLOSE";
        case OpCode::OP_GET_VARARG:    return "OP_GET_VARARG";
        case OpCode::OP_YIELD:         return "OP_YIELD";
        case OpCode::OP_RETURN:        return "OP_RETURN";
        default:                     return "UNKNOWN";
    }
}

#endif // LUA_OPCODE_HPP
