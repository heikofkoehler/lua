#include "vm/jit.hpp"
#include "value/closure.hpp"
#include "value/coroutine.hpp"
#include "value/table.hpp"
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

    // Debug offsets
    size_t offsetCurrentCoroutine = (size_t)&(((VM*)0)->currentCoroutine_);
    size_t offsetStack = (size_t)&(((CoroutineObject*)0)->stack);
    size_t offsetFrames = (size_t)&(((CoroutineObject*)0)->frames);
    size_t offsetStackBase = (size_t)&(((CallFrame*)0)->stackBase);
    size_t offsetIp = (size_t)&(((CallFrame*)0)->ip);
    size_t offsetHadError = (size_t)&(((VM*)0)->hadError_);
    size_t offsetInterrupted = (size_t)&(((VM*)0)->interrupted_);
    size_t offsetOpenUpvalues = (size_t)&(((CoroutineObject*)0)->openUpvalues);
    size_t offsetArray = (size_t)&(((TableObject*)0)->array_);
    size_t offsetMetatable = (size_t)&(((TableObject*)0)->metatable_);
    size_t offsetLastLen = (size_t)&(((TableObject*)0)->lastLen_);

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
    a64::Gp scratch_w = a64::w(scratch.id());
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
    
    // Ensure stack has enough headroom (at least 2048 slots = 16384 bytes)
    Label stack_ok = a.new_label();
    a.ldr(scratch, a64::ptr(co_reg, offsetStack + 16)); // co->stack.__end_cap_
    a.sub(scratch, scratch, top_reg);
    a.mov(scratch2, 2048 * 8);
    a.cmp(scratch, scratch2);
    a.b_hs(stack_ok);
    a.str(top_reg, a64::ptr(co_reg, offsetStack + 8));
    a.mov(a64::x0, vm_reg);
    a.mov(a64::x1, 2048);
    a.mov(scratch, (uint64_t)VM::jitEnsureStack);
    a.blr(scratch);
    a.ldr(stack_reg, a64::ptr(co_reg, offsetStack));
    a.ldr(top_reg, a64::ptr(co_reg, offsetStack + 8));
    a.bind(stack_ok);

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
                a.sub(top_reg, top_reg, 8);
                a.ldr(scratch, a64::ptr(top_reg));
                a.str(scratch, a64::ptr(local_reg, (uint64_t)slot * 8));
                break;
            }
            case OpCode::OP_POP: {
                a.sub(top_reg, top_reg, 8);
                break;
            }
            case OpCode::OP_DUP: {
                a.ldr(scratch, a64::ptr(top_reg, -8));
                a.str(scratch, a64::ptr(top_reg));
                a.add(top_reg, top_reg, 8);
                break;
            }
            case OpCode::OP_SWAP: {
                a.ldr(scratch, a64::ptr(top_reg, -8));
                a.ldr(scratch2, a64::ptr(top_reg, -16));
                a.str(scratch, a64::ptr(top_reg, -16));
                a.str(scratch2, a64::ptr(top_reg, -8));
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
                size_t loop_dest = i + 1 - offset;
                a.ldr(scratch_w, a64::ptr(vm_reg, offsetInterrupted));
                a.mov(scratch2, loop_dest);
                a.str(scratch2, a64::ptr(frame_reg, offsetIp));
                a.cbnz(scratch_w, frame_changed);
                a.b(labels[loop_dest]);
                break;
            }
            case OpCode::OP_FORPREP: {
                uint8_t rawBase = bytecode[++i];
                uint8_t base = rawBase & 0x7F;
                uint16_t offset = bytecode[i+1] | (bytecode[i+2] << 8);
                i += 2;

                a.str(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.mov(a64::x0, vm_reg);
                a.mov(a64::x1, (uint32_t)rawBase);
                a.mov(a64::x2, (uint32_t)offset);
                a.mov(scratch, (uint64_t)VM::jitForPrep);
                a.blr(scratch);
                // Reload state after C++ call
                a.ldr(stack_reg, a64::ptr(co_reg, offsetStack));
                a.ldr(scratch, a64::ptr(co_reg, offsetFrames + 8));
                a.sub(frame_reg, scratch, sizeof(CallFrame));
                a.ldr(scratch, a64::ptr(frame_reg, offsetStackBase));
                a.add(local_reg, stack_reg, scratch, a64::lsl(3));
                a.ldrb(scratch_w, a64::ptr(vm_reg, offsetHadError));
                a.cbnz(scratch_w, frame_changed);

                // If skipLoop returned true (w0 != 0), branch forward to labels[i + 1 + offset]
                Label loop_start = a.new_label();
                a.cbz(a64::w0, loop_start);
                // skipLoop: top_reg = local_reg + base * 8
                a.add(top_reg, local_reg, (uint64_t)base * 8);
                a.b(labels[i + 1 + offset]);

                a.bind(loop_start);
                // Loop starts: top_reg = local_reg + (base + 4) * 8
                a.add(top_reg, local_reg, (uint64_t)(base + 4) * 8);
                break;
            }
            case OpCode::OP_FORLOOP: {
                uint8_t base = bytecode[++i];
                uint16_t offset = bytecode[i+1] | (bytecode[i+2] << 8);
                i += 2;
                size_t loop_dest = i + 1 - offset;

                Label fallback = a.new_label();
                Label loop_cont = a.new_label();
                Label loop_end = a.new_label();
                Label step_neg = a.new_label();
                Label try_float = a.new_label();
                Label step_neg_f = a.new_label();

                // 1. Check interrupted flag (for debug hooks / signals)
                a.ldr(scratch_w, a64::ptr(vm_reg, offsetInterrupted));
                a.cbnz(scratch_w, frame_changed);

                // 2. Check if openUpvalues is empty: __begin_ == __end_
                a.ldr(scratch, a64::ptr(co_reg, offsetOpenUpvalues));
                a.ldr(scratch2, a64::ptr(co_reg, offsetOpenUpvalues + 8));
                a.cmp(scratch, scratch2);
                a.b_ne(fallback);

                // 3. Load v_init, v_limit, v_step
                a64::Gp reg_init = a64::x11;
                a64::Gp reg_limit = a64::x12;
                a64::Gp reg_step = a64::x13;
                a64::Gp reg_tag = a64::x14;

                a.ldr(reg_init, a64::ptr(local_reg, (uint64_t)base * 8));
                a.ldr(reg_limit, a64::ptr(local_reg, (uint64_t)(base + 1) * 8));
                a.ldr(reg_step, a64::ptr(local_reg, (uint64_t)(base + 2) * 8));

                // Check if all are integer: tag == 0xFFF3
                a.lsr(reg_tag, reg_init, 48);
                a.mov(scratch, 0xFFF3);
                a.cmp(reg_tag, scratch);
                a.b_ne(try_float);

                a.lsr(reg_tag, reg_step, 48);
                a.cmp(reg_tag, scratch);
                a.b_ne(try_float);

                a.lsr(reg_tag, reg_limit, 48);
                a.cmp(reg_tag, scratch);
                a.b_ne(fallback); // mixed int/float limit or big int -> fallback

                // All 3 are 48-bit inline integers!
                a.sbfx(reg_init, reg_init, 0, 48);
                a.sbfx(reg_step, reg_step, 0, 48);
                a.sbfx(reg_limit, reg_limit, 0, 48);

                // nextI = init + step with overflow check
                a64::Gp reg_next = a64::x15;
                a.adds(reg_next, reg_init, reg_step);
                a.b_vs(fallback); // 64-bit overflow -> fallback

                // Check 48-bit range for nextI: sbfx check, nextI must fit in 48 bits
                a.sbfx(scratch, reg_next, 0, 48);
                a.cmp(scratch, reg_next);
                a.b_ne(fallback);

                // Step direction check
                a.cmp(reg_step, 0);
                a.b_le(step_neg);

                // step > 0: nextI <= limitI
                a.cmp(reg_next, reg_limit);
                a.b_gt(loop_end);
                a.b(loop_cont);

                a.bind(step_neg);
                // step <= 0: nextI >= limitI
                a.cmp(reg_next, reg_limit);
                a.b_lt(loop_end);

                a.bind(loop_cont);
                // Pack nextI into 48-bit tagged integer Value
                a.mov(scratch, 0xFFF3);
                a.bfi(reg_next, scratch, 48, 16);
                a.str(reg_next, a64::ptr(local_reg, (uint64_t)base * 8));
                a.str(reg_next, a64::ptr(local_reg, (uint64_t)(base + 3) * 8));
                a.mov(scratch, (uint64_t)loop_dest);
                a.str(scratch, a64::ptr(frame_reg, offsetIp));
                a.b(labels[loop_dest]);

                // Float path
                a.bind(try_float);
                a.mov(scratch, 0xFFF1);
                a.lsr(reg_tag, reg_init, 48);
                a.cmp(reg_tag, scratch);
                a.b_hs(fallback);

                a.lsr(reg_tag, reg_step, 48);
                a.cmp(reg_tag, scratch);
                a.b_hs(fallback);

                a.lsr(reg_tag, reg_limit, 48);
                a.cmp(reg_tag, scratch);
                a.b_hs(fallback);

                // All 3 are IEEE-754 floats
                a.fmov(a64::d0, reg_init);
                a.fmov(a64::d1, reg_limit);
                a.fmov(a64::d2, reg_step);

                a.fadd(a64::d3, a64::d0, a64::d2); // nextF = initF + stepF
                a.fcmp(a64::d2, 0.0);
                a.b_le(step_neg_f);

                // stepF > 0: nextF <= limitF
                a.fcmp(a64::d3, a64::d1);
                a.b_vs(loop_end); // NaN -> end
                a.b_gt(loop_end);
                a.fmov(reg_next, a64::d3);
                a.str(reg_next, a64::ptr(local_reg, (uint64_t)base * 8));
                a.str(reg_next, a64::ptr(local_reg, (uint64_t)(base + 3) * 8));
                a.mov(scratch, (uint64_t)loop_dest);
                a.str(scratch, a64::ptr(frame_reg, offsetIp));
                a.b(labels[loop_dest]);

                a.bind(step_neg_f);
                // stepF <= 0: nextF >= limitF
                a.fcmp(a64::d3, a64::d1);
                a.b_vs(loop_end);
                a.b_lt(loop_end);
                a.fmov(reg_next, a64::d3);
                a.str(reg_next, a64::ptr(local_reg, (uint64_t)base * 8));
                a.str(reg_next, a64::ptr(local_reg, (uint64_t)(base + 3) * 8));
                a.mov(scratch, (uint64_t)loop_dest);
                a.str(scratch, a64::ptr(frame_reg, offsetIp));
                a.b(labels[loop_dest]);

                // Fallback: call VM::jitForLoopFallback
                a.bind(fallback);
                a.str(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.mov(a64::x0, vm_reg);
                a.mov(a64::x1, (uint32_t)base);
                a.mov(scratch, (uint64_t)VM::jitForLoopFallback);
                a.blr(scratch);
                // Reload state
                a.ldr(stack_reg, a64::ptr(co_reg, offsetStack));
                a.ldr(scratch, a64::ptr(co_reg, offsetFrames + 8));
                a.sub(frame_reg, scratch, sizeof(CallFrame));
                a.ldr(scratch, a64::ptr(frame_reg, offsetStackBase));
                a.add(local_reg, stack_reg, scratch, a64::lsl(3));
                a.ldrb(scratch_w, a64::ptr(vm_reg, offsetHadError));
                a.cbnz(scratch_w, frame_changed);
                // If jitForLoopFallback returned true (canContinue), branch backward
                a.cbnz(a64::w0, labels[loop_dest]);

                a.bind(loop_end);
                // Loop ended: stack resize to actualBase (base * 8)
                a.add(top_reg, local_reg, (uint64_t)base * 8);
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
                a.ldrb(scratch_w, a64::ptr(vm_reg, offsetHadError));
                a.cbnz(scratch, frame_changed);
                a.ldr(stack_reg, a64::ptr(co_reg, offsetStack));
                a.ldr(scratch, a64::ptr(co_reg, offsetFrames + 8));
                a.sub(frame_reg, scratch, sizeof(CallFrame));
                a.ldr(scratch, a64::ptr(frame_reg, offsetStackBase));
                a.add(local_reg, stack_reg, scratch, a64::lsl(3));
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
                a.ldrb(scratch_w, a64::ptr(vm_reg, offsetHadError));
                a.cbnz(scratch, frame_changed);
                a.ldr(stack_reg, a64::ptr(co_reg, offsetStack));
                a.ldr(scratch, a64::ptr(co_reg, offsetFrames + 8));
                a.sub(frame_reg, scratch, sizeof(CallFrame));
                a.ldr(scratch, a64::ptr(frame_reg, offsetStackBase));
                a.add(local_reg, stack_reg, scratch, a64::lsl(3));
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
                a.ldrb(scratch_w, a64::ptr(vm_reg, offsetHadError));
                a.cbnz(scratch, frame_changed);
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
                a.ldrb(scratch_w, a64::ptr(vm_reg, offsetHadError));
                a.cbnz(scratch, frame_changed);
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
            case OpCode::OP_GET_TABUP_LONG: {
                uint8_t upIndex = bytecode[++i];
                uint32_t keyIndex = bytecode[++i];
                keyIndex |= (bytecode[++i] << 8);
                keyIndex |= (bytecode[++i] << 16);
                a.str(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.mov(a64::x0, vm_reg);
                a.mov(a64::x1, (uint32_t)upIndex);
                a.mov(a64::x2, keyIndex);
                a.mov(a64::x3, (uint32_t)(i + 1));
                a.mov(scratch, (uint64_t)VM::jitGetTabUp);
                a.blr(scratch);
                a.ldr(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.ldrb(scratch_w, a64::ptr(vm_reg, offsetHadError));
                a.cbnz(scratch, frame_changed);
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
            case OpCode::OP_SET_TABUP_LONG: {
                uint8_t upIndex = bytecode[++i];
                uint32_t keyIndex = bytecode[++i];
                keyIndex |= (bytecode[++i] << 8);
                keyIndex |= (bytecode[++i] << 16);
                a.str(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.mov(a64::x0, vm_reg);
                a.mov(a64::x1, (uint32_t)upIndex);
                a.mov(a64::x2, keyIndex);
                a.mov(a64::x3, (uint32_t)(i + 1));
                a.mov(scratch, (uint64_t)VM::jitSetTabUp);
                a.blr(scratch);
                a.ldr(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.ldrb(scratch_w, a64::ptr(vm_reg, offsetHadError));
                a.cbnz(scratch, frame_changed);
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
            case OpCode::OP_LEN: {
                a.str(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.mov(a64::x0, vm_reg);
                a.mov(a64::x1, (uint32_t)(i + 1));
                a.mov(scratch, (uint64_t)VM::jitLen);
                a.blr(scratch);
                a.ldr(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.ldrb(scratch_w, a64::ptr(vm_reg, offsetHadError));
                a.cbnz(scratch, frame_changed);
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
            case OpCode::OP_NEW_TABLE: {
                a.str(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.mov(a64::x0, vm_reg);
                a.mov(scratch, (uint64_t)VM::jitNewTable);
                a.blr(scratch);
                a.ldr(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.ldrb(scratch_w, a64::ptr(vm_reg, offsetHadError));
                a.cbnz(scratch, frame_changed);
                a.ldr(stack_reg, a64::ptr(co_reg, offsetStack));
                a.ldr(scratch, a64::ptr(co_reg, offsetFrames + 8));
                a.sub(frame_reg, scratch, sizeof(CallFrame));
                a.ldr(scratch, a64::ptr(frame_reg, offsetStackBase));
                a.add(local_reg, stack_reg, scratch, a64::lsl(3));
                break;
            }
            case OpCode::OP_GET_TABLE: {
                Label fallback = a.new_label();
                Label success = a.new_label();

                a.ldr(scratch, a64::ptr(top_reg, -8));   // key
                a.ldr(scratch2, a64::ptr(top_reg, -16)); // tbl

                // 1. Check tbl is TableObject (0xFFF5)
                a64::Gp tag_tbl = a64::x11;
                a.lsr(tag_tbl, scratch2, 48);
                a.mov(a64::x13, 0xFFF5);
                a.cmp(tag_tbl, a64::x13);
                a.b_ne(fallback);

                // 2. Extract TableObject pointer (clear top 16 bits)
                a64::Gp tbl_ptr = a64::x12;
                a.ubfx(tbl_ptr, scratch2, 0, 48);

                // 3. Check metatable is nil
                a.ldr(a64::x14, a64::ptr(tbl_ptr, offsetMetatable));
                a.mov(a64::x15, Value::nil().bits());
                a.cmp(a64::x14, a64::x15);
                a.b_ne(fallback);

                // 4. Check key is integer (0xFFF3)
                a64::Gp tag_key = a64::x11;
                a.lsr(tag_key, scratch, 48);
                a.mov(a64::x13, 0xFFF3);
                a.cmp(tag_key, a64::x13);
                a.b_ne(fallback);

                // 5. Check 1 <= idx <= array_.size()
                a64::Gp idx = a64::x13;
                a.sbfx(idx, scratch, 0, 48);
                a.cmp(idx, 1);
                a.b_lt(fallback);

                // array_.size() = (end - begin) in bytes
                a64::Gp arr_begin = a64::x14;
                a64::Gp arr_end = a64::x15;
                a.ldr(arr_begin, a64::ptr(tbl_ptr, offsetArray));
                a.ldr(arr_end, a64::ptr(tbl_ptr, offsetArray + 8));
                a.sub(arr_end, arr_end, arr_begin); // size in bytes
                a.sub(idx, idx, 1); // 0-based idx
                a.lsl(idx, idx, 3); // idx in bytes
                a.cmp(idx, arr_end);
                a.b_hs(fallback);

                // Fast array load
                a.ldr(scratch, a64::ptr(arr_begin, idx));
                a.str(scratch, a64::ptr(top_reg, -16));
                a.sub(top_reg, top_reg, 8);
                a.b(success);

                a.bind(fallback);
                a.str(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.mov(a64::x0, vm_reg);
                a.mov(a64::x1, (uint32_t)(i + 1));
                a.mov(scratch, (uint64_t)VM::jitGetTable);
                a.blr(scratch);
                a.ldr(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.ldrb(scratch_w, a64::ptr(vm_reg, offsetHadError));
                a.cbnz(scratch, frame_changed);
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

                a.bind(success);
                break;
            }
            case OpCode::OP_SET_TABLE: {
                Label fallback = a.new_label();
                Label success = a.new_label();

                a.ldr(scratch, a64::ptr(top_reg, -8));    // val
                a.ldr(scratch2, a64::ptr(top_reg, -16));  // key
                a64::Gp tbl_val = a64::x13;
                a.ldr(tbl_val, a64::ptr(top_reg, -24));   // tbl

                // 1. Check tbl is TableObject (0xFFF5)
                a64::Gp tag_tbl = a64::x11;
                a.lsr(tag_tbl, tbl_val, 48);
                a.mov(a64::x12, 0xFFF5);
                a.cmp(tag_tbl, a64::x12);
                a.b_ne(fallback);

                // 2. Extract TableObject pointer
                a64::Gp tbl_ptr = a64::x12;
                a.ubfx(tbl_ptr, tbl_val, 0, 48);

                // 3. Check metatable is nil
                a.ldr(a64::x14, a64::ptr(tbl_ptr, offsetMetatable));
                a.mov(a64::x15, Value::nil().bits());
                a.cmp(a64::x14, a64::x15);
                a.b_ne(fallback);

                // 4. Check val is not a GC object (tag <= 0xFFF3)
                a64::Gp tag_val = a64::x11;
                a.lsr(tag_val, scratch, 48);
                a.mov(a64::x14, 0xFFF3);
                a.cmp(tag_val, a64::x14);
                a.b_hi(fallback); // tag > 0xFFF3 is GC object

                // 5. Check key is integer (0xFFF3)
                a64::Gp tag_key = a64::x11;
                a.lsr(tag_key, scratch2, 48);
                a.cmp(tag_key, a64::x14);
                a.b_ne(fallback);

                // 6. Check 1 <= idx <= array_.size()
                a64::Gp idx = a64::x11;
                a.sbfx(idx, scratch2, 0, 48);
                a.cmp(idx, 1);
                a.b_lt(fallback);

                a64::Gp arr_begin = a64::x14;
                a64::Gp arr_end = a64::x15;
                a.ldr(arr_begin, a64::ptr(tbl_ptr, offsetArray));
                a.ldr(arr_end, a64::ptr(tbl_ptr, offsetArray + 8));
                a.sub(arr_end, arr_end, arr_begin);
                a.sub(idx, idx, 1);
                a.lsl(idx, idx, 3);
                a.cmp(idx, arr_end);
                a.b_hs(fallback);

                // Fast array store
                a.str(scratch, a64::ptr(arr_begin, idx));
                // Invalidate lastLen_ (set to 0)
                a.str(a64::xzr, a64::ptr(tbl_ptr, offsetLastLen));
                a.sub(top_reg, top_reg, 24);
                a.b(success);

                a.bind(fallback);
                a.str(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.mov(a64::x0, vm_reg);
                a.mov(a64::x1, (uint32_t)(i + 1));
                a.mov(scratch, (uint64_t)VM::jitSetTable);
                a.blr(scratch);
                a.ldr(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.ldrb(scratch_w, a64::ptr(vm_reg, offsetHadError));
                a.cbnz(scratch, frame_changed);
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

                a.bind(success);
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
                a.ldrb(scratch_w, a64::ptr(vm_reg, offsetHadError));
                a.cbnz(scratch, frame_changed);
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
                a.ldrb(scratch_w, a64::ptr(vm_reg, offsetHadError));
                a.cbnz(scratch, frame_changed);
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
                a.ldrb(scratch_w, a64::ptr(vm_reg, offsetHadError));
                a.cbnz(scratch, frame_changed);
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
                a.ldrb(scratch_w, a64::ptr(vm_reg, offsetHadError));
                a.cbnz(scratch, frame_changed);
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
                a.ldrb(scratch_w, a64::ptr(vm_reg, offsetHadError));
                a.cbnz(scratch, frame_changed);
                a.ldr(stack_reg, a64::ptr(co_reg, offsetStack));
                a.ldr(scratch, a64::ptr(co_reg, offsetFrames + 8));
                a.sub(frame_reg, scratch, sizeof(CallFrame));
                a.ldr(scratch, a64::ptr(frame_reg, offsetStackBase));
                a.add(local_reg, stack_reg, scratch, a64::lsl(3));
                break;
            }
            case OpCode::OP_CLOSURE_LONG: {
                uint32_t constantIndex = bytecode[++i];
                constantIndex |= (bytecode[++i] << 8);
                constantIndex |= (bytecode[++i] << 16);
                size_t bytecodeOffset = i + 1;
                
                // Skip the upvalue capture bytes
                Value funcValue = chunk->constants()[constantIndex];
                FunctionObject* innerFunc = chunk->getFunction(funcValue.asFunctionIndex());
                i += 2 * innerFunc->upvalueCount();

                a.str(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.mov(a64::x0, vm_reg);
                a.mov(a64::x1, constantIndex);
                a.mov(a64::x2, (uint32_t)bytecodeOffset);
                a.mov(scratch, (uint64_t)VM::jitClosure);
                a.blr(scratch);
                a.ldr(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.ldrb(scratch_w, a64::ptr(vm_reg, offsetHadError));
                a.cbnz(scratch, frame_changed);
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
                a.ldrb(scratch_w, a64::ptr(vm_reg, offsetHadError));
                a.cbnz(scratch, frame_changed);
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
            case OpCode::OP_CALL_MULTI: {
                uint8_t fixedArgCount = bytecode[++i];
                uint8_t retCount = bytecode[++i];

                a.str(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.mov(a64::x0, vm_reg);
                a.mov(a64::x1, (uint32_t)fixedArgCount);
                a.mov(a64::x2, (uint32_t)retCount);
                a.mov(a64::x3, (uint32_t)(i + 1));
                a.mov(scratch, (uint64_t)VM::jitCallMulti);
                a.blr(scratch);
                a.ldr(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.ldrb(scratch_w, a64::ptr(vm_reg, offsetHadError));
                a.cbnz(scratch, frame_changed);
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
            case OpCode::OP_TAILCALL: {
                uint8_t argCount = bytecode[++i];
                a.str(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.mov(a64::x0, vm_reg);
                a.mov(a64::x1, (uint32_t)argCount);
                a.mov(a64::x2, (uint32_t)(i + 1));
                a.mov(scratch, (uint64_t)VM::jitTailCall);
                a.blr(scratch);
                a.ldr(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.b(frame_changed);
                break;
            }
            case OpCode::OP_TAILCALL_MULTI: {
                uint8_t fixedArgCount = bytecode[++i];
                a.str(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.mov(a64::x0, vm_reg);
                a.mov(a64::x1, (uint32_t)fixedArgCount);
                a.mov(a64::x2, (uint32_t)(i + 1));
                a.mov(scratch, (uint64_t)VM::jitTailCallMulti);
                a.blr(scratch);
                a.ldr(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.b(frame_changed);
                break;
            }
            case OpCode::OP_BAND:
            case OpCode::OP_BOR:
            case OpCode::OP_BXOR: {
                Label fallback = a.new_label();
                Label success = a.new_label();

                a.ldr(scratch, a64::ptr(top_reg, -8));   // val2
                a.ldr(scratch2, a64::ptr(top_reg, -16)); // val1

                a64::Gp tag1 = a64::x11;
                a64::Gp tag2 = a64::x12;
                a.lsr(tag2, scratch, 48);
                a.lsr(tag1, scratch2, 48);

                a.mov(a64::x13, 0xFFF3);
                a.cmp(tag1, a64::x13);
                a.b_ne(fallback);
                a.cmp(tag2, a64::x13);
                a.b_ne(fallback);

                if (op == OpCode::OP_BAND) {
                    a.and_(scratch2, scratch2, scratch);
                } else if (op == OpCode::OP_BOR) {
                    a.orr(scratch2, scratch2, scratch);
                } else if (op == OpCode::OP_BXOR) {
                    a.eor(scratch2, scratch2, scratch);
                    a.mov(a64::x13, 0xFFF3);
                    a.bfi(scratch2, a64::x13, 48, 16);
                }
                a.str(scratch2, a64::ptr(top_reg, -16));
                a.sub(top_reg, top_reg, 8);
                a.b(success);

                a.bind(fallback);
                a.str(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.mov(a64::x0, vm_reg);
                a.mov(a64::x1, (uint32_t)(i + 1));
                if (op == OpCode::OP_BAND) a.mov(scratch, (uint64_t)VM::jitBand);
                else if (op == OpCode::OP_BOR) a.mov(scratch, (uint64_t)VM::jitBor);
                else if (op == OpCode::OP_BXOR) a.mov(scratch, (uint64_t)VM::jitBxor);
                a.blr(scratch);
                a.ldr(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.ldrb(scratch_w, a64::ptr(vm_reg, offsetHadError));
                a.cbnz(scratch, frame_changed);
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

                a.bind(success);
                break;
            }
            case OpCode::OP_SHL:
            case OpCode::OP_SHR: {
                a.str(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.mov(a64::x0, vm_reg);
                a.mov(a64::x1, (uint32_t)(i + 1));
                if (op == OpCode::OP_SHL) a.mov(scratch, (uint64_t)VM::jitShl);
                else if (op == OpCode::OP_SHR) a.mov(scratch, (uint64_t)VM::jitShr);
                a.blr(scratch);
                a.ldr(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.ldrb(scratch_w, a64::ptr(vm_reg, offsetHadError));
                a.cbnz(scratch, frame_changed);
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
            case OpCode::OP_BNOT: {
                Label fallback = a.new_label();
                Label success = a.new_label();

                a.ldr(scratch, a64::ptr(top_reg, -8));
                a64::Gp tag = a64::x11;
                a.lsr(tag, scratch, 48);
                a.mov(a64::x12, 0xFFF3);
                a.cmp(tag, a64::x12);
                a.b_ne(fallback);

                a.sbfx(scratch2, scratch, 0, 48);
                a.mvn(scratch2, scratch2);
                a.sbfx(a64::x13, scratch2, 0, 48);
                a.cmp(a64::x13, scratch2);
                a.b_ne(fallback);

                a.mov(a64::x13, 0xFFF3);
                a.bfi(scratch2, a64::x13, 48, 16);
                a.str(scratch2, a64::ptr(top_reg, -8));
                a.b(success);

                a.bind(fallback);
                a.str(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.mov(a64::x0, vm_reg);
                a.mov(a64::x1, (uint32_t)(i + 1));
                a.mov(scratch, (uint64_t)VM::jitBnot);
                a.blr(scratch);
                a.ldr(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.ldrb(scratch_w, a64::ptr(vm_reg, offsetHadError));
                a.cbnz(scratch, frame_changed);
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

                a.bind(success);
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
                a.ldrb(scratch_w, a64::ptr(vm_reg, offsetHadError));
                a.cbnz(scratch, frame_changed);
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
            case OpCode::OP_ADD:
            case OpCode::OP_SUB:
            case OpCode::OP_MUL:
            case OpCode::OP_DIV: {
                Label fallback = a.new_label();
                Label success = a.new_label();
                Label float_int = a.new_label();
                Label int_float = a.new_label();
                Label both_floats_rhs_int = a.new_label();

                a.ldr(scratch, a64::ptr(top_reg, -8));   // val2 (rhs)
                a.ldr(scratch2, a64::ptr(top_reg, -16)); // val1 (lhs)

                a64::Gp tag1 = a64::x11;
                a64::Gp tag2 = a64::x12;
                a.lsr(tag2, scratch, 48);
                a.lsr(tag1, scratch2, 48);

                a.mov(a64::x13, 0xFFF3); // INTEGER tag
                a.cmp(tag1, a64::x13);
                a.b_ne(float_int);
                // lhs is integer
                a.cmp(tag2, a64::x13);
                a.b_ne(int_float);

                // Both are integers!
                if (op == OpCode::OP_DIV) {
                    // Division in Lua is always floating-point: 5 / 2 = 2.5
                    a.sbfx(scratch2, scratch2, 0, 48);
                    a.sbfx(scratch, scratch, 0, 48);
                    a.scvtf(a64::d0, scratch2);
                    a.scvtf(a64::d1, scratch);
                    a.fdiv(a64::d0, a64::d0, a64::d1);
                    a.fmov(scratch, a64::d0);
                    a.str(scratch, a64::ptr(top_reg, -16));
                    a.sub(top_reg, top_reg, 8);
                    a.b(success);
                } else {
                    a.sbfx(scratch2, scratch2, 0, 48);
                    a.sbfx(scratch, scratch, 0, 48);
                    if (op == OpCode::OP_ADD) a.add(scratch2, scratch2, scratch);
                    else if (op == OpCode::OP_SUB) a.sub(scratch2, scratch2, scratch);
                    else if (op == OpCode::OP_MUL) a.mul(scratch2, scratch2, scratch);

                    // Check if scratch2 fits in 48-bit signed integer
                    a.sbfx(a64::x14, scratch2, 0, 48);
                    a.cmp(a64::x14, scratch2);
                    a.b_ne(fallback);

                    a.mov(a64::x14, 0xFFF3);
                    a.bfi(scratch2, a64::x14, 48, 16);
                    a.str(scratch2, a64::ptr(top_reg, -16));
                    a.sub(top_reg, top_reg, 8);
                    a.b(success);
                }

                // lhs is not int: check if lhs is float (< 0xFFF1)
                a.bind(float_int);
                a.mov(a64::x14, 0xFFF1);
                a.cmp(tag1, a64::x14);
                a.b_hs(fallback);

                // lhs is float: check rhs
                a.cmp(tag2, a64::x13);
                a.b_eq(both_floats_rhs_int);
                a.cmp(tag2, a64::x14);
                a.b_hs(fallback);

                // Both are floats!
                a.fmov(a64::d0, scratch2);
                a.fmov(a64::d1, scratch);
                if (op == OpCode::OP_ADD) a.fadd(a64::d0, a64::d0, a64::d1);
                else if (op == OpCode::OP_SUB) a.fsub(a64::d0, a64::d0, a64::d1);
                else if (op == OpCode::OP_MUL) a.fmul(a64::d0, a64::d0, a64::d1);
                else if (op == OpCode::OP_DIV) a.fdiv(a64::d0, a64::d0, a64::d1);
                a.fmov(scratch, a64::d0);
                a.str(scratch, a64::ptr(top_reg, -16));
                a.sub(top_reg, top_reg, 8);
                a.b(success);

                // lhs is float, rhs is int
                a.bind(both_floats_rhs_int);
                a.sbfx(scratch, scratch, 0, 48);
                a.scvtf(a64::d1, scratch);
                a.fmov(a64::d0, scratch2);
                if (op == OpCode::OP_ADD) a.fadd(a64::d0, a64::d0, a64::d1);
                else if (op == OpCode::OP_SUB) a.fsub(a64::d0, a64::d0, a64::d1);
                else if (op == OpCode::OP_MUL) a.fmul(a64::d0, a64::d0, a64::d1);
                else if (op == OpCode::OP_DIV) a.fdiv(a64::d0, a64::d0, a64::d1);
                a.fmov(scratch, a64::d0);
                a.str(scratch, a64::ptr(top_reg, -16));
                a.sub(top_reg, top_reg, 8);
                a.b(success);

                // lhs is int, rhs is not int
                a.bind(int_float);
                a.mov(a64::x14, 0xFFF1);
                a.cmp(tag2, a64::x14);
                a.b_hs(fallback);

                // lhs is int, rhs is float
                a.sbfx(scratch2, scratch2, 0, 48);
                a.scvtf(a64::d0, scratch2);
                a.fmov(a64::d1, scratch);
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
                Label is_float = a.new_label();

                a.ldr(scratch, a64::ptr(top_reg, -8)); // val1
                a64::Gp tag = a64::x11;
                a.lsr(tag, scratch, 48);

                a.mov(a64::x12, 0xFFF3);
                a.cmp(tag, a64::x12);
                a.b_ne(is_float);

                // Integer negation
                a.sbfx(scratch2, scratch, 0, 48);
                a.neg(scratch2, scratch2);
                a.sbfx(a64::x13, scratch2, 0, 48);
                a.cmp(a64::x13, scratch2);
                a.b_ne(fallback);

                a.mov(a64::x13, 0xFFF3);
                a.bfi(scratch2, a64::x13, 48, 16);
                a.str(scratch2, a64::ptr(top_reg, -8));
                a.b(success);

                a.bind(is_float);
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
                Label is_false = a.new_label();
                Label success = a.new_label();
                Label check_floats = a.new_label();

                a.ldr(scratch, a64::ptr(top_reg, -8));   // val2 (rhs)
                a.ldr(scratch2, a64::ptr(top_reg, -16)); // val1 (lhs)

                a64::Gp tag1 = a64::x11;
                a64::Gp tag2 = a64::x12;
                a.lsr(tag2, scratch, 48);
                a.lsr(tag1, scratch2, 48);

                // If OP_EQUAL and exact bits match, it is equal!
                // (except for floats where NaN != NaN, so only if not float)
                if (op == OpCode::OP_EQUAL) {
                    Label not_exact = a.new_label();
                    a.cmp(scratch, scratch2);
                    a.b_ne(not_exact);
                    // Same bits: check if float
                    a.mov(a64::x13, 0xFFF1);
                    a.cmp(tag1, a64::x13);
                    a.b_hs(is_true); // Non-float identical bits: true!
                    a.bind(not_exact);
                }

                // Check if both are integers (0xFFF3)
                a.mov(a64::x13, 0xFFF3);
                a.cmp(tag1, a64::x13);
                a.b_ne(check_floats);
                a.cmp(tag2, a64::x13);
                a.b_ne(check_floats);

                // Both are integers!
                a.sbfx(scratch2, scratch2, 0, 48);
                a.sbfx(scratch, scratch, 0, 48);
                a.cmp(scratch2, scratch);
                if (op == OpCode::OP_EQUAL) a.b_eq(is_true);
                else if (op == OpCode::OP_LESS) a.b_lt(is_true);
                else if (op == OpCode::OP_LESS_EQUAL) a.b_le(is_true);
                else if (op == OpCode::OP_GREATER) a.b_gt(is_true);
                else if (op == OpCode::OP_GREATER_EQUAL) a.b_ge(is_true);
                a.b(is_false);

                // Check if both are floats (< 0xFFF1)
                a.bind(check_floats);
                a.mov(a64::x13, 0xFFF1);
                a.cmp(tag1, a64::x13);
                a.b_hs(fallback);
                a.cmp(tag2, a64::x13);
                a.b_hs(fallback);

                a.fmov(a64::d0, scratch2);
                a.fmov(a64::d1, scratch);
                a.fcmp(a64::d0, a64::d1);

                if (op == OpCode::OP_EQUAL) a.b_eq(is_true);
                else if (op == OpCode::OP_LESS) a.b_mi(is_true);
                else if (op == OpCode::OP_LESS_EQUAL) a.b_ls(is_true);
                else if (op == OpCode::OP_GREATER) a.b_gt(is_true);
                else if (op == OpCode::OP_GREATER_EQUAL) a.b_ge(is_true);

                a.bind(is_false);
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
                // Unsupported opcode, cannot safely compile function
                return nullptr;
            }
        }
    }

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
