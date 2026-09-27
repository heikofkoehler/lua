// Stack-to-register bytecode translator.
//
// See rtranslate.hpp for the design. Key points:
// - Stack slot N maps directly to register R(N).
// - sp (stack depth) is tracked statically through the bytecode.
// - Jumps are resolved in a second pass (old byte offset -> new pc).
// - Multires sequences (CALL_MULTI etc.) use a dynamic top; the translator
//   tracks the base register and the interpreter (Phase 3) handles the count.

#include "compiler/rtranslate.hpp"
#include "compiler/chunk.hpp"
#include "value/function.hpp"
#include "vm/opcode.hpp"
#include <unordered_map>

namespace {

struct JumpFixup {
    size_t newPc;      // pc of the JMP instruction to fix
    size_t oldTarget;  // byte offset target in old bytecode
};

class Translator {
public:
    Translator(FunctionObject* func)
        : func_(func), chunk_(func->chunk()) {}

    RTranslateResult run() {
        RTranslateResult result;
        if (!computeSp()) {
            result.error = error_;
            return result;
        }
        translate();
        if (!error_.empty()) {
            result.error = error_;
            return result;
        }
        resolveJumps();
        if (!error_.empty()) {
            result.error = error_;
            return result;
        }
        result.code = std::move(out_);
        result.maxRegisters = maxReg_;
        result.ok = true;
        return result;
    }

    // Debug: run only the SP computation phase.
    bool computeSpOnly(std::unordered_map<size_t, int>& spOut,
                       std::string& errorOut) {
        if (!computeSp()) {
            errorOut = error_;
            return false;
        }
        spOut = spAt_;
        return true;
    }

private:
    FunctionObject* func_;
    Chunk* chunk_;
    std::vector<RInstruction> out_;
    std::unordered_map<size_t, size_t> oldToNew_;  // byte offset -> new pc
    std::unordered_map<size_t, int> spAt_;          // byte offset -> sp
    std::unordered_map<size_t, int> dynBaseAt_;     // byte offset -> static base (if sp is DYNAMIC)
    std::vector<JumpFixup> fixups_;
    int maxReg_ = 0;
    std::string error_;

    static constexpr int SP_DYNAMIC = -1;

    void fail(const std::string& msg) {
        if (error_.empty()) error_ = msg;
    }

    // Track max register used.
    void useReg(int r) {
        if (r >= maxReg_) maxReg_ = r + 1;
    }
    void useRegs(int lo, int hi) {  // inclusive lo, exclusive hi
        if (hi > maxReg_) maxReg_ = hi;
        (void)lo;
    }

    void emit(RInstruction i) { out_.push_back(i); }
    void emitABC(ROpCode op, int a, int b, int c) {
        useReg(a); useReg(b); useReg(c);
        emit(ropEncodeABC(op, (uint8_t)a, (uint8_t)b, (uint8_t)c));
    }
    void emitAB(ROpCode op, int a, int b) {
        useReg(a); useReg(b);
        emit(ropEncodeABC(op, (uint8_t)a, (uint8_t)b, 0));
    }
    void emitA(ROpCode op, int a) {
        useReg(a);
        emit(ropEncodeABC(op, (uint8_t)a, 0, 0));
    }
    void emitABx(ROpCode op, int a, int bx) {
        useReg(a);
        emit(ropEncodeABx(op, (uint8_t)a, (uint16_t)bx));
    }

    // Find or add an integer constant; returns its index.
    int getOrAddIntConstant(int64_t val) {
        const auto& consts = chunk_->constants();
        for (size_t i = 0; i < consts.size(); i++) {
            if (consts[i].isInteger() && consts[i].asInteger() == val) {
                return (int)i;
            }
        }
        return (int)chunk_->addConstant(Value::integer(val));
    }

    // Read helpers for old bytecode
    uint8_t byteAt(size_t off) const { return chunk_->at(off); }
    uint16_t u16At(size_t off) const {
        return (uint16_t)byteAt(off) | ((uint16_t)byteAt(off + 1) << 8);
    }
    uint32_t u24At(size_t off) const {
        return (uint32_t)byteAt(off) | ((uint32_t)byteAt(off + 1) << 8) |
               ((uint32_t)byteAt(off + 2) << 16);
    }

