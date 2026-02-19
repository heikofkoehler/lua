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
    OP_CLOSE_UPVALUE, // Close upvalue at top of stack

    // Arithmetic operations (binary)
    OP_ADD,         // Addition: pop b, pop a, push a + b
    OP_SUB,         // Subtraction: pop b, pop a, push a - b
    OP_MUL,         // Multiplication: pop b, pop a, push a * b
    OP_DIV,         // Division: pop b, pop a, push a / b
    OP_MOD,         // Modulo: pop b, pop a, push a % b
    OP_POW,         // Power: pop b, pop a, push a ^ b

    // Unary operations
    OP_NEG,         // Negation: pop a, push -a
    OP_NOT,         // Logical not: pop a, push !a

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
    OP_JUMP,        // Unconditional jump [offset: uint16_t]
    OP_JUMP_IF_FALSE, // Jump if top of stack is falsey [offset: uint16_t]
    OP_LOOP,        // Jump backward [offset: uint16_t]

    // Functions
    OP_CLOSURE,     // Load function constant [index: uint8_t]
    OP_CALL,        // Call function [arg_count: uint8_t]
    OP_RETURN_VALUE, // Return with value from function

    // Tables
    OP_NEW_TABLE,   // Create new table, push onto stack
    OP_GET_TABLE,   // Get table[key]: pop key, pop table, push value
    OP_SET_TABLE,   // Set table[key] = value: pop value, pop key, pop table

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
        case OpCode::OP_CLOSE_UPVALUE: return "OP_CLOSE_UPVALUE";
        case OpCode::OP_ADD:           return "OP_ADD";
        case OpCode::OP_SUB:         return "OP_SUB";
        case OpCode::OP_MUL:         return "OP_MUL";
        case OpCode::OP_DIV:         return "OP_DIV";
        case OpCode::OP_MOD:         return "OP_MOD";
        case OpCode::OP_POW:         return "OP_POW";
        case OpCode::OP_NEG:         return "OP_NEG";
        case OpCode::OP_NOT:         return "OP_NOT";
        case OpCode::OP_EQUAL:         return "OP_EQUAL";
        case OpCode::OP_LESS:          return "OP_LESS";
        case OpCode::OP_LESS_EQUAL:    return "OP_LESS_EQUAL";
        case OpCode::OP_GREATER:       return "OP_GREATER";
        case OpCode::OP_GREATER_EQUAL: return "OP_GREATER_EQUAL";
        case OpCode::OP_PRINT:         return "OP_PRINT";
        case OpCode::OP_POP:           return "OP_POP";
        case OpCode::OP_JUMP:          return "OP_JUMP";
        case OpCode::OP_JUMP_IF_FALSE: return "OP_JUMP_IF_FALSE";
        case OpCode::OP_LOOP:          return "OP_LOOP";
        case OpCode::OP_CLOSURE:       return "OP_CLOSURE";
        case OpCode::OP_CALL:          return "OP_CALL";
        case OpCode::OP_RETURN_VALUE:  return "OP_RETURN_VALUE";
        case OpCode::OP_NEW_TABLE:     return "OP_NEW_TABLE";
        case OpCode::OP_GET_TABLE:     return "OP_GET_TABLE";
        case OpCode::OP_SET_TABLE:     return "OP_SET_TABLE";
        case OpCode::OP_RETURN:        return "OP_RETURN";
        default:                     return "UNKNOWN";
    }
}

#endif // LUA_OPCODE_HPP
