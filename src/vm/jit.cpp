#include "vm/jit.hpp"
#include "value/closure.hpp"
#include "value/coroutine.hpp"
#include <iostream>
#include <vector>
#include <cstddef>

#ifdef USE_JIT
#ifdef __APPLE__
#include <pthread.h>
#endif

using namespace asmjit;

JitRuntime JITCompiler::rt_;

JITCompiler::JITCompiler(VM* vm) : vm_(vm) { (void)vm_; }
JITCompiler::~JITCompiler() {}

JITFunc JITCompiler::compile(FunctionObject* function) {
    if (function->getJITCode()) return function->getJITCode();

    // Intern string constants in the chunk before compiling
    vm_->internConstants(*function);

    (void)vm_; // Suppress unused warning

    CodeHolder code;
    code.init(rt_.environment());
    a64::Assembler a(&code);

    // Registers (using callee-saved x19-x25 for stable state):
    // X0: VM* vm (input)
    // X19: VM* vm (saved)
    // X20: CoroutineObject* co
    // X21: Value* stack_begin
    // X22: CallFrame* frame
    // X23: Value* local_base (stackBase)
    // X24: Value* top_reg
    // X25: size_t saved_frames_size_bytes
    // X9-X15: Scratch (caller-saved)
    
    a64::Gp vm_reg = a64::x19;
    a64::Gp co_reg = a64::x20;
    a64::Gp stack_reg = a64::x21;
    a64::Gp frame_reg = a64::x22;
    a64::Gp local_reg = a64::x23;
    a64::Gp top_reg = a64::x24;
    a64::Gp frames_size_reg = a64::x25;
    
    a64::Gp scratch = a64::x9;
    a64::Gp scratch2 = a64::x10;

    Label epilogue = a.new_label();
    Label frame_changed = a.new_label();

    // Prologue: Save LR, FP and callee-saved registers
    a.stp(a64::x29, a64::x30, a64::ptr_pre(a64::sp, -96));
    a.mov(a64::x29, a64::sp);
    a.stp(a64::x19, a64::x20, a64::ptr(a64::sp, 16));
    a.stp(a64::x21, a64::x22, a64::ptr(a64::sp, 32));
    a.stp(a64::x23, a64::x24, a64::ptr(a64::sp, 48));
    a.stp(a64::x25, a64::x26, a64::ptr(a64::sp, 64));
    a.stp(a64::x27, a64::x28, a64::ptr(a64::sp, 80));

    a.mov(vm_reg, a64::x0);
    a.ldr(co_reg, a64::ptr(vm_reg, offsetCurrentCoroutine));
    a.ldr(stack_reg, a64::ptr(co_reg, offsetStack)); // co->stack.data()
    a.ldr(top_reg, a64::ptr(co_reg, offsetStack + 8)); // co->stack.top()
    
    // Load frame count (size in bytes)
    a.ldr(scratch, a64::ptr(co_reg, offsetFrames + 8)); // __end_ of frames
    a.ldr(scratch2, a64::ptr(co_reg, offsetFrames)); // __begin_ of frames
    a.sub(frames_size_reg, scratch, scratch2);
    
    // Current frame is frames.back()
    a.sub(frame_reg, scratch, sizeof(CallFrame));

    // Load stackBase and calc local_base
    a.ldr(scratch, a64::ptr(frame_reg, offsetStackBase));
    a.add(local_reg, stack_reg, scratch, a64::lsl(3));

    // Bytecode loop
    Chunk* chunk = function->chunk();
    const std::vector<uint8_t>& bytecode = chunk->code();
    std::vector<Label> labels(bytecode.size() + 1);
    std::vector<bool> boundLabels(bytecode.size() + 1, false);
    for (size_t i = 0; i <= bytecode.size(); i++) {
        labels[i] = a.new_label();
    }

    for (size_t i = 0; i < bytecode.size(); i++) {
        a.bind(labels[i]);
        boundLabels[i] = true;
        
        size_t start_i = i;
        OpCode op = static_cast<OpCode>(bytecode[i]);
        
        switch (op) {
            case OpCode::OP_CONSTANT: {
                uint8_t index = bytecode[++i];
                Value val = chunk->constants()[index];
                a.mov(scratch, val.bits());
                a.str(scratch, a64::ptr(top_reg));
                a.add(top_reg, top_reg, 8);
                break;
            }
            case OpCode::OP_CONSTANT_LONG: {
                uint32_t index = bytecode[++i];
                index |= (bytecode[++i] << 8);
                index |= (bytecode[++i] << 16);
                Value val = chunk->constants()[index];
                a.mov(scratch, val.bits());
                a.str(scratch, a64::ptr(top_reg));
                a.add(top_reg, top_reg, 8);
                break;
            }
            case OpCode::OP_GET_LOCAL: {
                uint8_t slot = bytecode[++i];
                a.ldr(scratch, a64::ptr(local_reg, (uint64_t)slot * 8));
                a.str(scratch, a64::ptr(top_reg));
                a.add(top_reg, top_reg, 8);
                break;
            }
            case OpCode::OP_SET_LOCAL: {
                uint8_t slot = bytecode[++i];
                a.ldr(scratch, a64::ptr(top_reg, -8));
                a.str(scratch, a64::ptr(local_reg, (uint64_t)slot * 8));
                break;
            }
            case OpCode::OP_POP: {
                a.sub(top_reg, top_reg, 8);
                break;
            }
            case OpCode::OP_TRUE: {
                a.mov(scratch, Value::boolean(true).bits());
                a.str(scratch, a64::ptr(top_reg));
                a.add(top_reg, top_reg, 8);
                break;
            }
            case OpCode::OP_FALSE: {
                a.mov(scratch, Value::boolean(false).bits());
                a.str(scratch, a64::ptr(top_reg));
                a.add(top_reg, top_reg, 8);
                break;
            }
            case OpCode::OP_JUMP: {
                uint16_t offset = bytecode[i+1] | (bytecode[i+2] << 8);
                i += 2;
                a.b(labels[i + 1 + offset]);
                break;
            }
            case OpCode::OP_LOOP: {
                uint16_t offset = bytecode[i+1] | (bytecode[i+2] << 8);
                i += 2;
                a.b(labels[i + 1 - offset]);
                break;
            }
            case OpCode::OP_JUMP_IF_FALSE: {
                uint16_t offset = bytecode[i+1] | (bytecode[i+2] << 8);
                size_t next_ip = i + 3;
                i += 2;
                a.ldr(scratch, a64::ptr(top_reg, -8));
                
                // Compare with nil/false
                a.mov(scratch2, Value::nil().bits());
                a.cmp(scratch, scratch2);
                a.b_eq(labels[next_ip + offset]);
                
                a.mov(scratch2, Value::boolean(false).bits());
                a.cmp(scratch, scratch2);
                a.b_eq(labels[next_ip + offset]);
                break;
            }
            case OpCode::OP_NIL: {
                a.mov(scratch, Value::nil().bits());
                a.str(scratch, a64::ptr(top_reg));
                a.add(top_reg, top_reg, 8);
                break;
            }
            case OpCode::OP_GET_GLOBAL: {
                uint8_t index = bytecode[++i];
                a.str(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.mov(a64::x0, vm_reg);
                a.mov(a64::x1, (uint32_t)index);
                a.mov(scratch, (uint64_t)VM::jitGetGlobal);
                a.blr(scratch);
                a.ldr(top_reg, a64::ptr(co_reg, offsetStack + 8));
                break;
            }
            case OpCode::OP_SET_GLOBAL: {
                uint8_t index = bytecode[++i];
                a.str(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.mov(a64::x0, vm_reg);
                a.mov(a64::x1, (uint32_t)index);
                a.mov(scratch, (uint64_t)VM::jitSetGlobal);
                a.blr(scratch);
                a.ldr(top_reg, a64::ptr(co_reg, offsetStack + 8));
                break;
            }
            case OpCode::OP_GET_TABUP: {
                uint8_t upIndex = bytecode[++i];
                uint8_t keyIndex = bytecode[++i];
                a.str(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.mov(a64::x0, vm_reg);
                a.mov(a64::x1, (uint32_t)upIndex);
                a.mov(a64::x2, (uint32_t)keyIndex);
                a.mov(a64::x3, (uint32_t)(i + 1));
                a.mov(scratch, (uint64_t)VM::jitGetTabUp);
                a.blr(scratch);
                a.ldr(top_reg, a64::ptr(co_reg, offsetStack + 8));
                
                a.ldr(scratch, a64::ptr(co_reg, offsetFrames + 8));
                a.ldr(scratch2, a64::ptr(co_reg, offsetFrames));
                a.sub(scratch, scratch, scratch2);
                a.cmp(scratch, frames_size_reg);
                a.b_ne(frame_changed); 

                a.ldr(scratch, a64::ptr(co_reg, offsetFrames + 8));
                a.sub(frame_reg, scratch, sizeof(CallFrame));
                a.ldr(scratch, a64::ptr(frame_reg, offsetStackBase));
                a.add(local_reg, stack_reg, scratch, a64::lsl(3));
                break;
            }
            case OpCode::OP_SET_TABUP: {
                uint8_t upIndex = bytecode[++i];
                uint8_t keyIndex = bytecode[++i];
                a.str(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.mov(a64::x0, vm_reg);
                a.mov(a64::x1, (uint32_t)upIndex);
                a.mov(a64::x2, (uint32_t)keyIndex);
                a.mov(a64::x3, (uint32_t)(i + 1));
                a.mov(scratch, (uint64_t)VM::jitSetTabUp);
                a.blr(scratch);
                a.ldr(top_reg, a64::ptr(co_reg, offsetStack + 8));
                
                a.ldr(scratch, a64::ptr(co_reg, offsetFrames + 8));
                a.ldr(scratch2, a64::ptr(co_reg, offsetFrames));
                a.sub(scratch, scratch, scratch2);
                a.cmp(scratch, frames_size_reg);
                a.b_ne(frame_changed); 

                a.ldr(scratch, a64::ptr(co_reg, offsetFrames + 8));
                a.sub(frame_reg, scratch, sizeof(CallFrame));
                a.ldr(scratch, a64::ptr(frame_reg, offsetStackBase));
                a.add(local_reg, stack_reg, scratch, a64::lsl(3));
                break;
            }
            case OpCode::OP_LEN: {
                a.str(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.mov(a64::x0, vm_reg);
                a.mov(a64::x1, (uint32_t)(i + 1));
                a.mov(scratch, (uint64_t)VM::jitLen);
                a.blr(scratch);
                a.ldr(top_reg, a64::ptr(co_reg, offsetStack + 8));
                
                a.ldr(scratch, a64::ptr(co_reg, offsetFrames + 8));
                a.ldr(scratch2, a64::ptr(co_reg, offsetFrames));
                a.sub(scratch, scratch, scratch2);
                a.cmp(scratch, frames_size_reg);
                a.b_ne(frame_changed); 

                a.ldr(scratch, a64::ptr(co_reg, offsetFrames + 8));
                a.sub(frame_reg, scratch, sizeof(CallFrame));
                a.ldr(scratch, a64::ptr(frame_reg, offsetStackBase));
                a.add(local_reg, stack_reg, scratch, a64::lsl(3));
                break;
            }
            case OpCode::OP_NEW_TABLE: {
                a.str(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.mov(a64::x0, vm_reg);
                a.mov(scratch, (uint64_t)VM::jitNewTable);
                a.blr(scratch);
                a.ldr(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.ldr(stack_reg, a64::ptr(co_reg, offsetStack));
                a.ldr(scratch, a64::ptr(co_reg, offsetFrames + 8));
                a.sub(frame_reg, scratch, sizeof(CallFrame));
                a.ldr(scratch, a64::ptr(frame_reg, offsetStackBase));
                a.add(local_reg, stack_reg, scratch, a64::lsl(3));
                break;
            }
            case OpCode::OP_GET_TABLE: {
                a.str(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.mov(a64::x0, vm_reg);
                a.mov(a64::x1, (uint32_t)(i + 1));
                a.mov(scratch, (uint64_t)VM::jitGetTable);
                a.blr(scratch);
                a.ldr(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.ldr(stack_reg, a64::ptr(co_reg, offsetStack));
                
                a.ldr(scratch, a64::ptr(co_reg, offsetFrames + 8));
                a.ldr(scratch2, a64::ptr(co_reg, offsetFrames));
                a.sub(scratch, scratch, scratch2);
                a.cmp(scratch, frames_size_reg);
                a.b_ne(frame_changed); 

                a.ldr(scratch, a64::ptr(co_reg, offsetFrames + 8));
                a.sub(frame_reg, scratch, sizeof(CallFrame));
                a.ldr(scratch, a64::ptr(frame_reg, offsetStackBase));
                a.add(local_reg, stack_reg, scratch, a64::lsl(3));
                break;
            }
            case OpCode::OP_SET_TABLE: {
                a.str(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.mov(a64::x0, vm_reg);
                a.mov(a64::x1, (uint32_t)(i + 1));
                a.mov(scratch, (uint64_t)VM::jitSetTable);
                a.blr(scratch);
                a.ldr(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.ldr(stack_reg, a64::ptr(co_reg, offsetStack));
                
                a.ldr(scratch, a64::ptr(co_reg, offsetFrames + 8));
                a.ldr(scratch2, a64::ptr(co_reg, offsetFrames));
                a.sub(scratch, scratch, scratch2);
                a.cmp(scratch, frames_size_reg);
                a.b_ne(frame_changed); 

                a.ldr(scratch, a64::ptr(co_reg, offsetFrames + 8));
                a.sub(frame_reg, scratch, sizeof(CallFrame));
                a.ldr(scratch, a64::ptr(frame_reg, offsetStackBase));
                a.add(local_reg, stack_reg, scratch, a64::lsl(3));
                break;
            }
            case OpCode::OP_GET_UPVALUE: {
                uint8_t slot = bytecode[++i];
                a.str(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.mov(a64::x0, vm_reg);
                a.mov(a64::x1, (uint32_t)slot);
                a.mov(scratch, (uint64_t)VM::jitGetUpvalue);
                a.blr(scratch);
                a.ldr(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.ldr(stack_reg, a64::ptr(co_reg, offsetStack));
                a.ldr(scratch, a64::ptr(co_reg, offsetFrames + 8));
                a.sub(frame_reg, scratch, sizeof(CallFrame));
                a.ldr(scratch, a64::ptr(frame_reg, offsetStackBase));
                a.add(local_reg, stack_reg, scratch, a64::lsl(3));
                break;
            }
            case OpCode::OP_SET_UPVALUE: {
                uint8_t slot = bytecode[++i];
                a.str(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.mov(a64::x0, vm_reg);
                a.mov(a64::x1, (uint32_t)slot);
                a.mov(scratch, (uint64_t)VM::jitSetUpvalue);
                a.blr(scratch);
                a.ldr(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.ldr(stack_reg, a64::ptr(co_reg, offsetStack));
                a.ldr(scratch, a64::ptr(co_reg, offsetFrames + 8));
                a.sub(frame_reg, scratch, sizeof(CallFrame));
                a.ldr(scratch, a64::ptr(frame_reg, offsetStackBase));
                a.add(local_reg, stack_reg, scratch, a64::lsl(3));
                break;
            }
            case OpCode::OP_CLOSE_UPVALUE: {
                // We need the absolute stack index
                a.sub(scratch, top_reg, stack_reg);
                a.lsr(scratch, scratch, 3); // scratch = index
                a.sub(scratch, scratch, 1);

                a.str(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.mov(a64::x0, vm_reg);
                a.mov(a64::x1, scratch);
                a.mov(scratch, (uint64_t)VM::jitCloseUpvalues);
                a.blr(scratch);
                a.ldr(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.ldr(stack_reg, a64::ptr(co_reg, offsetStack));
                a.ldr(scratch, a64::ptr(co_reg, offsetFrames + 8));
                a.sub(frame_reg, scratch, sizeof(CallFrame));
                a.ldr(scratch, a64::ptr(frame_reg, offsetStackBase));
                a.add(local_reg, stack_reg, scratch, a64::lsl(3));
                a.sub(top_reg, top_reg, 8); // pop
                break;
            }
            case OpCode::OP_CONCAT: {
                a.str(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.mov(a64::x0, vm_reg);
                a.mov(a64::x1, (uint32_t)(i + 1));
                a.mov(scratch, (uint64_t)VM::jitConcat);
                a.blr(scratch);
                a.ldr(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.ldr(stack_reg, a64::ptr(co_reg, offsetStack));
                
                a.ldr(scratch, a64::ptr(co_reg, offsetFrames + 8));
                a.ldr(scratch2, a64::ptr(co_reg, offsetFrames));
                a.sub(scratch, scratch, scratch2);
                a.cmp(scratch, frames_size_reg);
                a.b_ne(frame_changed); 

                a.ldr(scratch, a64::ptr(co_reg, offsetFrames + 8));
                a.sub(frame_reg, scratch, sizeof(CallFrame));
                a.ldr(scratch, a64::ptr(frame_reg, offsetStackBase));
                a.add(local_reg, stack_reg, scratch, a64::lsl(3));
                break;
            }
            case OpCode::OP_CLOSURE: {
                uint8_t constantIndex = bytecode[++i];
                size_t bytecodeOffset = i + 1;
                
                // Skip the upvalue capture bytes
                Value funcValue = chunk->constants()[constantIndex];
                FunctionObject* innerFunc = chunk->getFunction(funcValue.asFunctionIndex());
                i += 2 * innerFunc->upvalueCount();

                a.str(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.mov(a64::x0, vm_reg);
                a.mov(a64::x1, (uint32_t)constantIndex);
                a.mov(a64::x2, (uint32_t)bytecodeOffset);
                a.mov(scratch, (uint64_t)VM::jitClosure);
                a.blr(scratch);
                a.ldr(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.ldr(stack_reg, a64::ptr(co_reg, offsetStack));
                a.ldr(scratch, a64::ptr(co_reg, offsetFrames + 8));
                a.sub(frame_reg, scratch, sizeof(CallFrame));
                a.ldr(scratch, a64::ptr(frame_reg, offsetStackBase));
                a.add(local_reg, stack_reg, scratch, a64::lsl(3));
                break;
            }
            case OpCode::OP_CALL: {
                uint8_t argCount = bytecode[++i];
                uint8_t retCount = bytecode[++i];

                a.str(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.mov(a64::x0, vm_reg);
                a.mov(a64::x1, (uint32_t)argCount);
                a.mov(a64::x2, (uint32_t)retCount);
                a.mov(a64::x3, (uint32_t)(i + 1));
                a.mov(scratch, (uint64_t)VM::jitCall);
                a.blr(scratch);
                a.ldr(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.ldr(stack_reg, a64::ptr(co_reg, offsetStack));
                
                // Fall back if a new frame was added (Lua call or metamethod)
                a.ldr(scratch, a64::ptr(co_reg, offsetFrames + 8));
                a.ldr(scratch2, a64::ptr(co_reg, offsetFrames));
                a.sub(scratch, scratch, scratch2);
                a.cmp(scratch, frames_size_reg);
                a.b_ne(frame_changed); 

                a.ldr(scratch, a64::ptr(co_reg, offsetFrames + 8));
                a.sub(frame_reg, scratch, sizeof(CallFrame));
                a.ldr(scratch, a64::ptr(frame_reg, offsetStackBase));
                a.add(local_reg, stack_reg, scratch, a64::lsl(3));
                break;
            }
            case OpCode::OP_BAND:
            case OpCode::OP_BOR:
            case OpCode::OP_BXOR:
            case OpCode::OP_SHL:
            case OpCode::OP_SHR: {
                a.str(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.mov(a64::x0, vm_reg);
                a.mov(a64::x1, (uint32_t)(i + 1));
                if (op == OpCode::OP_BAND) a.mov(scratch, (uint64_t)VM::jitBand);
                else if (op == OpCode::OP_BOR) a.mov(scratch, (uint64_t)VM::jitBor);
                else if (op == OpCode::OP_BXOR) a.mov(scratch, (uint64_t)VM::jitBxor);
                else if (op == OpCode::OP_SHL) a.mov(scratch, (uint64_t)VM::jitShl);
                else if (op == OpCode::OP_SHR) a.mov(scratch, (uint64_t)VM::jitShr);
                a.blr(scratch);
                a.ldr(top_reg, a64::ptr(co_reg, offsetStack + 8));
                break;
            }
            case OpCode::OP_BNOT: {
                a.str(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.mov(a64::x0, vm_reg);
                a.mov(a64::x1, (uint32_t)(i + 1));
                a.mov(scratch, (uint64_t)VM::jitBnot);
                a.blr(scratch);
                a.ldr(top_reg, a64::ptr(co_reg, offsetStack + 8));
                break;
            }
            case OpCode::OP_IDIV:
            case OpCode::OP_MOD:
            case OpCode::OP_POW: {
                a.str(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.mov(a64::x0, vm_reg);
                a.mov(a64::x1, (uint32_t)(i + 1));
                if (op == OpCode::OP_IDIV) a.mov(scratch, (uint64_t)VM::jitIDiv);
                else if (op == OpCode::OP_MOD) a.mov(scratch, (uint64_t)VM::jitMod);
                else if (op == OpCode::OP_POW) a.mov(scratch, (uint64_t)VM::jitPow);
                a.blr(scratch);
                a.ldr(top_reg, a64::ptr(co_reg, offsetStack + 8));
                
                a.ldr(scratch, a64::ptr(co_reg, offsetFrames + 8));
                a.ldr(scratch2, a64::ptr(co_reg, offsetFrames));
                a.sub(scratch, scratch, scratch2);
                a.cmp(scratch, frames_size_reg);
                a.b_ne(frame_changed); 

                a.ldr(scratch, a64::ptr(co_reg, offsetFrames + 8));
                a.sub(frame_reg, scratch, sizeof(CallFrame));
                a.ldr(scratch, a64::ptr(frame_reg, offsetStackBase));
                a.add(local_reg, stack_reg, scratch, a64::lsl(3));
                break;
            }
            case OpCode::OP_ADD:
            case OpCode::OP_SUB:
            case OpCode::OP_MUL:
            case OpCode::OP_DIV: {
                Label fallback = a.new_label();
                Label success = a.new_label();

                a.ldr(scratch, a64::ptr(top_reg, -8)); // val2
                a.ldr(scratch2, a64::ptr(top_reg, -16)); // val1
                
                a64::Gp tag = a64::x11;
                a.lsr(tag, scratch, 48);
                a.mov(a64::x12, 0xFFF1);
                a.cmp(tag, a64::x12);
                a.b_hs(fallback);

                a.lsr(tag, scratch2, 48);
                a.cmp(tag, a64::x12);
                a.b_hs(fallback);

                a.fmov(a64::d1, scratch); // d1 = val2
                a.fmov(a64::d0, scratch2); // d0 = val1
                
                if (op == OpCode::OP_ADD) a.fadd(a64::d0, a64::d0, a64::d1);
                else if (op == OpCode::OP_SUB) a.fsub(a64::d0, a64::d0, a64::d1);
                else if (op == OpCode::OP_MUL) a.fmul(a64::d0, a64::d0, a64::d1);
                else if (op == OpCode::OP_DIV) a.fdiv(a64::d0, a64::d0, a64::d1);
                
                a.fmov(scratch, a64::d0);
                a.str(scratch, a64::ptr(top_reg, -16));
                a.sub(top_reg, top_reg, 8);
                a.b(success);

                a.bind(fallback);
                a.str(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.mov(a64::x0, (uint64_t)start_i);
                a.b(epilogue);

                a.bind(success);
                break;
            }
            case OpCode::OP_NEG: {
                Label fallback = a.new_label();
                Label success = a.new_label();

                a.ldr(scratch, a64::ptr(top_reg, -8)); // val1
                a64::Gp tag = a64::x11;
                a.lsr(tag, scratch, 48);
                a.mov(a64::x12, 0xFFF1);
                a.cmp(tag, a64::x12);
                a.b_hs(fallback);

                a.fmov(a64::d0, scratch);
                a.fneg(a64::d0, a64::d0);
                a.fmov(scratch, a64::d0);
                a.str(scratch, a64::ptr(top_reg, -8));
                a.b(success);

                a.bind(fallback);
                a.str(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.mov(a64::x0, (uint64_t)start_i);
                a.b(epilogue);

                a.bind(success);
                break;
            }
            case OpCode::OP_NOT: {
                Label is_falsey = a.new_label();
                Label success = a.new_label();

                a.ldr(scratch, a64::ptr(top_reg, -8));
                a.mov(scratch2, Value::nil().bits());
                a.cmp(scratch, scratch2);
                a.b_eq(is_falsey);
                a.mov(scratch2, Value::boolean(false).bits());
                a.cmp(scratch, scratch2);
                a.b_eq(is_falsey);

                a.mov(scratch, Value::boolean(false).bits());
                a.str(scratch, a64::ptr(top_reg, -8));
                a.b(success);

                a.bind(is_falsey);
                a.mov(scratch, Value::boolean(true).bits());
                a.str(scratch, a64::ptr(top_reg, -8));
                a.bind(success);
                break;
            }
            case OpCode::OP_EQUAL:
            case OpCode::OP_LESS:
            case OpCode::OP_LESS_EQUAL:
            case OpCode::OP_GREATER:
            case OpCode::OP_GREATER_EQUAL: {
                Label fallback = a.new_label();
                Label is_true = a.new_label();
                Label success = a.new_label();

                a.ldr(scratch, a64::ptr(top_reg, -8)); // val2
                a.ldr(scratch2, a64::ptr(top_reg, -16)); // val1
                
                a64::Gp tag = a64::x11;
                a.lsr(tag, scratch, 48);
                a.mov(a64::x12, 0xFFF1);
                a.cmp(tag, a64::x12);
                a.b_hs(fallback);
                a.lsr(tag, scratch2, 48);
                a.cmp(tag, a64::x12);
                a.b_hs(fallback);

                a.fmov(a64::d1, scratch);
                a.fmov(a64::d0, scratch2);
                a.fcmp(a64::d0, a64::d1);
                
                if (op == OpCode::OP_EQUAL) a.b_eq(is_true);
                else if (op == OpCode::OP_LESS) a.b_mi(is_true);
                else if (op == OpCode::OP_LESS_EQUAL) a.b_ls(is_true);
                else if (op == OpCode::OP_GREATER) a.b_gt(is_true);
                else if (op == OpCode::OP_GREATER_EQUAL) a.b_ge(is_true);
                
                a.mov(scratch, Value::boolean(false).bits());
                a.str(scratch, a64::ptr(top_reg, -16));
                a.sub(top_reg, top_reg, 8);
                a.b(success);

                a.bind(is_true);
                a.mov(scratch, Value::boolean(true).bits());
                a.str(scratch, a64::ptr(top_reg, -16));
                a.sub(top_reg, top_reg, 8);
                a.b(success);

                a.bind(fallback);
                a.str(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.mov(a64::x0, (uint64_t)start_i);
                a.b(epilogue);

                a.bind(success);
                break;
            }
            case OpCode::OP_RETURN: {
                a.str(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.mov(a64::x0, (uint64_t)start_i);
                a.b(epilogue);
                break;
            }
            case OpCode::OP_RETURN_VALUE: {
                uint8_t count = bytecode[++i];
                (void)count;
                a.str(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.mov(a64::x0, (uint64_t)start_i);
                a.b(epilogue);
                break;
            }
            case OpCode::OP_RETURN_VALUE_MULTI: {
                uint8_t count = bytecode[++i];
                (void)count;
                a.str(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.mov(a64::x0, (uint64_t)start_i);
                a.b(epilogue);
                break;
            }
            default: {
                // Unsupported opcode, fall back to interpreter
                a.str(top_reg, a64::ptr(co_reg, offsetStack + 8));
                if (start_i == 0) {
                    a.mov(a64::x0, -1);
                } else {
                    a.mov(a64::x0, (uint64_t)start_i); // Return current IP to the interpreter
                }
                a.b(epilogue);
                goto compilation_done;
            }
        }
    }

compilation_done:
    // Bind any remaining labels to prevent asmjit errors
    for (size_t j = 0; j < labels.size(); j++) {
        if (!boundLabels[j]) {
            a.bind(labels[j]);
        }
    }

    a.str(top_reg, a64::ptr(co_reg, offsetStack + 8));
    a.mov(a64::x0, -1);
    a.b(epilogue);

    a.bind(frame_changed);
    a.str(top_reg, a64::ptr(co_reg, offsetStack + 8));
    a.mov(a64::x0, -2);
    
    a.bind(epilogue);
    // FULL ARM64 CALLEE-SAVED RESTORE
    a.ldp(a64::x27, a64::x28, a64::ptr(a64::sp, 80));
    a.ldp(a64::x25, a64::x26, a64::ptr(a64::sp, 64));
    a.ldp(a64::x23, a64::x24, a64::ptr(a64::sp, 48));
    a.ldp(a64::x21, a64::x22, a64::ptr(a64::sp, 32));
    a.ldp(a64::x19, a64::x20, a64::ptr(a64::sp, 16));
    a.ldp(a64::x29, a64::x30, a64::ptr_post(a64::sp, 96));
    
    a.ret(a64::x30);

    JITFunc fn;
#ifdef __APPLE__
    pthread_jit_write_protect_np(0);
#endif
    Error err = rt_.add(&fn, &code);
#ifdef __APPLE__
    pthread_jit_write_protect_np(1);
    __builtin___clear_cache((char*)fn, (char*)fn + code.code_size());
#endif
    if (err != kErrorOk) {
        return nullptr;
    }

    function->setJITCode(fn);
    return fn;
}

#endif
