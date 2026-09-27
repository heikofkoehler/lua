#ifndef LUA_RINSTRUCTION_HPP
#define LUA_RINSTRUCTION_HPP

// Register VM instruction format (Phase 1 of register-vm migration).
//
// Fixed 32-bit instructions, PUC-Lua-style encoding:
//
//   iABC:  | opcode:8 | A:8 | B:8 | C:8 |
//   iABx:  | opcode:8 | A:8 | Bx:16     |
//   iAsBx: | opcode:8 | A:8 | sBx:16    |  (sBx biased by 32768)
//
// - 8-bit opcode: 256 available, ~40 used.
// - 8-bit A/B/C: 256 registers per function.
// - 16-bit Bx: 65K constants / jump table entries. No LONG variants.
// - B/C are always registers in iABC. Constants go through ROP_LOADK.
//   (The register allocator hoists loop-invariant constants.)
//
// This header is standalone: it introduces no behavior change. Phase 2
// (rcodegen) will emit these; Phase 3 (rrun) will execute them.

#include <cstdint>
#include <string>
#include <vector>

using RInstruction = uint32_t;

// Bias for signed jump offsets in iAsBx format.
constexpr int R_SBX_BIAS = 32768;
constexpr int R_SBX_MAX = 32767;
constexpr int R_SBX_MIN = -32768;

enum class ROpCode : uint8_t {
    // Constants & moves
    ROP_LOADK,      // A Bx     R(A) = K[Bx]
    ROP_LOADNIL,    // A B      R(A)..R(A+B) = nil
    ROP_LOADBOOL,   // A B C    R(A) = (B != 0); if (C) pc++
    ROP_MOVE,       // A B      R(A) = R(B)

    // Arithmetic: A B C  R(A) = R(B) op R(C)
    ROP_ADD,
    ROP_SUB,
    ROP_MUL,
    ROP_DIV,
    ROP_IDIV,
    ROP_MOD,
    ROP_POW,
    ROP_BAND,
    ROP_BOR,
    ROP_BXOR,
    ROP_SHL,
    ROP_SHR,
    ROP_CONCAT,     // A B C    R(A) = R(B) .. R(C)

    // Unary: A B  R(A) = op R(B)
    ROP_NEG,
    ROP_NOT,
    ROP_BNOT,
    ROP_LEN,

    // Comparison: A B C  if ((R(B) op R(C)) != A) pc++
    // (A=1 means "skip if true", used with inverted jumps)
    ROP_EQ,
    ROP_LT,
    ROP_LE,

    // Test: A C  if (truthy(R(A)) != C) pc++
    ROP_TEST,

    // Unconditional jump
    ROP_JMP,        // sBx      pc += sBx

    // Tables
    ROP_NEWTABLE,   // A B C    R(A) = new table (B = array hint, C = hash hint)
    ROP_GETTABLE,   // A B C    R(A) = R(B)[R(C)]
    ROP_SETTABLE,   // A B C    R(A)[R(B)] = R(C)

    // Upvalues
    ROP_GETUPVAL,   // A B      R(A) = Up[B]
    ROP_SETUPVAL,   // A B      Up[B] = R(A)
    ROP_GETTABUP,   // A B C    R(A) = Up[B][K[C]]  (_ENV global lookup)
    ROP_SETTABUP,   // A B C    Up[B][K[C]] = R(A)

    // Calls: B = arg count + 1 (0 = multires), C = ret count + 1 (0 = multires)
    ROP_CALL,       // A B C    R(A)..R(A+C-2) = R(A)(R(A+1)..R(A+B-1))
    ROP_TAILCALL,   // A B      return R(A)(R(A+1)..R(A+B-1))
    ROP_RETURN,     // A B      return R(A)..R(A+B-2)  (B=1: return no values)
    ROP_VARARG,     // A B      R(A)..R(A+B-2) = varargs (B=1: no values)

    // Closures: followed by B pseudo-instructions (one per upvalue:
    //   0 = move from register, 1 = capture upvalue) — decoded by rrun.
    ROP_CLOSURE,    // A Bx     R(A) = closure(K[Bx])

    // Numeric for: R(A)=index, R(A+1)=limit, R(A+2)=step
    ROP_FORPREP,    // A sBx    init; pc += sBx
    ROP_FORLOOP,    // A sBx    step; if not done: pc += sBx

