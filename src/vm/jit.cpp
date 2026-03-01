#include "vm/jit.hpp"
#include "value/closure.hpp"
#include "value/coroutine.hpp"
#include <iostream>
#include <vector>
#include <cstddef>

#ifdef USE_JIT

using namespace asmjit;

JITCompiler::JITCompiler(VM* vm) : vm_(vm) {}
JITCompiler::~JITCompiler() {}

JITFunc JITCompiler::compile(FunctionObject* function) {
    if (function->getJITCode()) return function->getJITCode();

    // Debug offsets
    size_t offsetCurrentCoroutine = (size_t)&(((VM*)0)->currentCoroutine_);
    size_t offsetStack = (size_t)&(((CoroutineObject*)0)->stack);
    size_t offsetFrames = (size_t)&(((CoroutineObject*)0)->frames);
    size_t offsetStackBase = (size_t)&(((CallFrame*)0)->stackBase);
    size_t offsetLastResultCount = (size_t)&(((CoroutineObject*)0)->lastResultCount);
    size_t offsetStatus = (size_t)&(((CoroutineObject*)0)->status);

    (void)vm_; // Suppress unused warning

    CodeHolder code;
    code.init(rt_.environment());
    a64::Assembler a(&code);

    // Registers:
    // X0: VM* vm (input)
    // X1: CoroutineObject* co
    // X2: Value* stack_begin
    // X3: CallFrame* frame
    // X4: Value* stack_top (current finish pointer)
    // X5: Value* local_base
    // X6, X7: Scratch (Gp)
    // X8, X9: Internal Scratch (Gp)
    // X10: Current IP (Gp)
    // D0, D1: Scratch (Fp)

    a64::Gp vm_reg = a64::x0;
    a64::Gp co_reg = a64::x1;
    a64::Gp stack_reg = a64::x2;
    a64::Gp frame_reg = a64::x3;
    a64::Gp top_reg = a64::x4;
    a64::Gp local_reg = a64::x5;
    a64::Gp scratch = a64::x6;
    a64::Gp scratch2 = a64::x7;
    a64::Gp ip_reg = a64::x10;

    // Load co = vm->currentCoroutine_
    a.ldr(co_reg, a64::ptr(vm_reg, offsetCurrentCoroutine));

    // Load stack pointers
    a.ldr(stack_reg, a64::ptr(co_reg, offsetStack));     // __begin_
    a.ldr(top_reg, a64::ptr(co_reg, offsetStack + 8));   // __end_ (top)

    // Load frame = co->frames.back()
    a.ldr(scratch, a64::ptr(co_reg, offsetFrames + 8)); // __end_ of frames
    a.sub(frame_reg, scratch, sizeof(CallFrame));

    // Load stackBase and calc local_base
    a.ldr(scratch, a64::ptr(frame_reg, offsetStackBase));
    a.add(local_reg, stack_reg, scratch, a64::lsl(3));

    auto emitUnpackDouble = [&](a64::Gp val, a64::Vec dest) {
        a.fmov(dest, val);
    };

    // Bytecode loop
    Chunk* chunk = function->chunk();
    const std::vector<uint8_t>& bytecode = chunk->code();
    std::vector<Label> labels;
    for (size_t i = 0; i < bytecode.size(); i++) {
        labels.push_back(a.new_label());
    }

    size_t ip = 0;
    while (ip < bytecode.size()) {
        size_t current_ip = ip;
        a.bind(labels[current_ip]);
        a.mov(ip_reg, (uint64_t)current_ip);
        OpCode op = static_cast<OpCode>(bytecode[ip++]);

        switch (op) {
            case OpCode::OP_CONSTANT: {
                uint8_t index = bytecode[ip++];
                Value val = chunk->constants()[index];
                a.mov(scratch, val.bits());
                a.str(scratch, a64::ptr(top_reg));
                a.add(top_reg, top_reg, 8);
                break;
            }
            case OpCode::OP_GET_LOCAL: {
                uint8_t slot = bytecode[ip++];
                a.ldr(scratch, a64::ptr(local_reg, slot * 8));
                a.str(scratch, a64::ptr(top_reg));
                a.add(top_reg, top_reg, 8);
                break;
            }
            case OpCode::OP_SET_LOCAL: {
                uint8_t slot = bytecode[ip++];
                a.ldr(scratch, a64::ptr(top_reg, -8));
                a.str(scratch, a64::ptr(local_reg, slot * 8));
                break;
            }
            case OpCode::OP_POP: {
                a.sub(top_reg, top_reg, 8);
                break;
            }
            case OpCode::OP_ADD: {
                a.sub(top_reg, top_reg, 8);
                a.ldr(scratch2, a64::ptr(top_reg)); // b
                a.sub(top_reg, top_reg, 8);
                a.ldr(scratch, a64::ptr(top_reg)); // a
                
                emitUnpackDouble(scratch, a64::d0);
                emitUnpackDouble(scratch2, a64::d1);
                a.fadd(a64::d0, a64::d0, a64::d1);
                a.fmov(scratch, a64::d0);
                
                a.str(scratch, a64::ptr(top_reg));
                a.add(top_reg, top_reg, 8);
                break;
            }
            case OpCode::OP_SUB: {
                a.sub(top_reg, top_reg, 8);
                a.ldr(scratch2, a64::ptr(top_reg)); // b
                a.sub(top_reg, top_reg, 8);
                a.ldr(scratch, a64::ptr(top_reg)); // a
                
                emitUnpackDouble(scratch, a64::d0);
                emitUnpackDouble(scratch2, a64::d1);
                a.fsub(a64::d0, a64::d0, a64::d1);
                a.fmov(scratch, a64::d0);
                
                a.str(scratch, a64::ptr(top_reg));
                a.add(top_reg, top_reg, 8);
                break;
            }
            case OpCode::OP_LESS_EQUAL: {
                a.sub(top_reg, top_reg, 8);
                a.ldr(scratch2, a64::ptr(top_reg)); // b
                a.sub(top_reg, top_reg, 8);
                a.ldr(scratch, a64::ptr(top_reg)); // a
                
                emitUnpackDouble(scratch, a64::d0);
                emitUnpackDouble(scratch2, a64::d1);
                a.fcmp(a64::d0, a64::d1);
                
                a.mov(scratch, 0x7FF2000000000001ULL); // true
                a.mov(scratch2, 0x7FF2000000000000ULL); // false
                a.csel(scratch, scratch, scratch2, a64::CondCode::kLE); 
                
                a.str(scratch, a64::ptr(top_reg));
                a.add(top_reg, top_reg, 8);
                break;
            }
            case OpCode::OP_GREATER_EQUAL: {
                a.sub(top_reg, top_reg, 8);
                a.ldr(scratch2, a64::ptr(top_reg)); // b
                a.sub(top_reg, top_reg, 8);
                a.ldr(scratch, a64::ptr(top_reg)); // a
                
                emitUnpackDouble(scratch, a64::d0);
                emitUnpackDouble(scratch2, a64::d1);
                a.fcmp(a64::d0, a64::d1);
                
                a.mov(scratch, 0x7FF2000000000001ULL); // true
                a.mov(scratch2, 0x7FF2000000000000ULL); // false
                a.csel(scratch, scratch, scratch2, a64::CondCode::kGE); 
                
                a.str(scratch, a64::ptr(top_reg));
                a.add(top_reg, top_reg, 8);
                break;
            }
            case OpCode::OP_JUMP: {
                uint16_t offset = bytecode[ip] | (bytecode[ip+1] << 8);
                ip += 2;
                a.b(labels[ip + offset]);
                break;
            }
            case OpCode::OP_LOOP: {
                uint16_t offset = bytecode[ip] | (bytecode[ip+1] << 8);
                ip += 2;
                a.b(labels[ip - offset]);
                break;
            }
            case OpCode::OP_JUMP_IF_FALSE: {
                uint16_t offset = bytecode[ip] | (bytecode[ip+1] << 8);
                ip += 2;
                a.ldr(scratch, a64::ptr(top_reg, -8));
                a.mov(scratch2, 0x7FF1000000000000ULL); 
                a.cmp(scratch, scratch2);
                a.b_eq(labels[ip + offset]);
                a.mov(scratch2, 0x7FF2000000000000ULL);
                a.cmp(scratch, scratch2);
                a.b_eq(labels[ip + offset]);
                break;
            }
            case OpCode::OP_RETURN_VALUE: {
                uint8_t count = bytecode[ip++];
                if (count != 1) return nullptr; 
                
                a.ldr(scratch, a64::ptr(top_reg, -8));
                a.sub(top_reg, local_reg, 8);
                a.str(scratch, a64::ptr(top_reg));
                a.add(top_reg, top_reg, 8);
                
                a.str(top_reg, a64::ptr(co_reg, offsetStack + 8));
                
                a.mov(scratch2, 1);
                a.str(scratch2, a64::ptr(co_reg, offsetLastResultCount));
                
                a.str(frame_reg, a64::ptr(co_reg, offsetFrames + 8));
                
                a.ldr(scratch2, a64::ptr(co_reg, offsetFrames));
                a.cmp(frame_reg, scratch2);
                Label not_dead = a.new_label();
                a.b_ne(not_dead);
                a.mov(scratch2, (int)CoroutineObject::Status::DEAD);
                a.str(scratch2, a64::ptr(co_reg, offsetStatus));
                a.bind(not_dead);

                a.mov(a64::x0, -1);
                a.ret(a64::x30);
                break;
            }
            case OpCode::OP_RETURN: {
                a.sub(top_reg, local_reg, 8);
                a.mov(scratch, Value::nil().bits());
                a.str(scratch, a64::ptr(top_reg));
                a.add(top_reg, top_reg, 8);

                a.str(top_reg, a64::ptr(co_reg, offsetStack + 8));
                
                a.mov(scratch2, 1);
                a.str(scratch2, a64::ptr(co_reg, offsetLastResultCount));
                
                a.str(frame_reg, a64::ptr(co_reg, offsetFrames + 8));
                
                a.ldr(scratch2, a64::ptr(co_reg, offsetFrames));
                a.cmp(frame_reg, scratch2);
                Label not_dead = a.new_label();
                a.b_ne(not_dead);
                a.mov(scratch2, (int)CoroutineObject::Status::DEAD);
                a.str(scratch2, a64::ptr(co_reg, offsetStatus));
                a.bind(not_dead);

                a.mov(a64::x0, -1);
                a.ret(a64::x30);
                break;
            }
            case OpCode::OP_NIL: {
                a.mov(scratch, Value::nil().bits());
                a.str(scratch, a64::ptr(top_reg));
                a.add(top_reg, top_reg, 8);
                break;
            }
            default: {
                return nullptr;
            }
        }
    }

    a.str(top_reg, a64::ptr(co_reg, offsetStack + 8));
    a.mov(a64::x0, -1);
    a.ret(a64::x30);

    JITFunc fn;
    Error err = rt_.add(&fn, &code);
    if (err != kErrorOk) return nullptr;

    function->setJITCode(fn);
    printf("DEBUG: Compiled function %s to JIT\n", function->name().c_str());
    return fn;
}

#endif
