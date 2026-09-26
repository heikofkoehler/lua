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

static void callHelperX64(x86::Assembler& a, void* func, x86::Gp vm_reg, int numArgs, uint64_t a2 = 0, uint64_t a3 = 0, uint64_t a4 = 0) {
#ifdef _WIN32
    if (vm_reg != x86::rcx) a.mov(x86::rcx, vm_reg);
    if (numArgs >= 2) a.mov(x86::rdx, a2);
    if (numArgs >= 3) a.mov(x86::r8, a3);
    if (numArgs >= 4) a.mov(x86::r9, a4);
    a.mov(x86::rax, (uint64_t)func);
    a.sub(x86::rsp, 32);
    a.call(x86::rax);
    a.add(x86::rsp, 32);
#else
    if (vm_reg != x86::rdi) a.mov(x86::rdi, vm_reg);
    if (numArgs >= 2) a.mov(x86::rsi, a2);
    if (numArgs >= 3) a.mov(x86::rdx, a3);
    if (numArgs >= 4) a.mov(x86::rcx, a4);
    a.mov(x86::rax, (uint64_t)func);
    a.call(x86::rax);
#endif
}

static void callHelperX64(x86::Assembler& a, void* func, x86::Gp vm_reg, x86::Gp arg2) {
#ifdef _WIN32
    if (vm_reg != x86::rcx) a.mov(x86::rcx, vm_reg);
    a.mov(x86::rdx, arg2);
    a.mov(x86::rax, (uint64_t)func);
    a.sub(x86::rsp, 32);
    a.call(x86::rax);
    a.add(x86::rsp, 32);
#else
    if (vm_reg != x86::rdi) a.mov(x86::rdi, vm_reg);
    a.mov(x86::rsi, arg2);
    a.mov(x86::rax, (uint64_t)func);
    a.call(x86::rax);
#endif
}