    // Close upvalues / to-be-closed variables with register >= A
    ROP_CLOSE,      // A

    ROP_COUNT  // Sentinel: number of opcodes
};

// ---------------------------------------------------------------------------
// Encoding
// ---------------------------------------------------------------------------

inline RInstruction ropEncodeABC(ROpCode op, uint8_t a, uint8_t b, uint8_t c) {
    return (static_cast<uint32_t>(op)      ) |
           (static_cast<uint32_t>(a) <<  8) |
           (static_cast<uint32_t>(b) << 16) |
           (static_cast<uint32_t>(c) << 24);
}

inline RInstruction ropEncodeABx(ROpCode op, uint8_t a, uint16_t bx) {
    return (static_cast<uint32_t>(op)      ) |
           (static_cast<uint32_t>(a) <<  8) |
           (static_cast<uint32_t>(bx) << 16);
}

inline RInstruction ropEncodeAsBx(ROpCode op, uint8_t a, int16_t sbx) {
    return ropEncodeABx(op, a, static_cast<uint16_t>(sbx + R_SBX_BIAS));
}

// ---------------------------------------------------------------------------
// Decoding
// ---------------------------------------------------------------------------

inline ROpCode ropGetOp(RInstruction i) {
    return static_cast<ROpCode>(i & 0xFF);
}

inline uint8_t ropGetA(RInstruction i) {
    return static_cast<uint8_t>((i >> 8) & 0xFF);
}

inline uint8_t ropGetB(RInstruction i) {
    return static_cast<uint8_t>((i >> 16) & 0xFF);
}

inline uint8_t ropGetC(RInstruction i) {
    return static_cast<uint8_t>((i >> 24) & 0xFF);
}

inline uint16_t ropGetBx(RInstruction i) {
    return static_cast<uint16_t>((i >> 16) & 0xFFFF);
}

inline int16_t ropGetSBx(RInstruction i) {
    return static_cast<int16_t>(ropGetBx(i) - R_SBX_BIAS);
}

// ---------------------------------------------------------------------------
// Names (for disassembler / debugging)
// ---------------------------------------------------------------------------

inline const char* ropName(ROpCode op) {
    switch (op) {
        case ROpCode::ROP_LOADK:    return "LOADK";
        case ROpCode::ROP_LOADNIL:  return "LOADNIL";
        case ROpCode::ROP_LOADBOOL: return "LOADBOOL";
        case ROpCode::ROP_MOVE:     return "MOVE";
        case ROpCode::ROP_ADD:      return "ADD";
        case ROpCode::ROP_SUB:      return "SUB";
        case ROpCode::ROP_MUL:      return "MUL";
        case ROpCode::ROP_DIV:      return "DIV";
        case ROpCode::ROP_IDIV:     return "IDIV";
        case ROpCode::ROP_MOD:      return "MOD";
        case ROpCode::ROP_POW:      return "POW";
        case ROpCode::ROP_BAND:     return "BAND";
        case ROpCode::ROP_BOR:      return "BOR";
        case ROpCode::ROP_BXOR:     return "BXOR";
        case ROpCode::ROP_SHL:      return "SHL";
        case ROpCode::ROP_SHR:      return "SHR";
        case ROpCode::ROP_CONCAT:   return "CONCAT";
        case ROpCode::ROP_NEG:      return "NEG";
        case ROpCode::ROP_NOT:      return "NOT";
        case ROpCode::ROP_BNOT:     return "BNOT";
        case ROpCode::ROP_LEN:      return "LEN";
        case ROpCode::ROP_EQ:       return "EQ";
        case ROpCode::ROP_LT:       return "LT";
        case ROpCode::ROP_LE:       return "LE";
        case ROpCode::ROP_TEST:     return "TEST";
        case ROpCode::ROP_JMP:      return "JMP";
        case ROpCode::ROP_NEWTABLE: return "NEWTABLE";
        case ROpCode::ROP_GETTABLE: return "GETTABLE";
        case ROpCode::ROP_SETTABLE: return "SETTABLE";
        case ROpCode::ROP_GETUPVAL: return "GETUPVAL";
        case ROpCode::ROP_SETUPVAL: return "SETUPVAL";
        case ROpCode::ROP_GETTABUP: return "GETTABUP";
        case ROpCode::ROP_SETTABUP: return "SETTABUP";
        case ROpCode::ROP_CALL:     return "CALL";
        case ROpCode::ROP_TAILCALL: return "TAILCALL";
        case ROpCode::ROP_RETURN:   return "RETURN";
        case ROpCode::ROP_VARARG:   return "VARARG";
        case ROpCode::ROP_CLOSURE:  return "CLOSURE";
        case ROpCode::ROP_FORPREP:  return "FORPREP";
        case ROpCode::ROP_FORLOOP:  return "FORLOOP";
        case ROpCode::ROP_CLOSE:    return "CLOSE";
        default:                    return "UNKNOWN";
    }
}