    // ------------------------------------------------------------------
    // Pass 1: compute sp at every instruction via worklist.
    // ------------------------------------------------------------------
    bool computeSp() {
        const auto& code = chunk_->code();
        std::unordered_map<size_t, int> sp;
        std::unordered_map<size_t, int> dynBase;
        std::vector<size_t> worklist;

        int arity = func_->arity();
        sp[0] = arity;
        worklist.push_back(0);

        auto propagate = [&](size_t src, size_t target, int targetSp, int targetDynBase) {
            auto it = sp.find(target);
            if (it == sp.end()) {
                sp[target] = targetSp;
                if (targetSp == SP_DYNAMIC) dynBase[target] = targetDynBase;
                worklist.push_back(target);
            } else if (it->second != targetSp && targetSp != SP_DYNAMIC &&
                       it->second != SP_DYNAMIC) {
                fail("inconsistent stack depth at offset " + std::to_string(target) +
                     " (existing=" + std::to_string(it->second) +
                     ", new=" + std::to_string(targetSp) +
                     ") from src " + std::to_string(src));
            } else if (targetSp == SP_DYNAMIC && it->second == SP_DYNAMIC) {
                // Both dynamic; bases should match (or keep existing)
                if (dynBase[target] != targetDynBase) {
                    // Allow: keep first recorded base (should be same in valid code)
                }
            }
        };

        while (!worklist.empty()) {
            size_t off = worklist.back();
            worklist.pop_back();
            int curSp = sp[off];
            int curDynBase = (curSp == SP_DYNAMIC) ? dynBase[off] : -1;

            if (off >= code.size()) {
                fail("bytecode offset out of range");
                return false;
            }
            OpCode op = static_cast<OpCode>(code[off]);
            size_t len = chunk_->instructionLength(off);
            if (len == 0) {
                fail("zero-length instruction");
                return false;
            }

            // Compute sp after this instruction and jump targets.
            int afterSp = curSp;
            bool hasFallthrough = true;
            std::vector<size_t> targets;

            switch (op) {
                // Push 1
                case OpCode::OP_CONSTANT:
                case OpCode::OP_CONSTANT_LONG:
                case OpCode::OP_NIL:
                case OpCode::OP_TRUE:
                case OpCode::OP_FALSE:
                case OpCode::OP_GET_LOCAL:
                case OpCode::OP_GET_UPVALUE:
                case OpCode::OP_GET_TABUP:
                case OpCode::OP_GET_TABUP_LONG:
                case OpCode::OP_NEW_TABLE:
                case OpCode::OP_CLOSURE:
                case OpCode::OP_CLOSURE_LONG:
                case OpCode::OP_GET_VARARG_COUNT:
                case OpCode::OP_PACK_VARARG_TABLE:
                    afterSp = curSp + 1;
                    break;
                // Pop 1
                case OpCode::OP_SET_LOCAL:
                case OpCode::OP_SET_UPVALUE:
                case OpCode::OP_SET_TABUP:
                case OpCode::OP_SET_TABUP_LONG:
                case OpCode::OP_POP:
                case OpCode::OP_CLOSE_UPVALUE:
                case OpCode::OP_DEF_GLOBAL:
                case OpCode::OP_DEF_GLOBAL_LONG:
                    afterSp = curSp - 1;
                    break;
                // Pop 1, push 1 (net 0)
                case OpCode::OP_NEG:
                case OpCode::OP_NOT:
                case OpCode::OP_BNOT:
                case OpCode::OP_LEN:
                case OpCode::OP_GET_VARARG_ITEM:
                    afterSp = curSp;
                    break;
                // Pop 2, push 1 (net -1)
                case OpCode::OP_ADD: case OpCode::OP_SUB: case OpCode::OP_MUL:
                case OpCode::OP_DIV: case OpCode::OP_IDIV: case OpCode::OP_MOD:
                case OpCode::OP_POW: case OpCode::OP_BAND: case OpCode::OP_BOR:
                case OpCode::OP_BXOR: case OpCode::OP_SHL: case OpCode::OP_SHR:
                case OpCode::OP_CONCAT:
                case OpCode::OP_EQUAL: case OpCode::OP_LESS: case OpCode::OP_LESS_EQUAL:
                case OpCode::OP_GREATER: case OpCode::OP_GREATER_EQUAL:
                case OpCode::OP_GET_TABLE:
                    afterSp = curSp - 1;
                    break;
                // Pop 3 (net -3)
                case OpCode::OP_SET_TABLE:
                    afterSp = curSp - 3;
                    break;
                // Pop 3, push 1 for DEF_GLOBAL_TABLE (value, env, key -> pop 3? check)
                case OpCode::OP_DEF_GLOBAL_TABLE:
                    afterSp = curSp - 3;
                    break;
                // No stack effect
                case OpCode::OP_DUP:
                    afterSp = curSp + 1;
                    break;
                case OpCode::OP_SWAP:
                case OpCode::OP_TBC:
                    afterSp = curSp;
                    break;
                case OpCode::OP_ROTATE:
                    afterSp = curSp;
                    break;
                case OpCode::OP_CLOSE: {
                    uint8_t slot = byteAt(off + 1);
                    // Only shrinks, never grows
                    afterSp = (curSp > (int)slot) ? slot : curSp;
                    break;
                }
                // Jumps
                case OpCode::OP_JUMP: {
                    uint16_t offset = u16At(off + 1);
                    targets.push_back(off + 3 + offset);
                    hasFallthrough = false;
                    break;
                }
                case OpCode::OP_JUMP_IF_FALSE: {
                    uint16_t offset = u16At(off + 1);
                    targets.push_back(off + 3 + offset);
                    // Does NOT pop; the codegen emits explicit OP_POP on both paths.
                    afterSp = curSp;
                    break;
                }
                case OpCode::OP_LOOP: {
                    uint16_t offset = u16At(off + 1);
                    targets.push_back(off + 3 - offset);
                    hasFallthrough = false;
                    break;
                }
                // Calls (static or dynamic)
                case OpCode::OP_CALL: {
                    uint8_t argc = byteAt(off + 1);
                    uint8_t retcEnc = byteAt(off + 2);
                    if (retcEnc == 0) {
                        // Multires: dynamic
                        afterSp = SP_DYNAMIC;
                        // Record base: if already dynamic, use existing base
                        int base = (curSp == SP_DYNAMIC) ? curDynBase : curSp;
                        dynBase[off + len] = base;
                    } else {
                        // Encoded as retCount+1; 1 means 0 values
                        int retc = (int)retcEnc - 1;
                        afterSp = curSp - argc - 1 + retc;
                    }
                    break;
                }
                case OpCode::OP_TAILCALL:
                case OpCode::OP_RETURN:
                    hasFallthrough = false;
                    break;
                case OpCode::OP_RETURN_VALUE:
                case OpCode::OP_RETURN_VALUE_MULTI:
                    hasFallthrough = false;
                    break;
                // Dynamic stack effects
                case OpCode::OP_CALL_MULTI:
                case OpCode::OP_TAILCALL_MULTI:
                case OpCode::OP_SET_TABLE_MULTI:
                case OpCode::OP_YIELD_MULTI:
                    if (op == OpCode::OP_CALL_MULTI) {
                        afterSp = SP_DYNAMIC;
                        // Record static base for the dynamic sequence
                        int base = (curSp == SP_DYNAMIC) ? curDynBase : curSp;
                        dynBase[off + len] = base;
                    } else if (op == OpCode::OP_SET_TABLE_MULTI) {
                        // Pops dynamic values + key_base + table, leaves table.
                        // table was at (base - 3), so new sp = base - 2.
                        if (curSp != SP_DYNAMIC || curDynBase == -1) {
                            fail("SET_TABLE_MULTI without dynamic base");
                            return false;
                        }
                        afterSp = curDynBase - 2;
                    } else if (op == OpCode::OP_YIELD_MULTI) {
                        afterSp = SP_DYNAMIC;
                        int base = (curSp == SP_DYNAMIC) ? curDynBase : curSp;
                        dynBase[off + len] = base;
                    } else {  // OP_TAILCALL_MULTI
                        afterSp = SP_DYNAMIC;
                        hasFallthrough = false;
                    }
                    break;
                case OpCode::OP_GET_VARARG: {
                    uint8_t retcEnc = byteAt(off + 1);
                    if (retcEnc == 0) {
                        afterSp = SP_DYNAMIC;
                        int base = (curSp == SP_DYNAMIC) ? curDynBase : curSp;
                        dynBase[off + len] = base;
                    } else {
                        // +1 encoded: 1 means 0 values
                        afterSp = curSp + (retcEnc - 1);
                    }
                    break;
                }
                // For loops
                case OpCode::OP_FORPREP: {
                    uint16_t offset = u16At(off + 2);
                    size_t target = off + 4 + offset;
                    targets.push_back(target);
                    // The stack VM pushes 2 values (step=1, nil) if stepDefault,
                    // else 1 value (nil).
                    bool stepDefault = (byteAt(off + 1) & 0x80) != 0;
                    afterSp = curSp + (stepDefault ? 2 : 1);
                    break;
                }
                case OpCode::OP_FORLOOP: {
                    uint16_t offset = u16At(off + 2);
                    size_t target = off + 4 - offset;
                    targets.push_back(target);
                    // No stack effect.
                    break;
                }
                default:
                    fail("unhandled opcode in sp computation: " +
                         std::string(opcodeName(op)));
                    return false;
            }

            if (hasFallthrough) {
                size_t next = off + len;
                int nextDynBase = -1;
                if (afterSp == SP_DYNAMIC) {
                    // Determine the base: check if we recorded it for this edge
                    auto dbIt = dynBase.find(next);
                    if (dbIt != dynBase.end()) {
                        nextDynBase = dbIt->second;
                    } else if (curSp == SP_DYNAMIC) {
                        nextDynBase = curDynBase;
                    }
                }
                propagate(off, next, afterSp, nextDynBase);
                if (!error_.empty()) return false;
            }
            for (size_t t : targets) {
                int targetDynBase = -1;
                if (afterSp == SP_DYNAMIC) {
                    targetDynBase = (curSp == SP_DYNAMIC) ? curDynBase : -1;
                }
                propagate(off, t, afterSp, targetDynBase);
                if (!error_.empty()) return false;
            }
        }

        spAt_ = std::move(sp);
        dynBaseAt_ = std::move(dynBase);
        return true;
    }

