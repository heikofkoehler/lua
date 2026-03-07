#include "vm/jit.hpp"
#include "value/closure.hpp"
#include "value/coroutine.hpp"
#include <iostream>
#include <vector>
#include <cstddef>

#ifdef USE_JIT

using namespace asmjit;

JITCompiler::JITCompiler(VM* vm) : vm_(vm) { (void)vm_; }
JITCompiler::~JITCompiler() {}

JITFunc JITCompiler::compile(FunctionObject* function) {
    if (function->getJITCode()) return function->getJITCode();

    // Debug offsets
    size_t offsetCurrentCoroutine = (size_t)&(((VM*)0)->currentCoroutine_);
    size_t offsetStack = (size_t)&(((CoroutineObject*)0)->stack);
    size_t offsetFrames = (size_t)&(((CoroutineObject*)0)->frames);
    size_t offsetStackBase = (size_t)&(((CallFrame*)0)->stackBase);

    (void)vm_; // Suppress unused warning

    CodeHolder code;
    code.init(rt_.environment());
    a64::Assembler a(&code);

    // Registers:
    // X0: VM* vm (input)
    // X1: CoroutineObject* co
    // X2: Value* stack_begin
    // X3: CallFrame* frame
    // X4: Value* local_base (stackBase)
    // X5: Value* top_reg
    // X6-X7: Scratch
    
    a64::Gp vm_reg = a64::x0;
    a64::Gp co_reg = a64::x1;
    a64::Gp stack_reg = a64::x2;
    a64::Gp frame_reg = a64::x3;
    a64::Gp local_reg = a64::x4;
    a64::Gp top_reg = a64::x5;
    
    a64::Gp scratch = a64::x6;
    a64::Gp scratch2 = a64::x7;

    // Prologue: Load VM state
    a.ldr(co_reg, a64::ptr(vm_reg, offsetCurrentCoroutine));
    a.ldr(stack_reg, a64::ptr(co_reg, offsetStack)); // co->stack.data()
    a.ldr(top_reg, a64::ptr(co_reg, offsetStack + 8)); // co->stack.top() (actually end pointer)
    
    // Load frame = co->frames.back()
    a.ldr(scratch, a64::ptr(co_reg, offsetFrames + 8)); // __end_ of frames
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
                
                // Compare with nil
                a.mov(scratch2, Value::nil().bits()); 
                a.cmp(scratch, scratch2);
                a.b_eq(labels[next_ip + offset]);
                
                // Compare with false
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
            default: {
                // Unsupported opcode, fall back to interpreter
                a.str(top_reg, a64::ptr(co_reg, offsetStack + 8));
                a.mov(a64::x0, (uint64_t)i); // Return current IP to the interpreter
                a.ret(a64::x30);
                goto compilation_done;
            }
        }
    }

compilation_done:
    // Bind the EOF label
    a.bind(labels[bytecode.size()]);
    boundLabels[bytecode.size()] = true;

    // Bind any remaining labels to prevent asmjit errors
    for (size_t j = 0; j < labels.size(); j++) {
        if (!boundLabels[j]) {
            a.bind(labels[j]);
        }
    }

    a.str(top_reg, a64::ptr(co_reg, offsetStack + 8));
    a.mov(a64::x0, -1);
    a.ret(a64::x30);

    JITFunc fn;
    Error err = rt_.add(&fn, &code);
    if (err != kErrorOk) {
        return nullptr;
    }

    function->setJITCode(fn);
    return fn;
}

#endif