bool JITCompiler::assembleX64(FunctionObject* function, CodeHolder& code) {
    if (!code.is_initialized()) {
        code.init(Environment(Arch::kX64));
    }

    // Intern string constants in the chunk before compiling
    vm_->internConstants(*function);

    // Offsets
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

    x86::Assembler a(&code);

    // Register Mapping (callee-saved):
    // rbx: VM* vm
    // r12: CoroutineObject* co
    // r13: Value* stack_begin
    // r14: Value* local_base (stackBase)
    // r15: Value* top_reg
    // rbp: CallFrame* frame
    // scratch: r10
    // scratch2: r11

    x86::Gp vm_reg = x86::rbx;
    x86::Gp co_reg = x86::r12;
    x86::Gp stack_reg = x86::r13;
    x86::Gp local_reg = x86::r14;
    x86::Gp top_reg = x86::r15;
    x86::Gp frame_reg = x86::rbp;

    x86::Gp scratch = x86::r10;
    x86::Gp scratch2 = x86::r11;

    Label epilogue = a.new_label();
    Label frame_changed = a.new_label();

    auto reloadState = [&]() {
        a.mov(top_reg, x86::qword_ptr(co_reg, offsetStack + 8));
        a.cmp(x86::byte_ptr(vm_reg, offsetHadError), 0);
        a.jne(frame_changed);
        a.mov(stack_reg, x86::qword_ptr(co_reg, offsetStack));

        // Check if frame count changed
        a.mov(scratch, x86::qword_ptr(co_reg, offsetFrames + 8));
        a.sub(scratch, x86::qword_ptr(co_reg, offsetFrames));
        a.cmp(scratch, x86::qword_ptr(x86::rsp, 0));
        a.jne(frame_changed);

        // Reload frame and local_base
        a.mov(scratch, x86::qword_ptr(co_reg, offsetFrames + 8));
        a.sub(scratch, sizeof(CallFrame));
        a.mov(frame_reg, scratch);
        a.mov(scratch, x86::qword_ptr(frame_reg, offsetStackBase));
        a.lea(local_reg, x86::ptr(stack_reg, scratch, 3));
    };

    // --- Prologue ---
    a.push(x86::rbp);
    a.mov(x86::rbp, x86::rsp);
    a.push(x86::rbx);
    a.push(x86::r12);
    a.push(x86::r13);
    a.push(x86::r14);
    a.push(x86::r15);
    a.sub(x86::rsp, 40); // 16-byte stack alignment: 8 + 8 + 40 + 40 = 96 (divisible by 16)

#ifdef _WIN32
    a.mov(vm_reg, x86::rcx);
#else
    a.mov(vm_reg, x86::rdi);
#endif

    a.mov(co_reg, x86::qword_ptr(vm_reg, offsetCurrentCoroutine));
    a.mov(stack_reg, x86::qword_ptr(co_reg, offsetStack));
    a.mov(top_reg, x86::qword_ptr(co_reg, offsetStack + 8));

    // Ensure stack headroom (2048 slots = 16384 bytes)
    Label stack_ok = a.new_label();
    a.mov(scratch, x86::qword_ptr(co_reg, offsetStack + 16)); // __end_cap_
    a.sub(scratch, top_reg);
    a.cmp(scratch, 2048 * 8);
    a.jae(stack_ok);
    a.mov(x86::qword_ptr(co_reg, offsetStack + 8), top_reg);
    callHelperX64(a, (void*)VM::jitEnsureStack, vm_reg, 2, 2048);
    a.mov(stack_reg, x86::qword_ptr(co_reg, offsetStack));
    a.mov(top_reg, x86::qword_ptr(co_reg, offsetStack + 8));
    a.bind(stack_ok);

    // Save frames_size in [rsp + 0]
    a.mov(scratch, x86::qword_ptr(co_reg, offsetFrames + 8));
    a.sub(scratch, x86::qword_ptr(co_reg, offsetFrames));
    a.mov(x86::qword_ptr(x86::rsp, 0), scratch);

    // Current frame = frames.back()
    a.mov(scratch, x86::qword_ptr(co_reg, offsetFrames + 8));
    a.sub(scratch, sizeof(CallFrame));
    a.mov(frame_reg, scratch);

    // Load stackBase and calc local_base
    a.mov(scratch, x86::qword_ptr(frame_reg, offsetStackBase));
    a.lea(local_reg, x86::ptr(stack_reg, scratch, 3));

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
                a.mov(x86::qword_ptr(top_reg), scratch);
                a.add(top_reg, 8);
                break;
            }
            case OpCode::OP_CONSTANT_LONG: {
                uint32_t index = bytecode[++i];
                index |= (bytecode[++i] << 8);
                index |= (bytecode[++i] << 16);
                Value val = chunk->constants()[index];
                a.mov(scratch, val.bits());
                a.mov(x86::qword_ptr(top_reg), scratch);
                a.add(top_reg, 8);
                break;
            }
            case OpCode::OP_GET_LOCAL: {
                uint8_t slot = bytecode[++i];
                a.mov(scratch, x86::qword_ptr(local_reg, (int32_t)slot * 8));
                a.mov(x86::qword_ptr(top_reg), scratch);
                a.add(top_reg, 8);
                break;
            }
            case OpCode::OP_SET_LOCAL: {
                uint8_t slot = bytecode[++i];
                a.sub(top_reg, 8);
                a.mov(scratch, x86::qword_ptr(top_reg));
                a.mov(x86::qword_ptr(local_reg, (int32_t)slot * 8), scratch);
                break;
            }
            case OpCode::OP_POP: {
                a.sub(top_reg, 8);
                break;
            }
            case OpCode::OP_DUP: {
                a.mov(scratch, x86::qword_ptr(top_reg, -8));
                a.mov(x86::qword_ptr(top_reg), scratch);
                a.add(top_reg, 8);
                break;
            }
            case OpCode::OP_SWAP: {
                a.mov(scratch, x86::qword_ptr(top_reg, -8));
                a.mov(scratch2, x86::qword_ptr(top_reg, -16));
                a.mov(x86::qword_ptr(top_reg, -16), scratch);
                a.mov(x86::qword_ptr(top_reg, -8), scratch2);
                break;
            }
            case OpCode::OP_TRUE: {
                a.mov(scratch, Value::boolean(true).bits());
                a.mov(x86::qword_ptr(top_reg), scratch);
                a.add(top_reg, 8);
                break;
            }
            case OpCode::OP_FALSE: {
                a.mov(scratch, Value::boolean(false).bits());
                a.mov(x86::qword_ptr(top_reg), scratch);
                a.add(top_reg, 8);
                break;
            }
            case OpCode::OP_NIL: {
                a.mov(scratch, Value::nil().bits());
                a.mov(x86::qword_ptr(top_reg), scratch);
                a.add(top_reg, 8);
                break;
            }
            case OpCode::OP_JUMP: {
                uint16_t offset = bytecode[i+1] | (bytecode[i+2] << 8);
                i += 2;
                a.jmp(labels[i + 1 + offset]);
                break;
            }
            case OpCode::OP_LOOP: {
                uint16_t offset = bytecode[i+1] | (bytecode[i+2] << 8);
                i += 2;
                size_t loop_dest = i + 1 - offset;
                a.mov(scratch, (uint64_t)loop_dest);
                a.mov(x86::qword_ptr(frame_reg, offsetIp), scratch);
                a.cmp(x86::dword_ptr(vm_reg, offsetInterrupted), 0);
                a.jne(frame_changed);
                a.jmp(labels[loop_dest]);
                break;
            }
            case OpCode::OP_JUMP_IF_FALSE: {
                uint16_t offset = bytecode[i+1] | (bytecode[i+2] << 8);
                size_t next_ip = i + 3;
                i += 2;
                a.mov(scratch, x86::qword_ptr(top_reg, -8));
                a.mov(scratch2, Value::nil().bits());
                a.cmp(scratch, scratch2);
                a.je(labels[next_ip + offset]);
                a.mov(scratch2, Value::boolean(false).bits());
                a.cmp(scratch, scratch2);
                a.je(labels[next_ip + offset]);
                break;
            }
            case OpCode::OP_FORPREP: {
                uint8_t rawBase = bytecode[++i];
                uint8_t base = rawBase & 0x7F;
                uint16_t offset = bytecode[i+1] | (bytecode[i+2] << 8);
                i += 2;

                a.mov(x86::qword_ptr(co_reg, offsetStack + 8), top_reg);
                callHelperX64(a, (void*)VM::jitForPrep, vm_reg, 3, (uint32_t)rawBase, (uint32_t)offset);
                a.mov(stack_reg, x86::qword_ptr(co_reg, offsetStack));
                a.mov(scratch, x86::qword_ptr(co_reg, offsetFrames + 8));
                a.sub(scratch, sizeof(CallFrame));
                a.mov(frame_reg, scratch);
                a.mov(scratch, x86::qword_ptr(frame_reg, offsetStackBase));
                a.lea(local_reg, x86::ptr(stack_reg, scratch, 3));
                a.cmp(x86::byte_ptr(vm_reg, offsetHadError), 0);
                a.jne(frame_changed);

                Label loop_start = a.new_label();
                a.test(x86::al, x86::al);
                a.jz(loop_start);
                a.lea(top_reg, x86::ptr(local_reg, (int32_t)base * 8));
                a.jmp(labels[i + 1 + offset]);

                a.bind(loop_start);
                a.lea(top_reg, x86::ptr(local_reg, (int32_t)(base + 4) * 8));
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

                a.cmp(x86::dword_ptr(vm_reg, offsetInterrupted), 0);
                a.jne(frame_changed);

                a.mov(scratch, x86::qword_ptr(co_reg, offsetOpenUpvalues));
                a.cmp(scratch, x86::qword_ptr(co_reg, offsetOpenUpvalues + 8));
                a.jne(fallback);

                x86::Gp reg_init = x86::r8;
                x86::Gp reg_limit = x86::r9;
                x86::Gp reg_step = x86::rcx;
                x86::Gp reg_tag = x86::rdx;

                a.mov(reg_init, x86::qword_ptr(local_reg, (int32_t)base * 8));
                a.mov(reg_limit, x86::qword_ptr(local_reg, (int32_t)(base + 1) * 8));
                a.mov(reg_step, x86::qword_ptr(local_reg, (int32_t)(base + 2) * 8));

                a.mov(reg_tag, reg_init); a.shr(reg_tag, 48); a.cmp(reg_tag, 0xFFF3); a.jne(try_float);
                a.mov(reg_tag, reg_step); a.shr(reg_tag, 48); a.cmp(reg_tag, 0xFFF3); a.jne(try_float);
                a.mov(reg_tag, reg_limit); a.shr(reg_tag, 48); a.cmp(reg_tag, 0xFFF3); a.jne(fallback);

                a.shl(reg_init, 16); a.sar(reg_init, 16);
                a.shl(reg_step, 16); a.sar(reg_step, 16);
                a.shl(reg_limit, 16); a.sar(reg_limit, 16);

                x86::Gp reg_next = scratch;
                a.mov(reg_next, reg_init);
                a.add(reg_next, reg_step);
                a.jo(fallback);

                a.mov(scratch2, reg_next);
                a.shl(scratch2, 16); a.sar(scratch2, 16);
                a.cmp(scratch2, reg_next);
                a.jne(fallback);

                a.cmp(reg_step, 0);
                a.jle(step_neg);

                a.cmp(reg_next, reg_limit);
                a.jg(loop_end);
                a.jmp(loop_cont);

                a.bind(step_neg);
                a.cmp(reg_next, reg_limit);
                a.jl(loop_end);

                a.bind(loop_cont);
                a.shl(reg_next, 16); a.shr(reg_next, 16);
                a.mov(scratch2, 0xFFF3000000000000ULL);
                a.or_(reg_next, scratch2);
                a.mov(x86::qword_ptr(local_reg, (int32_t)base * 8), reg_next);
                a.mov(x86::qword_ptr(local_reg, (int32_t)(base + 3) * 8), reg_next);
                a.mov(scratch2, (uint64_t)loop_dest);
                a.mov(x86::qword_ptr(frame_reg, offsetIp), scratch2);
                a.jmp(labels[loop_dest]);

                // Float path
                a.bind(try_float);
                a.mov(reg_tag, reg_init); a.shr(reg_tag, 48); a.cmp(reg_tag, 0xFFF1); a.jae(fallback);
                a.mov(reg_tag, reg_step); a.shr(reg_tag, 48); a.cmp(reg_tag, 0xFFF1); a.jae(fallback);
                a.mov(reg_tag, reg_limit); a.shr(reg_tag, 48); a.cmp(reg_tag, 0xFFF1); a.jae(fallback);

                a.movq(x86::xmm0, reg_init);
                a.movq(x86::xmm1, reg_limit);
                a.movq(x86::xmm2, reg_step);

                a.movapd(x86::xmm3, x86::xmm0);
                a.addsd(x86::xmm3, x86::xmm2);

                Label loop_cont_f = a.new_label();
                a.xorpd(x86::xmm4, x86::xmm4);
                a.ucomisd(x86::xmm2, x86::xmm4);
                a.jp(fallback);
                a.jbe(step_neg_f);

                a.ucomisd(x86::xmm3, x86::xmm1);
                a.jp(fallback);
                a.ja(loop_end);
                a.jmp(loop_cont_f);

                a.bind(step_neg_f);
                a.ucomisd(x86::xmm3, x86::xmm1);
                a.jp(fallback);
                a.jb(loop_end);

                a.bind(loop_cont_f);
                a.movq(reg_next, x86::xmm3);
                a.mov(x86::qword_ptr(local_reg, (int32_t)base * 8), reg_next);
                a.mov(x86::qword_ptr(local_reg, (int32_t)(base + 3) * 8), reg_next);
                a.mov(scratch2, (uint64_t)loop_dest);
                a.mov(x86::qword_ptr(frame_reg, offsetIp), scratch2);
                a.jmp(labels[loop_dest]);

                a.bind(fallback);
                a.mov(x86::qword_ptr(co_reg, offsetStack + 8), top_reg);
                callHelperX64(a, (void*)VM::jitForLoopFallback, vm_reg, 2, (uint32_t)base);
                a.mov(stack_reg, x86::qword_ptr(co_reg, offsetStack));
                a.mov(scratch, x86::qword_ptr(co_reg, offsetFrames + 8));
                a.sub(scratch, sizeof(CallFrame));
                a.mov(frame_reg, scratch);
                a.mov(scratch, x86::qword_ptr(frame_reg, offsetStackBase));
                a.lea(local_reg, x86::ptr(stack_reg, scratch, 3));
                a.cmp(x86::byte_ptr(vm_reg, offsetHadError), 0);
                a.jne(frame_changed);
                a.test(x86::al, x86::al);
                a.jnz(labels[loop_dest]);

                a.bind(loop_end);
                a.lea(top_reg, x86::ptr(local_reg, (int32_t)base * 8));
                break;
            }
            case OpCode::OP_GET_GLOBAL: {
                uint8_t index = bytecode[++i];
                a.mov(x86::qword_ptr(co_reg, offsetStack + 8), top_reg);
                callHelperX64(a, (void*)VM::jitGetGlobal, vm_reg, 2, (uint32_t)index);
                a.mov(top_reg, x86::qword_ptr(co_reg, offsetStack + 8));
                a.cmp(x86::byte_ptr(vm_reg, offsetHadError), 0);
                a.jne(frame_changed);
                a.mov(stack_reg, x86::qword_ptr(co_reg, offsetStack));
                break;
            }
            case OpCode::OP_SET_GLOBAL: {
                uint8_t index = bytecode[++i];
                a.mov(x86::qword_ptr(co_reg, offsetStack + 8), top_reg);
                callHelperX64(a, (void*)VM::jitSetGlobal, vm_reg, 2, (uint32_t)index);
                a.mov(top_reg, x86::qword_ptr(co_reg, offsetStack + 8));
                a.cmp(x86::byte_ptr(vm_reg, offsetHadError), 0);
                a.jne(frame_changed);
                a.mov(stack_reg, x86::qword_ptr(co_reg, offsetStack));
                break;
            }
            case OpCode::OP_GET_TABUP: {
                uint8_t upIndex = bytecode[++i];
                uint8_t keyIndex = bytecode[++i];
                a.mov(x86::qword_ptr(co_reg, offsetStack + 8), top_reg);
                callHelperX64(a, (void*)VM::jitGetTabUp, vm_reg, 4, (uint32_t)upIndex, (uint32_t)keyIndex, (uint32_t)(i + 1));
                reloadState();
                break;
            }
            case OpCode::OP_SET_TABUP: {
                uint8_t upIndex = bytecode[++i];
                uint8_t keyIndex = bytecode[++i];
                a.mov(x86::qword_ptr(co_reg, offsetStack + 8), top_reg);
                callHelperX64(a, (void*)VM::jitSetTabUp, vm_reg, 4, (uint32_t)upIndex, (uint32_t)keyIndex, (uint32_t)(i + 1));
                reloadState();
                break;
            }
            case OpCode::OP_GET_TABUP_LONG: {
                uint8_t upIndex = bytecode[++i];
                uint32_t keyIndex = bytecode[++i];
                keyIndex |= (bytecode[++i] << 8);
                keyIndex |= (bytecode[++i] << 16);
                a.mov(x86::qword_ptr(co_reg, offsetStack + 8), top_reg);
                callHelperX64(a, (void*)VM::jitGetTabUp, vm_reg, 4, (uint32_t)upIndex, keyIndex, (uint32_t)(i + 1));
                reloadState();
                break;
            }
            case OpCode::OP_SET_TABUP_LONG: {
                uint8_t upIndex = bytecode[++i];
                uint32_t keyIndex = bytecode[++i];
                keyIndex |= (bytecode[++i] << 8);
                keyIndex |= (bytecode[++i] << 16);
                a.mov(x86::qword_ptr(co_reg, offsetStack + 8), top_reg);
                callHelperX64(a, (void*)VM::jitSetTabUp, vm_reg, 4, (uint32_t)upIndex, keyIndex, (uint32_t)(i + 1));
                reloadState();
                break;
            }
            case OpCode::OP_LEN: {
                a.mov(x86::qword_ptr(co_reg, offsetStack + 8), top_reg);
                callHelperX64(a, (void*)VM::jitLen, vm_reg, 2, (uint32_t)(i + 1));
                reloadState();
                break;
            }
            case OpCode::OP_NEW_TABLE: {
                a.mov(x86::qword_ptr(co_reg, offsetStack + 8), top_reg);
                callHelperX64(a, (void*)VM::jitNewTable, vm_reg, 1);
                a.mov(top_reg, x86::qword_ptr(co_reg, offsetStack + 8));
                a.cmp(x86::byte_ptr(vm_reg, offsetHadError), 0);
                a.jne(frame_changed);
                a.mov(stack_reg, x86::qword_ptr(co_reg, offsetStack));
                a.mov(scratch, x86::qword_ptr(co_reg, offsetFrames + 8));
                a.sub(scratch, sizeof(CallFrame));
                a.mov(frame_reg, scratch);
                a.mov(scratch, x86::qword_ptr(frame_reg, offsetStackBase));
                a.lea(local_reg, x86::ptr(stack_reg, scratch, 3));
                break;
            }
            case OpCode::OP_GET_TABLE: {
                Label fallback = a.new_label();
                Label success = a.new_label();
                Label read_ok = a.new_label();

                a.mov(scratch, x86::qword_ptr(top_reg, -8));   // key
                a.mov(scratch2, x86::qword_ptr(top_reg, -16)); // tbl

                // 1. Check tbl is TableObject (0xFFF5)
                a.mov(x86::rax, scratch2);
                a.shr(x86::rax, 48);
                a.cmp(x86::rax, 0xFFF5);
                a.jne(fallback);

                // 2. Extract TableObject pointer (clear top 16 bits)
                x86::Gp tbl_ptr = x86::rax;
                a.mov(tbl_ptr, scratch2);
                a.shl(tbl_ptr, 16);
                a.shr(tbl_ptr, 16);

                // 3. Check metatable is nil
                a.mov(scratch2, Value::nil().bits());
                a.cmp(x86::qword_ptr(tbl_ptr, offsetMetatable), scratch2);
                a.jne(fallback);

                // 4. Check key is integer (0xFFF3)
                a.mov(scratch2, scratch);
                a.shr(scratch2, 48);
                a.cmp(scratch2, 0xFFF3);
                a.jne(fallback);

                // 5. Check 1 <= idx <= array_.size()
                x86::Gp idx = x86::rdx;
                a.mov(idx, scratch);
                a.shl(idx, 16);
                a.sar(idx, 16);
                a.cmp(idx, 1);
                a.jl(fallback);

                x86::Gp arr_begin = x86::rcx;
                x86::Gp arr_end = x86::rsi;
                a.mov(arr_begin, x86::qword_ptr(tbl_ptr, offsetArray));
                a.mov(arr_end, x86::qword_ptr(tbl_ptr, offsetArray + 8));
                a.sub(arr_end, arr_begin);
                a.sub(idx, 1);
                a.shl(idx, 3);
                a.cmp(idx, arr_end);
                a.jae(fallback);

                a.mov(scratch, x86::qword_ptr(arr_begin, idx));
                a.mov(scratch2, 0xFFF0000000000000ULL);
                a.cmp(scratch, scratch2);
                a.jne(read_ok);
                a.mov(scratch, Value::nil().bits());
                a.bind(read_ok);
                a.mov(x86::qword_ptr(top_reg, -16), scratch);
                a.sub(top_reg, 8);
                a.jmp(success);

                a.bind(fallback);
                a.mov(x86::qword_ptr(co_reg, offsetStack + 8), top_reg);
                callHelperX64(a, (void*)VM::jitGetTable, vm_reg, 2, (uint32_t)(i + 1));
                reloadState();
                a.bind(success);
                break;
            }
            case OpCode::OP_SET_TABLE: {
                Label fallback = a.new_label();
                Label success = a.new_label();

                a.mov(scratch, x86::qword_ptr(top_reg, -16));  // key
                a.mov(scratch2, x86::qword_ptr(top_reg, -24)); // tbl
                x86::Gp val_reg = x86::r8;
                a.mov(val_reg, x86::qword_ptr(top_reg, -8));   // val

                a.mov(x86::rax, Value::nil().bits());
                a.cmp(val_reg, x86::rax);
                a.je(fallback);

                a.mov(x86::rax, scratch2);
                a.shr(x86::rax, 48);
                a.cmp(x86::rax, 0xFFF5);
                a.jne(fallback);

                x86::Gp tbl_ptr = x86::rax;
                a.mov(tbl_ptr, scratch2);
                a.shl(tbl_ptr, 16);
                a.shr(tbl_ptr, 16);

                a.mov(scratch2, Value::nil().bits());
                a.cmp(x86::qword_ptr(tbl_ptr, offsetMetatable), scratch2);
                a.jne(fallback);

                a.mov(scratch2, scratch);
                a.shr(scratch2, 48);
                a.cmp(scratch2, 0xFFF3);
                a.jne(fallback);

                x86::Gp idx = x86::rdx;
                a.mov(idx, scratch);
                a.shl(idx, 16);
                a.sar(idx, 16);
                a.cmp(idx, 1);
                a.jl(fallback);

                x86::Gp arr_begin = x86::rcx;
                x86::Gp arr_end = x86::rsi;
                a.mov(arr_begin, x86::qword_ptr(tbl_ptr, offsetArray));
                a.mov(arr_end, x86::qword_ptr(tbl_ptr, offsetArray + 8));
                a.sub(arr_end, arr_begin);
                a.sub(idx, 1);
                a.shl(idx, 3);
                a.cmp(idx, arr_end);
                a.jae(fallback);

                a.mov(x86::qword_ptr(arr_begin, idx), val_reg);
                a.mov(x86::qword_ptr(tbl_ptr, offsetLastLen), 0);
                a.sub(top_reg, 24);
                a.jmp(success);

                a.bind(fallback);
                a.mov(x86::qword_ptr(co_reg, offsetStack + 8), top_reg);
                callHelperX64(a, (void*)VM::jitSetTable, vm_reg, 2, (uint32_t)(i + 1));
                reloadState();
                a.bind(success);
                break;
            }
            case OpCode::OP_GET_UPVALUE: {
                uint8_t slot = bytecode[++i];
                a.mov(x86::qword_ptr(co_reg, offsetStack + 8), top_reg);
                callHelperX64(a, (void*)VM::jitGetUpvalue, vm_reg, 2, (uint32_t)slot);
                reloadState();
                break;
            }
            case OpCode::OP_SET_UPVALUE: {
                uint8_t slot = bytecode[++i];
                a.mov(x86::qword_ptr(co_reg, offsetStack + 8), top_reg);
                callHelperX64(a, (void*)VM::jitSetUpvalue, vm_reg, 2, (uint32_t)slot);
                reloadState();
                break;
            }
            case OpCode::OP_CLOSE_UPVALUE: {
                a.mov(scratch, top_reg);
                a.sub(scratch, stack_reg);
                a.shr(scratch, 3);
                a.sub(scratch, 1);

                a.mov(x86::qword_ptr(co_reg, offsetStack + 8), top_reg);
                callHelperX64(a, (void*)VM::jitCloseUpvalues, vm_reg, scratch);
                reloadState();
                a.sub(top_reg, 8);
                break;
            }
            case OpCode::OP_CONCAT: {
                a.mov(x86::qword_ptr(co_reg, offsetStack + 8), top_reg);
                callHelperX64(a, (void*)VM::jitConcat, vm_reg, 2, (uint32_t)(i + 1));
                reloadState();
                break;
            }
            case OpCode::OP_CLOSURE: {
                uint8_t constantIndex = bytecode[++i];
                size_t bytecodeOffset = i + 1;
                Value funcValue = chunk->constants()[constantIndex];
                FunctionObject* innerFunc = chunk->getFunction(funcValue.asFunctionIndex());
                i += 2 * innerFunc->upvalueCount();

                a.mov(x86::qword_ptr(co_reg, offsetStack + 8), top_reg);
                callHelperX64(a, (void*)VM::jitClosure, vm_reg, 3, (uint32_t)constantIndex, (uint32_t)bytecodeOffset);
                reloadState();
                break;
            }
            case OpCode::OP_CLOSURE_LONG: {
                uint32_t constantIndex = bytecode[++i];
                constantIndex |= (bytecode[++i] << 8);
                constantIndex |= (bytecode[++i] << 16);
                size_t bytecodeOffset = i + 1;
                Value funcValue = chunk->constants()[constantIndex];
                FunctionObject* innerFunc = chunk->getFunction(funcValue.asFunctionIndex());
                i += 2 * innerFunc->upvalueCount();

                a.mov(x86::qword_ptr(co_reg, offsetStack + 8), top_reg);
                callHelperX64(a, (void*)VM::jitClosure, vm_reg, 3, (uint32_t)constantIndex, (uint32_t)bytecodeOffset);
                reloadState();
                break;
            }
            case OpCode::OP_CALL: {
                uint8_t argCount = bytecode[++i];
                uint8_t retCount = bytecode[++i];
                a.mov(x86::qword_ptr(co_reg, offsetStack + 8), top_reg);
                callHelperX64(a, (void*)VM::jitCall, vm_reg, 4, (uint32_t)argCount, (uint32_t)retCount, (uint32_t)(i + 1));
                reloadState();
                break;
            }
            case OpCode::OP_CALL_MULTI: {
                uint8_t fixedArgCount = bytecode[++i];
                uint8_t retCount = bytecode[++i];
                a.mov(x86::qword_ptr(co_reg, offsetStack + 8), top_reg);
                callHelperX64(a, (void*)VM::jitCallMulti, vm_reg, 4, (uint32_t)fixedArgCount, (uint32_t)retCount, (uint32_t)(i + 1));
                reloadState();
                break;
            }
            case OpCode::OP_TAILCALL: {
                uint8_t argCount = bytecode[++i];
                a.mov(x86::qword_ptr(co_reg, offsetStack + 8), top_reg);
                callHelperX64(a, (void*)VM::jitTailCall, vm_reg, 3, (uint32_t)argCount, (uint32_t)(i + 1));
                a.jmp(frame_changed);
                break;
            }
            case OpCode::OP_TAILCALL_MULTI: {
                uint8_t fixedArgCount = bytecode[++i];
                a.mov(x86::qword_ptr(co_reg, offsetStack + 8), top_reg);
                callHelperX64(a, (void*)VM::jitTailCallMulti, vm_reg, 3, (uint32_t)fixedArgCount, (uint32_t)(i + 1));
                a.jmp(frame_changed);
                break;
            }
            case OpCode::OP_BAND:
            case OpCode::OP_BOR:
            case OpCode::OP_BXOR:
            case OpCode::OP_SHL:
            case OpCode::OP_SHR: {
                Label fallback = a.new_label();
                Label success = a.new_label();

                a.mov(scratch, x86::qword_ptr(top_reg, -8));   // rhs
                a.mov(scratch2, x86::qword_ptr(top_reg, -16)); // lhs

                a.mov(x86::rax, scratch2); a.shr(x86::rax, 48); a.cmp(x86::rax, 0xFFF3); a.jne(fallback);
                a.mov(x86::rax, scratch);  a.shr(x86::rax, 48); a.cmp(x86::rax, 0xFFF3); a.jne(fallback);

                a.shl(scratch2, 16); a.sar(scratch2, 16);
                a.shl(scratch, 16);  a.sar(scratch, 16);

                if (op == OpCode::OP_BAND) a.and_(scratch2, scratch);
                else if (op == OpCode::OP_BOR) a.or_(scratch2, scratch);
                else if (op == OpCode::OP_BXOR) a.xor_(scratch2, scratch);
                else if (op == OpCode::OP_SHL || op == OpCode::OP_SHR) {
                    a.cmp(scratch, 0); a.jl(fallback);
                    a.cmp(scratch, 64); a.jge(fallback);
                    a.mov(x86::rcx, scratch);
                    if (op == OpCode::OP_SHL) a.shl(scratch2, x86::cl);
                    else a.shr(scratch2, x86::cl);
                }

                a.mov(scratch, scratch2); a.shl(scratch, 16); a.sar(scratch, 16); a.cmp(scratch, scratch2); a.jne(fallback);

                a.shl(scratch2, 16); a.shr(scratch2, 16);
                a.mov(scratch, 0xFFF3000000000000ULL); a.or_(scratch2, scratch);
                a.mov(x86::qword_ptr(top_reg, -16), scratch2);
                a.sub(top_reg, 8);
                a.jmp(success);

                a.bind(fallback);
                a.mov(x86::qword_ptr(co_reg, offsetStack + 8), top_reg);
                void* helper = nullptr;
                if (op == OpCode::OP_BAND) helper = (void*)VM::jitBand;
                else if (op == OpCode::OP_BOR) helper = (void*)VM::jitBor;
                else if (op == OpCode::OP_BXOR) helper = (void*)VM::jitBxor;
                else if (op == OpCode::OP_SHL) helper = (void*)VM::jitShl;
                else if (op == OpCode::OP_SHR) helper = (void*)VM::jitShr;
                callHelperX64(a, helper, vm_reg, 2, (uint32_t)(i + 1));
                reloadState();
                a.bind(success);
                break;
            }
            case OpCode::OP_BNOT: {
                Label fallback = a.new_label();
                Label success = a.new_label();

                a.mov(scratch, x86::qword_ptr(top_reg, -8));
                a.mov(x86::rax, scratch); a.shr(x86::rax, 48); a.cmp(x86::rax, 0xFFF3); a.jne(fallback);

                a.shl(scratch, 16); a.sar(scratch, 16);
                a.not_(scratch);
                a.mov(scratch2, scratch); a.shl(scratch2, 16); a.sar(scratch2, 16); a.cmp(scratch2, scratch); a.jne(fallback);

                a.shl(scratch, 16); a.shr(scratch, 16);
                a.mov(scratch2, 0xFFF3000000000000ULL); a.or_(scratch, scratch2);
                a.mov(x86::qword_ptr(top_reg, -8), scratch);
                a.jmp(success);

                a.bind(fallback);
                a.mov(x86::qword_ptr(co_reg, offsetStack + 8), top_reg);
                callHelperX64(a, (void*)VM::jitBnot, vm_reg, 2, (uint32_t)(i + 1));
                reloadState();
                a.bind(success);
                break;
            }
            case OpCode::OP_IDIV:
            case OpCode::OP_MOD:
            case OpCode::OP_POW: {
                a.mov(x86::qword_ptr(co_reg, offsetStack + 8), top_reg);
                void* helper = nullptr;
                if (op == OpCode::OP_IDIV) helper = (void*)VM::jitIDiv;
                else if (op == OpCode::OP_MOD) helper = (void*)VM::jitMod;
                else if (op == OpCode::OP_POW) helper = (void*)VM::jitPow;
                callHelperX64(a, helper, vm_reg, 2, (uint32_t)(i + 1));
                reloadState();
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

                a.mov(scratch, x86::qword_ptr(top_reg, -8));   // rhs
                a.mov(scratch2, x86::qword_ptr(top_reg, -16)); // lhs

                x86::Gp tag1 = x86::rax;
                x86::Gp tag2 = x86::rdx;
                a.mov(tag1, scratch2); a.shr(tag1, 48);
                a.mov(tag2, scratch);  a.shr(tag2, 48);

                a.cmp(tag1, 0xFFF3);
                a.jne(float_int);
                a.cmp(tag2, 0xFFF3);
                a.jne(int_float);

                // Both integers
                if (op == OpCode::OP_DIV) {
                    a.shl(scratch2, 16); a.sar(scratch2, 16);
                    a.shl(scratch, 16);  a.sar(scratch, 16);
                    a.cvtsi2sd(x86::xmm0, scratch2);
                    a.cvtsi2sd(x86::xmm1, scratch);
                    a.divsd(x86::xmm0, x86::xmm1);
                    a.movq(scratch, x86::xmm0);
                    a.mov(x86::qword_ptr(top_reg, -16), scratch);
                    a.sub(top_reg, 8);
                    a.jmp(success);
                } else {
                    a.shl(scratch2, 16); a.sar(scratch2, 16);
                    a.shl(scratch, 16);  a.sar(scratch, 16);
                    if (op == OpCode::OP_ADD) a.add(scratch2, scratch);
                    else if (op == OpCode::OP_SUB) a.sub(scratch2, scratch);
                    else if (op == OpCode::OP_MUL) a.imul(scratch2, scratch);

                    a.mov(scratch, scratch2); a.shl(scratch, 16); a.sar(scratch, 16); a.cmp(scratch, scratch2); a.jne(fallback);
                    a.shl(scratch2, 16); a.shr(scratch2, 16);
                    a.mov(scratch, 0xFFF3000000000000ULL); a.or_(scratch2, scratch);
                    a.mov(x86::qword_ptr(top_reg, -16), scratch2);
                    a.sub(top_reg, 8);
                    a.jmp(success);
                }

                a.bind(float_int);
                a.cmp(tag1, 0xFFF1);
                a.jae(fallback);

                a.cmp(tag2, 0xFFF3);
                a.je(both_floats_rhs_int);
                a.cmp(tag2, 0xFFF1);
                a.jae(fallback);

                // Both floats
                a.movq(x86::xmm0, scratch2);
                a.movq(x86::xmm1, scratch);
                if (op == OpCode::OP_ADD) a.addsd(x86::xmm0, x86::xmm1);
                else if (op == OpCode::OP_SUB) a.subsd(x86::xmm0, x86::xmm1);
                else if (op == OpCode::OP_MUL) a.mulsd(x86::xmm0, x86::xmm1);
                else if (op == OpCode::OP_DIV) a.divsd(x86::xmm0, x86::xmm1);
                a.movq(scratch, x86::xmm0);
                a.mov(x86::qword_ptr(top_reg, -16), scratch);
                a.sub(top_reg, 8);
                a.jmp(success);

                // lhs float, rhs int
                a.bind(both_floats_rhs_int);
                a.shl(scratch, 16); a.sar(scratch, 16);
                a.cvtsi2sd(x86::xmm1, scratch);
                a.movq(x86::xmm0, scratch2);
                if (op == OpCode::OP_ADD) a.addsd(x86::xmm0, x86::xmm1);
                else if (op == OpCode::OP_SUB) a.subsd(x86::xmm0, x86::xmm1);
                else if (op == OpCode::OP_MUL) a.mulsd(x86::xmm0, x86::xmm1);
                else if (op == OpCode::OP_DIV) a.divsd(x86::xmm0, x86::xmm1);
                a.movq(scratch, x86::xmm0);
                a.mov(x86::qword_ptr(top_reg, -16), scratch);
                a.sub(top_reg, 8);
                a.jmp(success);

                // lhs int, rhs float
                a.bind(int_float);
                a.cmp(tag2, 0xFFF1);
                a.jae(fallback);

                a.shl(scratch2, 16); a.sar(scratch2, 16);
                a.cvtsi2sd(x86::xmm0, scratch2);
                a.movq(x86::xmm1, scratch);
                if (op == OpCode::OP_ADD) a.addsd(x86::xmm0, x86::xmm1);
                else if (op == OpCode::OP_SUB) a.subsd(x86::xmm0, x86::xmm1);
                else if (op == OpCode::OP_MUL) a.mulsd(x86::xmm0, x86::xmm1);
                else if (op == OpCode::OP_DIV) a.divsd(x86::xmm0, x86::xmm1);
                a.movq(scratch, x86::xmm0);
                a.mov(x86::qword_ptr(top_reg, -16), scratch);
                a.sub(top_reg, 8);
                a.jmp(success);

                a.bind(fallback);
                a.mov(x86::qword_ptr(co_reg, offsetStack + 8), top_reg);
                void* helper = nullptr;
                if (op == OpCode::OP_ADD) helper = (void*)VM::jitAdd;
                else if (op == OpCode::OP_SUB) helper = (void*)VM::jitSub;
                else if (op == OpCode::OP_MUL) helper = (void*)VM::jitMul;
                else if (op == OpCode::OP_DIV) helper = (void*)VM::jitDiv;
                callHelperX64(a, helper, vm_reg, 2, (uint32_t)(i + 1));
                reloadState();
                a.bind(success);
                break;
            }
            case OpCode::OP_NOT: {
                Label is_falsey = a.new_label();
                Label success = a.new_label();

                a.mov(scratch, x86::qword_ptr(top_reg, -8));
                a.mov(scratch2, Value::nil().bits());
                a.cmp(scratch, scratch2);
                a.je(is_falsey);
                a.mov(scratch2, Value::boolean(false).bits());
                a.cmp(scratch, scratch2);
                a.je(is_falsey);

                a.mov(scratch, Value::boolean(false).bits());
                a.mov(x86::qword_ptr(top_reg, -8), scratch);
                a.jmp(success);

                a.bind(is_falsey);
                a.mov(scratch, Value::boolean(true).bits());
                a.mov(x86::qword_ptr(top_reg, -8), scratch);
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

                a.mov(scratch, x86::qword_ptr(top_reg, -8));   // rhs
                a.mov(scratch2, x86::qword_ptr(top_reg, -16)); // lhs

                x86::Gp tag1 = x86::rax;
                x86::Gp tag2 = x86::rdx;
                a.mov(tag1, scratch2); a.shr(tag1, 48);
                a.mov(tag2, scratch);  a.shr(tag2, 48);

                if (op == OpCode::OP_EQUAL) {
                    Label not_exact = a.new_label();
                    a.cmp(scratch, scratch2);
                    a.jne(not_exact);
                    a.cmp(tag1, 0xFFF1);
                    a.jae(is_true);
                    a.bind(not_exact);
                }

                // Both integers
                a.cmp(tag1, 0xFFF3);
                a.jne(check_floats);
                a.cmp(tag2, 0xFFF3);
                a.jne(check_floats);

                a.shl(scratch2, 16); a.sar(scratch2, 16);
                a.shl(scratch, 16);  a.sar(scratch, 16);
                a.cmp(scratch2, scratch);
                if (op == OpCode::OP_EQUAL) a.je(is_true);
                else if (op == OpCode::OP_LESS) a.jl(is_true);
                else if (op == OpCode::OP_LESS_EQUAL) a.jle(is_true);
                else if (op == OpCode::OP_GREATER) a.jg(is_true);
                else if (op == OpCode::OP_GREATER_EQUAL) a.jge(is_true);
                a.jmp(is_false);

                // Both floats (< 0xFFF1)
                a.bind(check_floats);
                a.cmp(tag1, 0xFFF1);
                a.jae(fallback);
                a.cmp(tag2, 0xFFF1);
                a.jae(fallback);

                a.movq(x86::xmm0, scratch2);
                a.movq(x86::xmm1, scratch);
                a.ucomisd(x86::xmm0, x86::xmm1);
                a.jp(is_false);

                if (op == OpCode::OP_EQUAL) a.je(is_true);
                else if (op == OpCode::OP_LESS) a.jb(is_true);
                else if (op == OpCode::OP_LESS_EQUAL) a.jbe(is_true);
                else if (op == OpCode::OP_GREATER) a.ja(is_true);
                else if (op == OpCode::OP_GREATER_EQUAL) a.jae(is_true);

                a.bind(is_false);
                a.mov(scratch, Value::boolean(false).bits());
                a.mov(x86::qword_ptr(top_reg, -16), scratch);
                a.sub(top_reg, 8);
                a.jmp(success);

                a.bind(is_true);
                a.mov(scratch, Value::boolean(true).bits());
                a.mov(x86::qword_ptr(top_reg, -16), scratch);
                a.sub(top_reg, 8);
                a.jmp(success);

                a.bind(fallback);
                a.mov(x86::qword_ptr(co_reg, offsetStack + 8), top_reg);
                a.mov(x86::rax, (uint64_t)start_i);
                a.jmp(epilogue);

                a.bind(success);
                break;
            }
            case OpCode::OP_RETURN:
            case OpCode::OP_RETURN_VALUE:
            case OpCode::OP_RETURN_VALUE_MULTI: {
                if (op != OpCode::OP_RETURN) {
                    uint8_t count = bytecode[++i];
                    (void)count;
                }
                a.mov(x86::qword_ptr(co_reg, offsetStack + 8), top_reg);
                a.mov(x86::rax, (uint64_t)start_i);
                a.jmp(epilogue);
                break;
            }
            default: {
                return false;
            }
        }
    }

    // Bind any remaining labels
    for (size_t j = 0; j < labels.size(); j++) {
        if (!boundLabels[j]) {
            a.bind(labels[j]);
        }
    }

    a.mov(x86::qword_ptr(co_reg, offsetStack + 8), top_reg);
    a.mov(x86::rax, -1);
    a.jmp(epilogue);

    a.bind(frame_changed);
    a.mov(x86::qword_ptr(co_reg, offsetStack + 8), top_reg);
    a.mov(x86::rax, -2);

    a.bind(epilogue);
    a.add(x86::rsp, 40);
    a.pop(x86::r15);
    a.pop(x86::r14);
    a.pop(x86::r13);
    a.pop(x86::r12);
    a.pop(x86::rbx);
    a.pop(x86::rbp);
    a.ret();

    return true;
}

JITFunc JITCompiler::compileX64(FunctionObject* function) {
#if !(defined(__x86_64__) || defined(_M_X64))
    (void)function;
    return nullptr;
#else
    if (function->getJITCode()) return function->getJITCode();

    CodeHolder code;
    code.init(rt_.environment());

    if (!assembleX64(function, code)) {
        return nullptr;
    }

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
#endif
}

#endif