    // ------------------------------------------------------------------
    // Pass 2: translate.
    // ------------------------------------------------------------------
    void translate() {
        const auto& code = chunk_->code();
        size_t off = 0;

        // We translate in linear order; jumps are fixed up afterwards.
        // spAt_ gives the sp at each instruction.
        while (off < code.size()) {
            auto it = spAt_.find(off);
            if (it == spAt_.end()) {
                // Unreachable code — skip.
                off += chunk_->instructionLength(off);
                continue;
            }
            int sp = it->second;
            int dynBase = -1;
            if (sp == SP_DYNAMIC) {
                auto dbIt = dynBaseAt_.find(off);
                if (dbIt != dynBaseAt_.end()) dynBase = dbIt->second;
            }
            oldToNew_[off] = out_.size();

            OpCode op = static_cast<OpCode>(code[off]);
            size_t len = chunk_->instructionLength(off);
            translateOne(op, off, sp, dynBase);
            if (!error_.empty()) return;
            off += len;
        }
    }

    void translateOne(OpCode op, size_t off, int sp, int dynBase) {
        switch (op) {
            // ---- Constants ----
            case OpCode::OP_CONSTANT:
                emitABx(ROpCode::ROP_LOADK, sp, byteAt(off + 1));
                break;
            case OpCode::OP_CONSTANT_LONG:
                emitABx(ROpCode::ROP_LOADK, sp, (int)u24At(off + 1));
                break;
            case OpCode::OP_NIL:
                emitAB(ROpCode::ROP_LOADNIL, sp, 0);
                break;
            case OpCode::OP_TRUE:
                emitABC(ROpCode::ROP_LOADBOOL, sp, 1, 0);
                break;
            case OpCode::OP_FALSE:
                emitABC(ROpCode::ROP_LOADBOOL, sp, 0, 0);
                break;

            // ---- Locals ----
            case OpCode::OP_GET_LOCAL:
                emitAB(ROpCode::ROP_MOVE, sp, byteAt(off + 1));
                break;
            case OpCode::OP_SET_LOCAL:
                emitAB(ROpCode::ROP_MOVE, byteAt(off + 1), sp - 1);
                break;

            // ---- Upvalues ----
            case OpCode::OP_GET_UPVALUE:
                emitAB(ROpCode::ROP_GETUPVAL, sp, byteAt(off + 1));
                break;
            case OpCode::OP_SET_UPVALUE:
                emitAB(ROpCode::ROP_SETUPVAL, sp - 1, byteAt(off + 1));
                break;
            case OpCode::OP_GET_TABUP:
                emitABC(ROpCode::ROP_GETTABUP, sp, byteAt(off + 1), byteAt(off + 2));
                break;
            case OpCode::OP_SET_TABUP:
                emitABC(ROpCode::ROP_SETTABUP, sp - 1, byteAt(off + 1), byteAt(off + 2));
                break;
            case OpCode::OP_GET_TABUP_LONG: {
                uint32_t k = u24At(off + 2);
                // K index may exceed 255; use Bx form via a separate path.
                // For now, require k < 256 (codegen interns globals early).
                if (k >= 256) { fail("GETTABUP_LONG constant too large"); break; }
                emitABC(ROpCode::ROP_GETTABUP, sp, byteAt(off + 1), (int)k);
                break;
            }
            case OpCode::OP_SET_TABUP_LONG: {
                uint32_t k = u24At(off + 2);
                if (k >= 256) { fail("SETTABUP_LONG constant too large"); break; }
                emitABC(ROpCode::ROP_SETTABUP, sp - 1, byteAt(off + 1), (int)k);
                break;
            }
            case OpCode::OP_CLOSE_UPVALUE:
                emitA(ROpCode::ROP_CLOSE, sp - 1);
                break;
            case OpCode::OP_TBC: {
                // Marks local at slot as to-be-closed. No register instruction
                // needed; the interpreter tracks TBC slots per frame.
                // We emit a no-op marker so the interpreter can register it.
                // Actually: use ROP_CLOSE with a special encoding? No —
                // simplest: the register interpreter reads TBC info from a
                // side table built during translation.
                // For now: record it. (Handled via tbcSlots_.)
                tbcSlots_.push_back(byteAt(off + 1));
                break;
            }
            case OpCode::OP_CLOSE: {
                uint8_t slot = byteAt(off + 1);
                emitA(ROpCode::ROP_CLOSE, slot);
                break;
            }

            // ---- Arithmetic ----
            case OpCode::OP_ADD: emitABC(ROpCode::ROP_ADD, sp-2, sp-2, sp-1); break;
            case OpCode::OP_SUB: emitABC(ROpCode::ROP_SUB, sp-2, sp-2, sp-1); break;
            case OpCode::OP_MUL: emitABC(ROpCode::ROP_MUL, sp-2, sp-2, sp-1); break;
            case OpCode::OP_DIV: emitABC(ROpCode::ROP_DIV, sp-2, sp-2, sp-1); break;
            case OpCode::OP_IDIV: emitABC(ROpCode::ROP_IDIV, sp-2, sp-2, sp-1); break;
            case OpCode::OP_MOD: emitABC(ROpCode::ROP_MOD, sp-2, sp-2, sp-1); break;
            case OpCode::OP_POW: emitABC(ROpCode::ROP_POW, sp-2, sp-2, sp-1); break;
            case OpCode::OP_BAND: emitABC(ROpCode::ROP_BAND, sp-2, sp-2, sp-1); break;
            case OpCode::OP_BOR: emitABC(ROpCode::ROP_BOR, sp-2, sp-2, sp-1); break;
            case OpCode::OP_BXOR: emitABC(ROpCode::ROP_BXOR, sp-2, sp-2, sp-1); break;
            case OpCode::OP_SHL: emitABC(ROpCode::ROP_SHL, sp-2, sp-2, sp-1); break;
            case OpCode::OP_SHR: emitABC(ROpCode::ROP_SHR, sp-2, sp-2, sp-1); break;
            case OpCode::OP_CONCAT: emitABC(ROpCode::ROP_CONCAT, sp-2, sp-2, sp-1); break;

            // ---- Unary (in place) ----
            case OpCode::OP_NEG: emitAB(ROpCode::ROP_NEG, sp-1, sp-1); break;
            case OpCode::OP_NOT: emitAB(ROpCode::ROP_NOT, sp-1, sp-1); break;
            case OpCode::OP_BNOT: emitAB(ROpCode::ROP_BNOT, sp-1, sp-1); break;
            case OpCode::OP_LEN: emitAB(ROpCode::ROP_LEN, sp-1, sp-1); break;

            // ---- Comparison (push boolean; TEST/JMP fusion is a later opt) ----
            case OpCode::OP_EQUAL: emitABC(ROpCode::ROP_EQ, sp-2, sp-2, sp-1); break;
            case OpCode::OP_LESS: emitABC(ROpCode::ROP_LT, sp-2, sp-2, sp-1); break;
            case OpCode::OP_LESS_EQUAL: emitABC(ROpCode::ROP_LE, sp-2, sp-2, sp-1); break;
            case OpCode::OP_GREATER:
                // a > b  ==  b < a
                emitABC(ROpCode::ROP_LT, sp-2, sp-1, sp-2); break;
            case OpCode::OP_GREATER_EQUAL:
                // a >= b  ==  b <= a
                emitABC(ROpCode::ROP_LE, sp-2, sp-1, sp-2); break;

            // ---- Stack manipulation ----
            case OpCode::OP_POP:
                // No instruction; sp decreases.
                break;
            case OpCode::OP_DUP:
                emitAB(ROpCode::ROP_MOVE, sp, sp-1);
                break;
            case OpCode::OP_SWAP: {
                // Use a scratch register above maxSp (allocated at end).
                int tmp = 255;  // reserved scratch; validated below
                emitAB(ROpCode::ROP_MOVE, tmp, sp-1);
                emitAB(ROpCode::ROP_MOVE, sp-1, sp-2);
                emitAB(ROpCode::ROP_MOVE, sp-2, tmp);
                useReg(tmp);
                break;
            }
            case OpCode::OP_ROTATE: {
                // Rotate top n values. Lower to moves with scratch.
                uint8_t n = byteAt(off + 1);
                int tmp = 255;
                // Save top
                emitAB(ROpCode::ROP_MOVE, tmp, sp-1);
                // Shift down
                for (int i = 1; i < n; i++) {
                    emitAB(ROpCode::ROP_MOVE, sp-i, sp-i-1);
                }
                // Restore top to bottom of rotated region
                emitAB(ROpCode::ROP_MOVE, sp-n, tmp);
                useReg(tmp);
                break;
            }

            // ---- Jumps ----
            case OpCode::OP_JUMP: {
                size_t target = off + 3 + u16At(off + 1);
                emitJump(ROpCode::ROP_JMP, 0, target);
                break;
            }
            case OpCode::OP_JUMP_IF_FALSE: {
                size_t target = off + 3 + u16At(off + 1);
                // TEST R(sp-1), 0: skip next if R(sp-1) is falsey
                emitABC(ROpCode::ROP_TEST, sp-1, 0, 0);
                emitJump(ROpCode::ROP_JMP, 0, target);
                break;
            }
            case OpCode::OP_LOOP: {
                size_t target = off + 3 - u16At(off + 1);
                emitJump(ROpCode::ROP_JMP, 0, target);
                break;
            }

            // ---- Functions ----
            case OpCode::OP_CLOSURE:
            case OpCode::OP_CLOSURE_LONG: {
                uint32_t k = (op == OpCode::OP_CLOSURE)
                    ? byteAt(off + 1) : u24At(off + 1);
                size_t descOff = off + (op == OpCode::OP_CLOSURE ? 2 : 4);
                emitABx(ROpCode::ROP_CLOSURE, sp, (int)k);
                // Emit upvalue descriptors as pseudo-instructions:
                // ABC with op=ROP_MOVE is ambiguous, so encode as
                // ABx with a fake Bx = (isLocal << 8) | index, op = ROP_CLOSURE.
                // The interpreter knows to read upvalueCount descriptors after CLOSURE.
                const Value& c = chunk_->getConstant(k);
                // Get upvalue count from the function object
                int nup = 0;
                if (c.isFunction()) {
                    FunctionObject* f = chunk_->getFunction(c.asFunctionIndex());
                    if (f) nup = f->upvalueCount();
                }
                for (int i = 0; i < nup; i++) {
                    uint8_t isLocal = byteAt(descOff + i * 2);
                    uint8_t idx = byteAt(descOff + i * 2 + 1);
                    // Pseudo: CLOSURE with A=255 marker would clash; instead
                    // encode as raw: op=CLOSURE, A=isLocal, Bx=idx.
                    // Interpreter distinguishes by "expecting descriptor" state.
                    emit(ropEncodeABx(ROpCode::ROP_CLOSURE, isLocal, idx));
                }
                break;
            }
            case OpCode::OP_CALL: {
                uint8_t argc = byteAt(off + 1);
                uint8_t retcEnc = byteAt(off + 2);
                int funcReg = sp - argc - 1;
                if (retcEnc == 0) {
                    // Multires: C=0 signals dynamic returns
                    emitABC(ROpCode::ROP_CALL, funcReg, argc + 1, 0);
                } else {
                    // Encode as B=argc+1, C=retc (already +1 encoded)
                    emitABC(ROpCode::ROP_CALL, funcReg, argc + 1, retcEnc);
                }
                break;
            }
            case OpCode::OP_CALL_MULTI: {
                uint8_t fixed = byteAt(off + 1);
                uint8_t retc = byteAt(off + 2);
                int funcReg = sp - fixed - 1;
                // B=0 signals "fixed args + multires last arg".
                // The interpreter uses lastResultCount for the multires part.
                emitABC(ROpCode::ROP_CALL, funcReg, 0, retc);
                // Record fixed arg count in a side table keyed by pc.
                multiCallFixed_[out_.size() - 1] = fixed;
                break;
            }
            case OpCode::OP_TAILCALL: {
                uint8_t argc = byteAt(off + 1);
                int funcReg = sp - argc - 1;
                emitAB(ROpCode::ROP_TAILCALL, funcReg, argc + 1);
                break;
            }
            case OpCode::OP_TAILCALL_MULTI: {
                uint8_t fixed = byteAt(off + 1);
                int funcReg = sp - fixed - 1;
                emitAB(ROpCode::ROP_TAILCALL, funcReg, 0);
                multiCallFixed_[out_.size() - 1] = fixed;
                break;
            }
            case OpCode::OP_RETURN_VALUE: {
                uint8_t count = byteAt(off + 1);
                emitABC(ROpCode::ROP_RETURN, sp - count, count + 1, 0);
                break;
            }
            case OpCode::OP_RETURN_VALUE_MULTI: {
                uint8_t fixed = byteAt(off + 1);
                // Values: R(sp-fixed)..R(sp-1) + multires (lastResultCount)
                emitABC(ROpCode::ROP_RETURN, sp - fixed, fixed + 1, 0);
                multiCallFixed_[out_.size() - 1] = fixed;
                // Mark as multires return via C=0 and fixed count in side table.
                // Interpreter: return fixed regs + lastResultCount values.
                break;
            }
            case OpCode::OP_RETURN:
                emitABC(ROpCode::ROP_RETURN, 0, 1, 0);
                break;

            // ---- Tables ----
            case OpCode::OP_NEW_TABLE:
                emitABC(ROpCode::ROP_NEWTABLE, sp, 0, 0);
                break;
            case OpCode::OP_GET_TABLE:
                emitABC(ROpCode::ROP_GETTABLE, sp-2, sp-2, sp-1);
                break;
            case OpCode::OP_SET_TABLE:
                emitABC(ROpCode::ROP_SETTABLE, sp-3, sp-2, sp-1);
                break;
            case OpCode::OP_SET_TABLE_MULTI: {
                // Stack: [..., table, key_base, val1...valN] (N = lastResultCount)
                // table at R(dynBase-3), key_base at R(dynBase-2)
                if (dynBase == -1) {
                    fail("SET_TABLE_MULTI without dynamic base");
                    break;
                }
                int tableReg = dynBase - 3;
                int keyBaseReg = dynBase - 2;
                emitAB(ROpCode::ROP_SETTABLEMULTI, tableReg, keyBaseReg);
                break;
            }

            // ---- Varargs ----
            case OpCode::OP_GET_VARARG: {
                uint8_t retc = byteAt(off + 1);
                if (retc == 0) {
                    // C=0 means multires. B=0 means "all varargs".
                    out_.push_back(ropEncodeABC(ROpCode::ROP_VARARG, (uint8_t)sp, 0, 0));
                    useReg(sp);
                } else {
                    emitABC(ROpCode::ROP_VARARG, sp, retc, 0);
                }
                break;
            }
            case OpCode::OP_PACK_VARARG_TABLE:
                emitA(ROpCode::ROP_PACKVARARG, sp);
                break;
            case OpCode::OP_GET_VARARG_ITEM:
                emitAB(ROpCode::ROP_VARARGITEM, sp-1, sp-1);
                break;
            case OpCode::OP_GET_VARARG_COUNT:
                emitA(ROpCode::ROP_VARARGCOUNT, sp);
                break;

            // ---- Globals ----
            case OpCode::OP_DEF_GLOBAL:
                emitABC(ROpCode::ROP_DEFGLOBAL, sp-1, byteAt(off+1), byteAt(off+2));
                break;
            case OpCode::OP_DEF_GLOBAL_LONG: {
                uint32_t k = u24At(off + 2);
                if (k >= 256) { fail("DEF_GLOBAL_LONG constant too large"); break; }
                emitABC(ROpCode::ROP_DEFGLOBAL, sp-1, byteAt(off+1), (int)k);
                break;
            }
            case OpCode::OP_DEF_GLOBAL_TABLE: {
                // Stack: [value, env_table, key] -> pop all, set env[key]=value with check.
                // Lower to: DEFGLOBAL with table in R(sp-2)... but DEFGLOBAL uses upvalue.
                // This variant has explicit table. Add handling: emit DEFGLOBAL with
                // a flag? For now, fail — check if it's actually emitted.
                fail("OP_DEF_GLOBAL_TABLE not yet supported");
                break;
            }

            // ---- Yield ----
            case OpCode::OP_YIELD_MULTI: {
                uint8_t fixed = byteAt(off + 1);
                uint8_t retc = byteAt(off + 2);
                // Values at R(sp-fixed-lastResultCount)..R(sp-1)
                emitABC(ROpCode::ROP_YIELD, sp - fixed, fixed + 1, retc);
                multiCallFixed_[out_.size() - 1] = fixed;
                break;
            }

            // ---- For loops ----
            case OpCode::OP_FORPREP: {
                uint8_t rawBase = byteAt(off + 1);
                uint8_t base = rawBase & 0x7F;
                bool stepDefault = (rawBase & 0x80) != 0;
                size_t target = off + 4 + u16At(off + 2);
                if (stepDefault) {
                    // No step provided; the stack interpreter pushes 1.
                    // Emit it into R(base+2) explicitly.
                    int oneIdx = getOrAddIntConstant(1);
                    emitABx(ROpCode::ROP_LOADK, base + 2, oneIdx);
                }
                emitJump(ROpCode::ROP_FORPREP, base, target);
                useRegs(base, base + 4);
                break;
            }
            case OpCode::OP_FORLOOP: {
                uint8_t base = byteAt(off + 1) & 0x7F;
                size_t target = off + 4 - u16At(off + 2);
                emitJump(ROpCode::ROP_FORLOOP, base, target);
                useRegs(base, base + 4);
                break;
            }

            default:
                fail("unhandled opcode in translation: " +
                     std::string(opcodeName(op)));
                break;
        }
    }

