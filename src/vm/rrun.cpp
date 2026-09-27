// Register VM interpreter (Phase 3 of register-vm migration).
//
// Executes 32-bit register bytecode (see src/vm/rinstruction.hpp).
// The register file is a window into the coroutine value stack:
//   Value* R = &coroutine->stack[frame.stackBase];

#include "vm/vm.hpp"
#include "vm/rinstruction.hpp"
#include "value/function.hpp"
#include "value/value.hpp"
#include "compiler/chunk.hpp"
#include <cstdint>

bool VM::runRegister(size_t targetFrameCount) {
    while (true) {
        if (currentCoroutine_->status == CoroutineObject::Status::SUSPENDED) {
            return true;
        }
        if (currentCoroutine_->frames.size() <= targetFrameCount) {
            return !hadError_;
        }

        if (interrupted_) {
            interrupted_ = 0;
            // TODO: handle interruption
        }

        CallFrame& frame = currentCoroutine_->frames.back();
        FunctionObject* func = frame.closure->function();
        Chunk* chunk = func->chunk();
        
        if (!chunk->hasRCode()) {
            // No register code; fall back to stack VM (should not happen in register mode)
            hadError_ = true;
            return false;
        }
        
        const std::vector<uint32_t>& code = chunk->rcode();
        const std::vector<Value>& constants = chunk->constants();
        
        // Register window: R[i] = stack[frame.stackBase + i]
        // Ensure the stack has enough space for the registers
        size_t base = frame.stackBase;
        // The translator computes maxRegisters; we need to ensure stack size
        // For now, assume the frame was set up with enough space
        
        Value* R = currentCoroutine_->stack.data() + base;
        size_t pc = 0;
        
        // TODO: Get frame size from translator result (maxRegisters)
        // For now, use a large enough value
        
        while (pc < code.size()) {
            uint32_t instr = code[pc++];
            ROpCode op = ropGetOp(instr);
            uint8_t A = ropGetA(instr);
            uint8_t B = ropGetB(instr);
            uint8_t C = ropGetC(instr);
            uint16_t Bx = ropGetBx(instr);
            int16_t sBx = ropGetSBx(instr);
            
            switch (op) {
                case ROpCode::ROP_LOADK: {
                    // R(A) = K[Bx]
                    R[A] = constants[Bx];
                    break;
                }
                case ROpCode::ROP_LOADNIL: {
                    // R(A)..R(A+B) = nil
                    for (int i = 0; i <= B; i++) {
                        R[A + i] = Value::nil();
                    }
                    break;
                }
                case ROpCode::ROP_LOADBOOL: {
                    // R(A) = (B != 0); if (C) pc++
                    R[A] = Value::boolean(B != 0);
                    if (C) pc++;
                    break;
                }
                case ROpCode::ROP_MOVE: {
                    // R(A) = R(B)
                    R[A] = R[B];
                    break;
                }
                case ROpCode::ROP_ADD: {
                    // R(A) = R(B) + R(C)
                    // TODO: Use the existing arithmetic helpers with fast paths
                    // For now, simple implementation
                    Value b = R[B];
                    Value c = R[C];
                    if (b.isNumber() && c.isNumber()) {
                        R[A] = Value::number(b.asNumber() + c.asNumber());
                    } else {
                        // TODO: call metamethod
                        hadError_ = true;
                        return false;
                    }
                    break;
                }
                case ROpCode::ROP_SUB: {
                    Value b = R[B];
                    Value c = R[C];
                    if (b.isNumber() && c.isNumber()) {
                        R[A] = Value::number(b.asNumber() - c.asNumber());
                    } else {
                        hadError_ = true;
                        return false;
                    }
                    break;
                }
                case ROpCode::ROP_MUL: {
                    Value b = R[B];
                    Value c = R[C];
                    if (b.isNumber() && c.isNumber()) {
                        R[A] = Value::number(b.asNumber() * c.asNumber());
                    } else {
                        hadError_ = true;
                        return false;
                    }
                    break;
                }
                case ROpCode::ROP_DIV: {
                    Value b = R[B];
                    Value c = R[C];
                    if (b.isNumber() && c.isNumber()) {
                        R[A] = Value::number(b.asNumber() / c.asNumber());
                    } else {
                        hadError_ = true;
                        return false;
                    }
                    break;
                }
                case ROpCode::ROP_JMP: {
                    // pc += sBx
                    pc += sBx;
                    break;
                }
                case ROpCode::ROP_RETURN: {
                    // R(A)..R(A+B-2) are return values (B=1: no values)
                    // TODO: Implement return (pop frame, push results to caller)
                    // For now, just return from VM
                    return !hadError_;
                }
                default: {
                    // Unimplemented opcode
                    hadError_ = true;
                    return false;
                }
            }
        }
        
        // Fell off end without RETURN; treat as return with no values
        return !hadError_;
    }
}