// Operand format for the disassembler.
enum class ROpFormat {
    ABC,   // A, B, C registers
    ABx,   // A register, Bx unsigned
    AsBx,  // A register, sBx signed
    AB,    // A, B registers (C unused)
    A,     // A register only
};

inline ROpFormat ropFormat(ROpCode op) {
    switch (op) {
        case ROpCode::ROP_LOADK:
        case ROpCode::ROP_CLOSURE:
            return ROpFormat::ABx;
        case ROpCode::ROP_JMP:
        case ROpCode::ROP_FORPREP:
        case ROpCode::ROP_FORLOOP:
            return ROpFormat::AsBx;
        case ROpCode::ROP_LOADNIL:
        case ROpCode::ROP_MOVE:
        case ROpCode::ROP_NEG:
        case ROpCode::ROP_NOT:
        case ROpCode::ROP_BNOT:
        case ROpCode::ROP_LEN:
        case ROpCode::ROP_GETUPVAL:
        case ROpCode::ROP_SETUPVAL:
        case ROpCode::ROP_TAILCALL:
            return ROpFormat::AB;
        case ROpCode::ROP_CLOSE:
            return ROpFormat::A;
        default:
            return ROpFormat::ABC;
    }
}

// ---------------------------------------------------------------------------
// Disassembler
// ---------------------------------------------------------------------------

// Disassemble one instruction. pc is the instruction index (for jump targets).
inline std::string ropDisassemble(RInstruction instr, size_t pc) {
    ROpCode op = ropGetOp(instr);
    uint8_t a = ropGetA(instr);
    char buf[128];

    switch (ropFormat(op)) {
        case ROpFormat::ABC: {
            uint8_t b = ropGetB(instr);
            uint8_t c = ropGetC(instr);
            snprintf(buf, sizeof(buf), "%-10s R%d R%d R%d",
                     ropName(op), a, b, c);
            break;
        }
        case ROpFormat::ABx: {
            uint16_t bx = ropGetBx(instr);
            snprintf(buf, sizeof(buf), "%-10s R%d %u",
                     ropName(op), a, bx);
            break;
        }
        case ROpFormat::AsBx: {
            int16_t sbx = ropGetSBx(instr);
            // Show absolute jump target: pc+1+sbx (pc points at this instr)
            long target = static_cast<long>(pc) + 1 + sbx;
            snprintf(buf, sizeof(buf), "%-10s R%d %+d  ; -> %ld",
                     ropName(op), a, sbx, target);
            break;
        }
        case ROpFormat::AB: {
            uint8_t b = ropGetB(instr);
            snprintf(buf, sizeof(buf), "%-10s R%d R%d",
                     ropName(op), a, b);
            break;
        }
        case ROpFormat::A: {
            snprintf(buf, sizeof(buf), "%-10s R%d", ropName(op), a);
            break;
        }
    }
    return std::string(buf);
}

// Disassemble a full instruction sequence to stdout.
inline void ropDisassembleCode(const std::vector<RInstruction>& code,
                              const std::string& name) {
    printf("--- Register chunk: %s ---\n", name.c_str());
    printf("Instructions (%zu):\n", code.size());
    for (size_t pc = 0; pc < code.size(); pc++) {
        std::string text = ropDisassemble(code[pc], pc);
        printf("  [%4zu] %s\n", pc, text.c_str());
    }
}

#endif // LUA_RINSTRUCTION_HPP
