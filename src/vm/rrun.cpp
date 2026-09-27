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
#include <cmath>

bool VM::runRegister(size_t targetFrameCount) {
    while (true) {
next_frame:
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
                case ROpCode::ROP_IDIV: {
                    Value b = R[B];
                    Value c = R[C];
                    if (b.isNumber() && c.isNumber()) {
                        double res = b.asNumber() / c.asNumber();
                        // Floor division
                        R[A] = Value::number(std::floor(res));
                    } else {
                        hadError_ = true;
                        return false;
                    }
                    break;
                }
                case ROpCode::ROP_MOD: {
                    Value b = R[B];
                    Value c = R[C];
                    if (b.isNumber() && c.isNumber()) {
                        double bn = b.asNumber();
                        double cn = c.asNumber();
                        // Lua modulo: bn - floor(bn/cn)*cn
                        R[A] = Value::number(bn - std::floor(bn/cn)*cn);
                    } else {
                        hadError_ = true;
                        return false;
                    }
                    break;
                }
                case ROpCode::ROP_POW: {
                    Value b = R[B];
                    Value c = R[C];
                    if (b.isNumber() && c.isNumber()) {
                        R[A] = Value::number(std::pow(b.asNumber(), c.asNumber()));
                    } else {
                        hadError_ = true;
                        return false;
                    }
                    break;
                }
                case ROpCode::ROP_NEG: {
                    // R(A) = -R(B)
                    Value b = R[B];
                    if (b.isNumber()) {
                        R[A] = Value::number(-b.asNumber());
                    } else {
                        hadError_ = true;
                        return false;
                    }
                    break;
                }
                case ROpCode::ROP_NOT: {
                    // R(A) = not R(B)
                    Value b = R[B];
                    bool truthy = !b.isNil() && !(b.isBool() && !b.asBool());
                    R[A] = Value::boolean(!truthy);
                    break;
                }
                case ROpCode::ROP_LEN: {
                    // R(A) = #R(B)
                    // TODO: implement for strings, tables
                    hadError_ = true;
                    return false;
                }
                case ROpCode::ROP_BAND: {
                    // TODO: integer bitwise ops
                    hadError_ = true;
                    return false;
                }
                case ROpCode::ROP_BOR: {
                    hadError_ = true;
                    return false;
                }
                case ROpCode::ROP_BXOR: {
                    hadError_ = true;
                    return false;
                }
                case ROpCode::ROP_SHL: {
                    hadError_ = true;
                    return false;
                }
                case ROpCode::ROP_SHR: {
                    hadError_ = true;
                    return false;
                }
                case ROpCode::ROP_BNOT: {
                    hadError_ = true;
                    return false;
                }
                case ROpCode::ROP_CONCAT: {
                    // R(A) = R(B) .. R(C)
                    // TODO: implement string concatenation
                    hadError_ = true;
                    return false;
                }
                case ROpCode::ROP_NEWTABLE: {
                    // R(A) = new table (B = array hint, C = hash hint)
                    TableObject* table = createTable();
                    R[A] = Value::table(table);
                    break;
                }
                case ROpCode::ROP_GETTABLE: {
                    // R(A) = R(B)[R(C)]
                    Value tableVal = R[B];
                    Value key = R[C];
                    // Use the same logic as OP_GET_TABLE (simplified for now)
                    if (tableVal.isTable()) {
                        TableObject* table = tableVal.asTableObj();
                        Value result = table->get(key);
                        R[A] = result;
                    } else {
                        // TODO: metamethod __index
                        hadError_ = true;
                        return false;
                    }
                    break;
                }
                case ROpCode::ROP_SETTABLE: {
                    // R(A)[R(B)] = R(C)
                    Value tableVal = R[A];
                    Value key = R[B];
                    Value val = R[C];
                    if (tableVal.isTable()) {
                        TableObject* table = tableVal.asTableObj();
                        table->set(key, val);
                    } else {
                        // TODO: metamethod __newindex
                        hadError_ = true;
                        return false;
                    }
                    break;
                }
                case ROpCode::ROP_SETTABLEMULTI: {
                    // Multi-value table set
                    // TODO
                    hadError_ = true;
                    return false;
                }
                case ROpCode::ROP_GETUPVAL: {
                    // R(A) = Up[B]
                    UpvalueObject* upvalue = frame.closure->getUpvalueObj(B);
                    if (upvalue) {
                        R[A] = upvalue->get(currentCoroutine_->stack);
                    } else {
                        R[A] = Value::nil();
                    }
                    break;
                }
                case ROpCode::ROP_SETUPVAL: {
                    // Up[B] = R(A)
                    UpvalueObject* upvalue = frame.closure->getUpvalueObj(B);
                    if (upvalue) {
                        upvalue->set(currentCoroutine_->stack, R[A]);
                    }
                    break;
                }
                case ROpCode::ROP_GETTABUP: {
                    // R(A) = Up[B][K[C]]  (_ENV global lookup)
                    UpvalueObject* upvalue = frame.closure->getUpvalueObj(B);
                    if (!upvalue) {
                        R[A] = Value::nil();
                        break;
                    }
                    Value upTable = upvalue->get(currentCoroutine_->stack);
                    Value key = constants[C];
                    if (upTable.isTable()) {
                        TableObject* table = upTable.asTableObj();
                        R[A] = table->get(key);
                    } else {
                        R[A] = Value::nil();
                    }
                    break;
                }
                case ROpCode::ROP_SETTABUP: {
                    // Up[B][K[C]] = R(A)
                    UpvalueObject* upvalue = frame.closure->getUpvalueObj(B);
                    if (!upvalue) break;
                    Value upTable = upvalue->get(currentCoroutine_->stack);
                    Value key = constants[C];
                    if (upTable.isTable()) {
                        TableObject* table = upTable.asTableObj();
                        table->set(key, R[A]);
                    }
                    break;
                }
                case ROpCode::ROP_DEFGLOBAL: {
                    // Up[B][K[C]] = R(A), error if already defined
                    // TODO
                    hadError_ = true;
                    return false;
                }
                case ROpCode::ROP_CALL: {
                    // R(A)..R(A+C-2) = R(A)(R(A+1)..R(A+B-1))
                    // B = arg count + 1 (0 = multires), C = ret count + 1 (0 = multires)
                    // For now: only support fixed arg/ret counts, no multires
                    int argCount = B - 1;
                    int retCount = C - 1;
                    if (B == 0 || C == 0) {
                        // Multires not yet supported
                        hadError_ = true;
                        return false;
                    }
                    
                    // Copy function and args to stack top for callValue
                    // R(A) is function, R(A+1)..R(A+argCount) are args
                    // Note: R points into stack; we need to push copies
                    Value funcVal = R[A];
                    std::vector<Value> args;
                    for (int i = 0; i < argCount; i++) {
                        args.push_back(R[A + 1 + i]);
                    }
                    
                    // Push function and args onto stack
                    currentCoroutine_->stack.push_back(funcVal);
                    for (auto& arg : args) {
                        currentCoroutine_->stack.push_back(arg);
                    }
                    
                    size_t prevFrames = currentCoroutine_->frames.size();
                    if (!callValue(argCount, retCount)) {
                        return false;
                    }
                    
                    // If a new Lua frame was pushed, break to outer loop to execute it
                    // (it may have rcode or not; outer loop handles dispatch)
                    if (currentCoroutine_->frames.size() > prevFrames) {
                        // Copy results? No, callee hasn't run yet.
                        // Break inner loop; outer loop will pick up new frame.
                        // But outer loop expects rcode; if callee has no rcode, we need to return.
                        CallFrame& newFrame = currentCoroutine_->frames.back();
                        FunctionObject* newFunc = newFrame.closure->function();
                        if (!newFunc->chunk()->hasRCode()) {
                            // Callee has no register code; return to VM::run() for dispatch
                            // Results will be on stack; caller needs to copy them to registers
                            // For now, this is a limitation.
                            hadError_ = true;
                            return false;
                        }
                        // New frame has rcode; break inner loop to execute it
                        goto next_frame;
                    }
                    
                    // No new frame (C function); results are on stack top
                    // Copy results back to R(A)..
                    // retCount results are on stack top
                    size_t stackSize = currentCoroutine_->stack.size();
                    for (int i = 0; i < retCount; i++) {
                        R[A + i] = currentCoroutine_->stack[stackSize - retCount + i];
                    }
                    // Pop results from stack
                    for (int i = 0; i < retCount; i++) {
                        currentCoroutine_->stack.pop_back();
                    }
                    break;
                }
                case ROpCode::ROP_TAILCALL: {
                    // return R(A)(R(A+1)..R(A+B-1))
                    // TODO: tail call (reuse frame)
                    hadError_ = true;
                    return false;
                }
                case ROpCode::ROP_CLOSURE: {
                    // R(A) = closure(K[Bx]), followed by B pseudo-instructions
                    // B = upvalue count, Bx = constant index of function
                    // For now: only support functions with no upvalues
                    if (B != 0) {
                        // Has upvalues; need to read pseudo-instructions
                        hadError_ = true;
                        return false;
                    }
                    Value funcValue = constants[Bx];
                    size_t funcIndex = funcValue.asFunctionIndex();
                    FunctionObject* function = chunk->getFunction(funcIndex);
                    
                    ClosureObject* closure = createClosure(function);
                    R[A] = Value::closure(closure);
                    break;
                }
                case ROpCode::ROP_VARARG: {
                    // R(A)..R(A+B-2) = varargs (B=1: no values)
                    // TODO
                    hadError_ = true;
                    return false;
                }
                case ROpCode::ROP_YIELD: {
                    // yield R(A)..R(A+B-2)
                    // TODO: coroutine yield
                    hadError_ = true;
                    return false;
                }
                case ROpCode::ROP_PACKVARARG: {
                    // R(A) = table.pack(varargs)
                    // TODO
                    hadError_ = true;
                    return false;
                }
                case ROpCode::ROP_VARARGITEM: {
                    // R(A) = varargs[R(B)]
                    // TODO
                    hadError_ = true;
                    return false;
                }
                case ROpCode::ROP_VARARGCOUNT: {
                    // R(A) = #varargs
                    // TODO
                    hadError_ = true;
                    return false;
                }
                case ROpCode::ROP_FORPREP: {
                    // Numeric for: R(A)=index, R(A+1)=limit, R(A+2)=step; pc += sBx
                    // TODO
                    hadError_ = true;
                    return false;
                }
                case ROpCode::ROP_FORLOOP: {
                    // TODO
                    hadError_ = true;
                    return false;
                }
                case ROpCode::ROP_CLOSE: {
                    // Close upvalues / to-be-closed variables with register >= A
                    // TODO
                    hadError_ = true;
                    return false;
                }
                case ROpCode::ROP_JMP: {
                    // pc += sBx
                    pc += sBx;
                    break;
                }
                case ROpCode::ROP_EQ: {
                    // if ((R(B) == R(C)) != A) pc++
                    // A=0: skip if false, A=1: skip if true
                    Value b = R[B];
                    Value c = R[C];
                    bool eq = (b == c);  // Uses Value::operator==
                    if (eq != (A != 0)) pc++;
                    break;
                }
                case ROpCode::ROP_LT: {
                    // if ((R(B) < R(C)) != A) pc++
                    Value b = R[B];
                    Value c = R[C];
                    bool lt = false;
                    if (b.isNumber() && c.isNumber()) {
                        lt = b.asNumber() < c.asNumber();
                    } else {
                        // TODO: metamethod, string comparison
                        hadError_ = true;
                        return false;
                    }
                    if (lt != (A != 0)) pc++;
                    break;
                }
                case ROpCode::ROP_LE: {
                    // if ((R(B) <= R(C)) != A) pc++
                    Value b = R[B];
                    Value c = R[C];
                    bool le = false;
                    if (b.isNumber() && c.isNumber()) {
                        le = b.asNumber() <= c.asNumber();
                    } else {
                        hadError_ = true;
                        return false;
                    }
                    if (le != (A != 0)) pc++;
                    break;
                }
                case ROpCode::ROP_TEST: {
                    // if (truthy(R(A)) != C) pc++
                    Value a = R[A];
                    bool truthy = !a.isNil() && !(a.isBool() && !a.asBool());
                    if (truthy != (C != 0)) pc++;
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