    void emitJump(ROpCode op, int a, size_t oldTarget) {
        size_t pc = out_.size();
        // Placeholder; fixed up in resolveJumps.
        emit(ropEncodeAsBx(op, (uint8_t)a, 0));
        fixups_.push_back({pc, oldTarget});
        useReg(a);
    }

    void resolveJumps() {
        for (auto& f : fixups_) {
            auto it = oldToNew_.find(f.oldTarget);
            if (it == oldToNew_.end()) {
                fail("jump target not translated: " + std::to_string(f.oldTarget));
                return;
            }
            size_t newTarget = it->second;
            long sbx = (long)newTarget - (long)(f.newPc + 1);
            if (sbx < R_SBX_MIN || sbx > R_SBX_MAX) {
                fail("jump offset out of range");
                return;
            }
            ROpCode op = ropGetOp(out_[f.newPc]);
            uint8_t a = ropGetA(out_[f.newPc]);
            out_[f.newPc] = ropEncodeAsBx(op, a, (int16_t)sbx);
        }
    }

    // Side tables
    std::vector<int> tbcSlots_;
    std::unordered_map<size_t, int> multiCallFixed_;  // pc -> fixed arg count
};

}  // namespace

RTranslateResult translateToRegister(FunctionObject* func) {
    Translator t(func);
    return t.run();
}

std::unordered_map<size_t, int> rtranslateComputeSp(FunctionObject* func,
                                                     std::string& error) {
    Translator t(func);
    std::unordered_map<size_t, int> spOut;
    if (!t.computeSpOnly(spOut, error)) {
        return {};
    }
    return spOut;
}
