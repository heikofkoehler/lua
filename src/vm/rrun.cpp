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
        size_t pc = frame.ip;  // Restore pc from frame (0 for new frames)
        
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
                    Value b = R[B];
                    Value c = R[C];
                    if (b.isInteger() && c.isInteger()) {
                        R[A] = makeInteger(b.asInteger() + c.asInteger());
                    } else if (b.isNumber() && c.isNumber()) {
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
                    if (b.isInteger() && c.isInteger()) {
                        R[A] = makeInteger(b.asInteger() - c.asInteger());
                    } else if (b.isNumber() && c.isNumber()) {
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
                    if (b.isInteger() && c.isInteger()) {
                        R[A] = makeInteger(b.asInteger() * c.asInteger());
                    } else if (b.isNumber() && c.isNumber()) {
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
                    Value b = R[B];
                    if (b.isString()) {
                        R[A] = Value::integer(static_cast<int64_t>(getStringValue(b).length()));
                    } else if (b.isTable()) {
                        Value mm = getMetamethod(b, "__len");
                        if (!mm.isNil()) {
                            // Call metamethod: result on stack top
                            currentCoroutine_->stack.push_back(mm);
                            currentCoroutine_->stack.push_back(b);
                            if (!callValue(1, 2, false, "len")) return false;
                            R[A] = pop();
                        } else {
                            R[A] = Value::integer(static_cast<int64_t>(b.asTableObj()->length()));
                        }
                    } else {
                        Value mm = getMetamethod(b, "__len");
                        if (!mm.isNil()) {
                            currentCoroutine_->stack.push_back(mm);
                            currentCoroutine_->stack.push_back(b);
                            if (!callValue(1, 2, false, "len")) return false;
                            R[A] = pop();
                        } else {
                            runtimeError("attempt to get length of a " + typeName(b) + " value");
                            return false;
                        }
                    }
                    break;
                }
                case ROpCode::ROP_BAND: {
                    // R(A) = R(B) & R(C)
                    Value b = R[B], c = R[C];
                    int64_t ib, ic;
                    if (toIntegerNoString(b, ib) && toIntegerNoString(c, ic)) {
                        R[A] = makeInteger(ib & ic);
                    } else if (!callBinaryMetamethod(b, c, "__band")) {
                        if (hadError_) return false;
                        runtimeError("attempt to perform bitwise operation on a non-integer value");
                        return false;
                    } else { R[A] = pop(); }
                    break;
                }
                case ROpCode::ROP_BOR: {
                    Value b = R[B], c = R[C];
                    int64_t ib, ic;
                    if (toIntegerNoString(b, ib) && toIntegerNoString(c, ic)) {
                        R[A] = makeInteger(ib | ic);
                    } else if (!callBinaryMetamethod(b, c, "__bor")) {
                        if (hadError_) return false;
                        runtimeError("attempt to perform bitwise operation on a non-integer value");
                        return false;
                    } else { R[A] = pop(); }
                    break;
                }
                case ROpCode::ROP_BXOR: {
                    Value b = R[B], c = R[C];
                    int64_t ib, ic;
                    if (toIntegerNoString(b, ib) && toIntegerNoString(c, ic)) {
                        R[A] = makeInteger(ib ^ ic);
                    } else if (!callBinaryMetamethod(b, c, "__bxor")) {
                        if (hadError_) return false;
                        runtimeError("attempt to perform bitwise operation on a non-integer value");
                        return false;
                    } else { R[A] = pop(); }
                    break;
                }
                case ROpCode::ROP_SHL: {
                    Value b = R[B], c = R[C];
                    int64_t ib, ic;
                    if (toIntegerNoString(b, ib) && toIntegerNoString(c, ic)) {
                        uint64_t shift = static_cast<uint64_t>(ic) & 63;
                        R[A] = makeInteger(static_cast<int64_t>(static_cast<uint64_t>(ib) << shift));
                    } else if (!callBinaryMetamethod(b, c, "__shl")) {
                        if (hadError_) return false;
                        runtimeError("attempt to perform bitwise operation on a non-integer value");
                        return false;
                    } else { R[A] = pop(); }
                    break;
                }
                case ROpCode::ROP_SHR: {
                    Value b = R[B], c = R[C];
                    int64_t ib, ic;
                    if (toIntegerNoString(b, ib) && toIntegerNoString(c, ic)) {
                        uint64_t shift = static_cast<uint64_t>(ic) & 63;
                        R[A] = makeInteger(static_cast<int64_t>(static_cast<uint64_t>(ib) >> shift));
                    } else if (!callBinaryMetamethod(b, c, "__shr")) {
                        if (hadError_) return false;
                        runtimeError("attempt to perform bitwise operation on a non-integer value");
                        return false;
                    } else { R[A] = pop(); }
                    break;
                }
                case ROpCode::ROP_BNOT: {
                    // R(A) = ~R(B)
                    Value b = R[B];
                    int64_t ib;
                    if (toIntegerNoString(b, ib)) {
                        R[A] = makeInteger(~ib);
                    } else {
                        Value mm = getMetamethod(b, "__bnot");
                        if (mm.isNil()) {
                            runtimeError("attempt to perform bitwise operation on a non-integer value");
                            return false;
                        }
                        currentCoroutine_->stack.push_back(mm);
                        currentCoroutine_->stack.push_back(b);
                        if (!callValue(1, 2, false, "bnot")) return false;
                        R[A] = pop();
                    }
                    break;
                }
                case ROpCode::ROP_CONCAT: {
                    // R(A) = R(B) .. R(C)
                    Value b = R[B];
                    Value c = R[C];
                    if ((b.isString() || b.isNumber()) && (c.isString() || c.isNumber())) {
                        R[A] = concat(b, c);
                    } else if (!callBinaryMetamethod(b, c, "__concat")) {
                        if (hadError_) return false;
                        Value badVal = !(b.isString() || b.isNumber()) ? b : c;
                        runtimeError("attempt to concatenate a " + typeName(badVal) + " value");
                        return false;
                    } else {
                        // Metamethod succeeded; result is on stack top
                        R[A] = pop();
                    }
                    break;
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
                    // Re-establish R in case stack reallocated during previous call
                    R = currentCoroutine_->stack.data() + base;
                    bool isMultiArg = (B == 0);
                    bool isMultiRet = (C == 0);
                    // For multires args: fixed=0 for now (TODO: side table for fixed count)
                    // Args are the lastResultCount values starting at R(A+1)
                    int argCount = isMultiArg ? static_cast<int>(currentCoroutine_->lastResultCount) : (B - 1);
                    // For multires returns: use 0 to signal "all results" to callValue
                    int retCount = isMultiRet ? 0 : (C - 1);
                    
                    // Copy function and args to stack top for callValue
                    // R(A) is function, R(A+1)..R(A+argCount) are args
                    // Note: R points into stack; we need to push copies
                    Value funcVal = R[A];
                    std::vector<Value> args;
                    for (int i = 0; i < argCount; i++) {
                        args.push_back(R[A + 1 + i]);
                    }
                    
                    // Push function and args onto stack
                    // Record base for multires result counting
                    size_t stackBaseBefore = currentCoroutine_->stack.size();
                    currentCoroutine_->stack.push_back(funcVal);
                    for (auto& arg : args) {
                        currentCoroutine_->stack.push_back(arg);
                    }
                    
                    // Save pc in caller frame before callValue (which may reallocate frames)
                    frame.ip = pc;
                    
                    size_t prevFrames = currentCoroutine_->frames.size();
                    if (!callValue(argCount, retCount)) {
                        return false;
                    }
                    
                    // If a new Lua frame was pushed, set its register destination
                    // (for ROP_RETURN to know where to put results in caller)
                    if (currentCoroutine_->frames.size() > prevFrames) {
                        CallFrame& newFrame = currentCoroutine_->frames.back();
                        // Destination is R(A) in the caller; store absolute stack index
                        // Caller base = base (current frame's stackBase), dest = base + A
                        newFrame.regDest = static_cast<int>(base + A);
                        
                        // Check if callee has rcode
                        FunctionObject* newFunc = newFrame.closure->function();
                        if (!newFunc->chunk()->hasRCode()) {
                            hadError_ = true;
                            return false;
                        }
                        goto next_frame;
                    }
                    
                    // No new frame (C function); results are on stack top
                    // Copy results back to R(A)..
                    // Re-establish R after potential stack reallocation
                    R = currentCoroutine_->stack.data() + base;
                    int actualRetCount = retCount;
                    if (isMultiRet) {
                        // Multires: count results from stack
                        actualRetCount = static_cast<int>(currentCoroutine_->stack.size() - stackBaseBefore);
                        currentCoroutine_->lastResultCount = static_cast<size_t>(actualRetCount);
                    }
                    size_t stackSize = currentCoroutine_->stack.size();
                    for (int i = 0; i < actualRetCount; i++) {
                        R[A + i] = currentCoroutine_->stack[stackSize - actualRetCount + i];
                    }
                    // Pop results from stack
                    for (int i = 0; i < actualRetCount; i++) {
                        currentCoroutine_->stack.pop_back();
                    }
                    break;
                }
                case ROpCode::ROP_TAILCALL: {
                    // return R(A)(R(A+1)..R(A+B-1))
                    // For now: treat as regular call (no frame reuse optimization)
                    // TODO: implement proper tail call (reuse frame)
                    int argCount = B - 1;
                    if (B == 0) {
                        hadError_ = true;
                        return false;
                    }
                    // Copy function and args to stack top
                    Value funcVal = R[A];
                    std::vector<Value> args;
                    for (int i = 0; i < argCount; i++) {
                        args.push_back(R[A + 1 + i]);
                    }
                    currentCoroutine_->stack.push_back(funcVal);
                    for (auto& arg : args) {
                        currentCoroutine_->stack.push_back(arg);
                    }
                    // Save pc (though tailcall reuses frame, be safe)
                    frame.ip = pc;
                    // Use tail call flag
                    size_t prevFrames = currentCoroutine_->frames.size();
                    if (!callValue(argCount, 0, true)) {  // 0 = multires, true = tailcall
                        return false;
                    }
                    if (currentCoroutine_->frames.size() > prevFrames) {
                        goto next_frame;
                    }
                    // C function tailcall; results on stack, need to return them
                    // For simplicity, copy to R(A) and do RETURN
                    // TODO: proper multires handling
                    break;
                }
                case ROpCode::ROP_CLOSURE: {
                    // R(A) = closure(K[Bx]), followed by nup pseudo-instructions
                    // Each pseudo: ROP_CLOSURE with A=isLocal, Bx=idx
                    Value funcValue = constants[Bx];
                    size_t funcIndex = funcValue.asFunctionIndex();
                    FunctionObject* function = chunk->getFunction(funcIndex);
                    
                    ClosureObject* closure = createClosure(function);
                    // Re-establish R after potential GC/stack reallocation
                    R = currentCoroutine_->stack.data() + base;
                    // Anchor for GC
                    R[A] = Value::closure(closure);
                    
                    // Capture upvalues from pseudo-instructions
                    int nup = function->upvalueCount();
                    for (int i = 0; i < nup; i++) {
                        uint32_t desc = code[pc++];
                        // Desc is ROP_CLOSURE with A=isLocal, Bx=idx
                        uint8_t isLocal = ropGetA(desc);
                        uint16_t idx = ropGetBx(desc);
                        
                        if (isLocal) {
                            // Capture from current frame's register window
                            size_t stackIndex = base + idx;
                            UpvalueObject* upvalue = captureUpvalue(stackIndex);
                            closure->setUpvalue(i, upvalue);
                        } else {
                            // Capture from enclosing function's upvalue
                            UpvalueObject* upvalue = frame.closure->getUpvalueObj(idx);
                            closure->setUpvalue(i, upvalue);
                        }
                    }
                    break;
                }
                case ROpCode::ROP_VARARG: {
                    // R(A)..R(A+B-2) = varargs (B=1: no values)
                    // B = number of values + 1
                    if (B == 1) {
                        // No values to copy
                        break;
                    }
                    int count = B - 1;
                    // Copy from frame.varargs to registers
                    // If fewer varargs than requested, fill with nil
                    for (int i = 0; i < count; i++) {
                        if (i < (int)frame.varargs.size()) {
                            R[A + i] = frame.varargs[i];
                        } else {
                            R[A + i] = Value::nil();
                        }
                    }
                    break;
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
                    // R(A+3) is external (loop variable)
                    // Setup: R(A) = init - step (so FORLOOP adds step first)
                    // Jump to FORLOOP for first iteration check
                    Value init = R[A];
                    Value limitV = R[A+1];
                    Value step = R[A+2];
                    if (!init.isNumber() || !limitV.isNumber() || !step.isNumber()) {
                        hadError_ = true;
                        return false;
                    }
                    // Preserve integer types
                    if (init.isInteger() && step.isInteger()) {
                        R[A] = makeInteger(init.asInteger() - step.asInteger());
                    } else {
                        R[A] = Value::number(init.asNumber() - step.asNumber());
                    }
                    pc += sBx;
                    break;
                }
                case ROpCode::ROP_FORLOOP: {
                    // R(A) += R(A+2); if (step > 0 ? R(A) <= R(A+1) : R(A) >= R(A+1)) then R(A+3)=R(A); pc += sBx
                    Value step = R[A+2];
                    if (!step.isNumber()) {
                        hadError_ = true;
                        return false;
                    }
                    // Preserve integer types if possible
                    bool isInt = R[A].isInteger() && step.isInteger() && R[A+1].isInteger();
                    if (isInt) {
                        int64_t idx = R[A].asInteger() + step.asInteger();
                        R[A] = makeInteger(idx);
                        int64_t limit = R[A+1].asInteger();
                        int64_t stepN = step.asInteger();
                        bool cond = (stepN > 0) ? (idx <= limit) : (idx >= limit);
                        if (cond) {
                            R[A+3] = R[A];
                            pc += sBx;
                        }
                    } else {
                        double stepN = step.asNumber();
                        double idx = R[A].asNumber() + stepN;
                        R[A] = Value::number(idx);
                        double limit = R[A+1].asNumber();
                        bool cond = (stepN > 0) ? (idx <= limit) : (idx >= limit);
                        if (cond) {
                            R[A+3] = R[A];
                            pc += sBx;
                        }
                    }
                    break;
                }
                case ROpCode::ROP_CLOSE: {
                    // Close upvalues / to-be-closed variables with register >= A
                    // For now: close upvalues with stackIndex >= base + A
                    size_t level = base + A;
                    closeUpvalues(level);
                    break;
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
                    // R(A)..R(A+B-2) are return values (B=1: no values, B=0: multires)
                    bool isMultiRet = (B == 0);
                    int retCount = isMultiRet ? static_cast<int>(currentCoroutine_->lastResultCount) : (B - 1);
                    
                    // Collect return values
                    // Re-establish R in case stack reallocated
                    R = currentCoroutine_->stack.data() + base;
                    std::vector<Value> retVals;
                    for (int i = 0; i < retCount; i++) {
                        retVals.push_back(R[A + i]);
                    }
                    
                    // Pop current frame
                    // Save regDest before popping
                    int dest = frame.regDest;
                    size_t frameStackBase = frame.stackBase;
                    
                    // Close upvalues in this frame's window
                    closeUpvalues(frameStackBase);
                    
                    currentCoroutine_->frames.pop_back();
                    
                    // If we've reached target, done
                    if (currentCoroutine_->frames.size() <= targetFrameCount) {
                        return !hadError_;
                    }
                    
                    // Copy results to caller's destination
                    if (dest >= 0) {
                        // Register VM caller: copy to stack[dest]..
                        for (int i = 0; i < retCount; i++) {
                            currentCoroutine_->stack[dest + i] = retVals[i];
                        }
                        // TODO: handle retCount mismatch (caller expects different count)
                    } else {
                        // Stack VM caller: push results onto stack
                        for (auto& v : retVals) {
                            currentCoroutine_->stack.push_back(v);
                        }
                    }
                    
                    // Shrink stack to caller's window end to prevent unbounded growth
                    // Caller is now on top of frames
                    if (!currentCoroutine_->frames.empty()) {
                        CallFrame& caller = currentCoroutine_->frames.back();
                        FunctionObject* callerFunc = caller.closure->function();
                        if (callerFunc->chunk()->hasRCode()) {
                            size_t callerEnd = caller.stackBase + callerFunc->chunk()->rFrameSize();
                            if (currentCoroutine_->stack.size() > callerEnd) {
                                currentCoroutine_->stack.resize(callerEnd);
                            }
                        }
                    }
                    
                    // Set lastResultCount for multires callers
                    currentCoroutine_->lastResultCount = static_cast<size_t>(retCount);
                    
                    // Continue with caller frame
                    goto next_frame;
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
