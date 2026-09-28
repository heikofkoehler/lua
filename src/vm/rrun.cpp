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

namespace {
constexpr int MAX_TAG_LOOP = 2000;

static inline int64_t lua_shift_left(int64_t x, int64_t y) {
    if (y < 0) {
        if (y <= -64) return 0;
        return static_cast<int64_t>(static_cast<uint64_t>(x) >> (-y));
    } else {
        if (y >= 64) return 0;
        return static_cast<int64_t>(static_cast<uint64_t>(x) << y);
    }
}

static inline int64_t lua_shift_right(int64_t x, int64_t y) {
    if (y < 0) {
        if (y <= -64) return 0;
        return static_cast<int64_t>(static_cast<uint64_t>(x) << (-y));
    } else {
        if (y >= 64) return 0;
        return static_cast<int64_t>(static_cast<uint64_t>(x) >> y);
    }
}
}

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
            if (chunk->code().empty()) {
                runtimeError("attempt to execute empty function");
                return false;
            }
            // No register code available and translator removed; fall back to stack VM
            if (!run(currentCoroutine_->frames.size() - 1)) {
                return false;
            }
            goto next_frame;
        }
        
        const std::vector<uint32_t>& code = chunk->rcode();
        const std::vector<Value>& constants = chunk->constants();
        // Register window: R[i] = stack[frame.stackBase + i]
        size_t base = frame.stackBase;
        size_t needed = base + chunk->rFrameSize();
        if (currentCoroutine_->stack.size() < needed) {
            currentCoroutine_->stack.resize(needed, Value::nil());
        }

        if (frame.yieldDest >= 0) {
            // We just resumed into this frame from a yield!
            int dest = frame.yieldDest;
            frame.yieldDest = -1;
            size_t pushed = currentCoroutine_->lastResultCount;
            std::vector<Value> res;
            for (size_t i = 0; i < pushed && !currentCoroutine_->stack.empty(); i++) {
                res.push_back(currentCoroutine_->stack.back());
                currentCoroutine_->stack.pop_back();
            }
            std::reverse(res.begin(), res.end());
            for (size_t i = 0; i < res.size(); i++) {
                currentCoroutine_->stack[dest + i] = res[i];
            }
            frame.resultCount = res.size();
            frame.topReg = (dest >= static_cast<int>(base) ? static_cast<size_t>(dest - base) : 0) + res.size();
        }

        Value* R = currentCoroutine_->stack.data() + base;
        size_t pc = frame.ip;  // Restore pc from frame (0 for new frames)
        
        // TODO: Get frame size from translator result (maxRegisters)
        // For now, use a large enough value
        
        auto dispatchBinaryMM = [&](uint8_t destReg, const Value& v1, const Value& v2, const char* method, const char* opDesc, int explicitBadReg = -1) -> int {
            frame.ip = pc;
            size_t prevFrames = currentCoroutine_->frames.size();
            if (!callBinaryMetamethod(v1, v2, method)) {
                if (!hadError_) {
                    int badReg = explicitBadReg;
                    if (std::string(opDesc) == "perform bitwise operation on" && v1.isNumber() && v2.isNumber()) {
                        if (badReg < 0 && pc > 0) {
                            int64_t dummy;
                            bool ok1 = toIntegerNoString(v1, dummy);
                            badReg = !ok1 ? (int)ropGetB(code[pc - 1]) : (int)ropGetC(code[pc - 1]);
                        }
                        runtimeError("number" + getRVarInfo(pc - 1, badReg) + " has no integer representation");
                    } else {
                        if (badReg < 0 && pc > 0) {
                            badReg = !coerceToNumber(const_cast<Value&>(v1)) ? (int)ropGetB(code[pc - 1]) : (int)ropGetC(code[pc - 1]);
                        }
                        Value badVal = !coerceToNumber(const_cast<Value&>(v1)) ? v1 : v2;
                        runtimeError(std::string("attempt to ") + opDesc + " a " + typeName(badVal) + " value" + getRVarInfo(pc - 1, badReg));
                    }
                }
                return -1;
            }
            if (currentCoroutine_->frames.size() > prevFrames) {
                CallFrame& newFrame = currentCoroutine_->frames.back();
                newFrame.regDest = static_cast<int>(base + destReg);
                if (!newFrame.closure->function()->chunk()->hasRCode()) {
                    hadError_ = true;
                    return -1;
                }
                return 1;
            }
            R = currentCoroutine_->stack.data() + base;
            R[destReg] = pop();
            return 0;
        };

        while (pc < code.size()) {
            if (stdlibInitialized_ && !currentCoroutine_->inHook && currentCoroutine_->hookMask != 0) {
                bool triggerCount = false;
                bool triggerLine = false;
                int currentLine = -1;

                if (currentCoroutine_->hookMask & CoroutineObject::MASK_COUNT) {
                    if (--currentCoroutine_->hookCount <= 0) {
                        triggerCount = true;
                        currentCoroutine_->hookCount = currentCoroutine_->baseHookCount;
                    }
                }

                if (currentCoroutine_->hookMask & CoroutineObject::MASK_LINE) {
                    if (!currentCoroutine_->frames.empty()) {
                        CallFrame& curFrame = currentFrame();
                        if (curFrame.chunk) {
                            currentLine = curFrame.chunk->getLine(pc);
                            if (currentLine > 0) {
                                if (curFrame.lastLine == -1 ||
                                    pc < curFrame.lastIp ||
                                    currentLine != curFrame.lastLine) {
                                    triggerLine = true;
                                    curFrame.lastLine = currentLine;
                                }
                            } else {
                                if (curFrame.lastLine == -1 || pc < curFrame.lastIp) {
                                    triggerLine = true;
                                    curFrame.lastLine = -2;
                                }
                            }
                            curFrame.lastIp = pc;
                        }
                    }
                }

                if (triggerCount) {
                    frame.ip = pc;
                    callHook("count");
                    if (hadError_) return false;
                    R = currentCoroutine_->stack.data() + base;
                }
                
                if (triggerLine) {
                    frame.ip = pc;
                    callHook("line", currentLine);
                    if (hadError_) return false;
                    R = currentCoroutine_->stack.data() + base;
                }
            }

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
                    Value b = R[B], c = R[C];
                    if (b.isInteger() && c.isInteger()) {
                        R[A] = makeInteger(static_cast<int64_t>(static_cast<uint64_t>(b.asInteger()) + static_cast<uint64_t>(c.asInteger())));
                    } else {
                        Value ca = b, cb = c;
                        if (coerceToNumber(ca) && coerceToNumber(cb)) {
                            R[A] = add(ca, cb);
                        } else {
                            int r = dispatchBinaryMM(A, b, c, "__add", "perform arithmetic on");
                            if (r < 0) return false;
                            if (r > 0) goto next_frame;
                        }
                    }
                    break;
                }
                case ROpCode::ROP_SUB: {
                    Value b = R[B], c = R[C];
                    if (b.isInteger() && c.isInteger()) {
                        R[A] = makeInteger(static_cast<int64_t>(static_cast<uint64_t>(b.asInteger()) - static_cast<uint64_t>(c.asInteger())));
                    } else {
                        Value ca = b, cb = c;
                        if (coerceToNumber(ca) && coerceToNumber(cb)) {
                            R[A] = subtract(ca, cb);
                        } else {
                            int r = dispatchBinaryMM(A, b, c, "__sub", "perform arithmetic on");
                            if (r < 0) return false;
                            if (r > 0) goto next_frame;
                        }
                    }
                    break;
                }
                case ROpCode::ROP_MUL: {
                    Value b = R[B], c = R[C];
                    if (b.isInteger() && c.isInteger()) {
                        R[A] = makeInteger(static_cast<int64_t>(static_cast<uint64_t>(b.asInteger()) * static_cast<uint64_t>(c.asInteger())));
                    } else {
                        Value ca = b, cb = c;
                        if (coerceToNumber(ca) && coerceToNumber(cb)) {
                            R[A] = multiply(ca, cb);
                        } else {
                            int r = dispatchBinaryMM(A, b, c, "__mul", "perform arithmetic on");
                            if (r < 0) return false;
                            if (r > 0) goto next_frame;
                        }
                    }
                    break;
                }
                case ROpCode::ROP_DIV: {
                    Value b = R[B], c = R[C];
                    Value ca = b, cb = c;
                    if (coerceToNumber(ca) && coerceToNumber(cb)) {
                        R[A] = divide(ca, cb);
                    } else {
                        int r = dispatchBinaryMM(A, b, c, "__div", "perform arithmetic on");
                        if (r < 0) return false;
                        if (r > 0) goto next_frame;
                    }
                    break;
                }
                case ROpCode::ROP_IDIV: {
                    Value b = R[B], c = R[C];
                    Value ca = b, cb = c;
                    if (coerceToNumber(ca) && coerceToNumber(cb)) {
                        R[A] = integerDivide(ca, cb);
                    } else {
                        int r = dispatchBinaryMM(A, b, c, "__idiv", "perform arithmetic on");
                        if (r < 0) return false;
                        if (r > 0) goto next_frame;
                    }
                    break;
                }
                case ROpCode::ROP_MOD: {
                    Value b = R[B], c = R[C];
                    Value ca = b, cb = c;
                    if (coerceToNumber(ca) && coerceToNumber(cb)) {
                        R[A] = modulo(ca, cb);
                    } else {
                        int r = dispatchBinaryMM(A, b, c, "__mod", "perform arithmetic on");
                        if (r < 0) return false;
                        if (r > 0) goto next_frame;
                    }
                    break;
                }
                case ROpCode::ROP_POW: {
                    Value b = R[B], c = R[C];
                    Value ca = b, cb = c;
                    if (coerceToNumber(ca) && coerceToNumber(cb)) {
                        R[A] = power(ca, cb);
                    } else {
                        int r = dispatchBinaryMM(A, b, c, "__pow", "perform arithmetic on");
                        if (r < 0) return false;
                        if (r > 0) goto next_frame;
                    }
                    break;
                }
                case ROpCode::ROP_NEG: {
                    // R(A) = -R(B)
                    Value b = R[B];
                    if (b.isInteger()) {
                        R[A] = makeInteger(static_cast<int64_t>(-static_cast<uint64_t>(b.asInteger())));
                    } else {
                        Value ca = b;
                        if (coerceToNumber(ca)) {
                            R[A] = negate(ca);
                        } else {
                            int r = dispatchBinaryMM(A, b, b, "__unm", "perform arithmetic on", B);
                            if (r < 0) return false;
                            if (r > 0) goto next_frame;
                        }
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
                    } else if (b.isTable() && getMetamethod(b, "__len").isNil()) {
                        R[A] = Value::integer(static_cast<int64_t>(b.asTableObj()->length()));
                    } else {
                        Value mm = getMetamethod(b, "__len");
                        if (mm.isNil()) {
                            runtimeError("attempt to get length of a " + typeName(b) + " value");
                            return false;
                        }
                        push(mm);
                        push(b);
                        frame.ip = pc;
                        size_t prevFrames = currentCoroutine_->frames.size();
                        if (!callValue(1, 2, false, "len")) return false;
                        if (currentCoroutine_->frames.size() > prevFrames) {
                            CallFrame& newFrame = currentCoroutine_->frames.back();
                            newFrame.regDest = static_cast<int>(base + A);
                            if (!newFrame.closure->function()->chunk()->hasRCode()) {
                                hadError_ = true;
                                return false;
                            }
                            goto next_frame;
                        }
                        R = currentCoroutine_->stack.data() + base;
                        R[A] = pop();
                    }
                    break;
                }
                case ROpCode::ROP_BAND: {
                    // R(A) = R(B) & R(C)
                    Value b = R[B], c = R[C];
                    int64_t ib, ic;
                    if (toIntegerNoString(b, ib) && toIntegerNoString(c, ic)) {
                        R[A] = makeInteger(ib & ic);
                    } else {
                        int r = dispatchBinaryMM(A, b, c, "__band", "perform bitwise operation on");
                        if (r < 0) return false;
                        if (r > 0) goto next_frame;
                    }
                    break;
                }
                case ROpCode::ROP_BOR: {
                    Value b = R[B], c = R[C];
                    int64_t ib, ic;
                    if (toIntegerNoString(b, ib) && toIntegerNoString(c, ic)) {
                        R[A] = makeInteger(ib | ic);
                    } else {
                        int r = dispatchBinaryMM(A, b, c, "__bor", "perform bitwise operation on");
                        if (r < 0) return false;
                        if (r > 0) goto next_frame;
                    }
                    break;
                }
                case ROpCode::ROP_BXOR: {
                    Value b = R[B], c = R[C];
                    int64_t ib, ic;
                    if (toIntegerNoString(b, ib) && toIntegerNoString(c, ic)) {
                        R[A] = makeInteger(ib ^ ic);
                    } else {
                        int r = dispatchBinaryMM(A, b, c, "__bxor", "perform bitwise operation on");
                        if (r < 0) return false;
                        if (r > 0) goto next_frame;
                    }
                    break;
                }
                case ROpCode::ROP_SHL: {
                    Value b = R[B], c = R[C];
                    int64_t ib, ic;
                    if (toIntegerNoString(b, ib) && toIntegerNoString(c, ic)) {
                        R[A] = makeInteger(lua_shift_left(ib, ic));
                    } else {
                        int r = dispatchBinaryMM(A, b, c, "__shl", "perform bitwise operation on");
                        if (r < 0) return false;
                        if (r > 0) goto next_frame;
                    }
                    break;
                }
                case ROpCode::ROP_SHR: {
                    Value b = R[B], c = R[C];
                    int64_t ib, ic;
                    if (toIntegerNoString(b, ib) && toIntegerNoString(c, ic)) {
                        R[A] = makeInteger(lua_shift_right(ib, ic));
                    } else {
                        int r = dispatchBinaryMM(A, b, c, "__shr", "perform bitwise operation on");
                        if (r < 0) return false;
                        if (r > 0) goto next_frame;
                    }
                    break;
                }
                case ROpCode::ROP_BNOT: {
                    // R(A) = ~R(B)
                    Value b = R[B];
                    int64_t ib;
                    if (toIntegerNoString(b, ib)) {
                        R[A] = makeInteger(~ib);
                    } else {
                        int r = dispatchBinaryMM(A, b, b, "__bnot", "perform bitwise operation on", B);
                        if (r < 0) return false;
                        if (r > 0) goto next_frame;
                    }
                    break;
                }
                case ROpCode::ROP_CONCAT: {
                    // R(A) = R(B) .. R(C)
                    Value b = R[B];
                    Value c = R[C];
                    if ((b.isString() || b.isNumber()) && (c.isString() || c.isNumber())) {
                        R[A] = concat(b, c);
                    } else {
                        int r = dispatchBinaryMM(A, b, c, "__concat", "concatenate");
                        if (r < 0) return false;
                        if (r > 0) goto next_frame;
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
                    Value t = tableVal;
                    bool done = false;
                    for (int loop = 0; loop < MAX_TAG_LOOP; loop++) {
                        if (t.isTable()) {
                            TableObject* table = t.asTableObj();
                            Value result = table->get(key);
                            if (!result.isNil() || table->getMetatable().isNil()) {
                                R[A] = result;
                                done = true;
                                break;
                            }
                        }

                        if (key.isString()) {
                            Value mm = getMetamethod(t, getStringValue(key));
                            if (!mm.isNil()) {
                                R[A] = mm;
                                done = true;
                                break;
                            }
                        }

                        Value indexMethod = getMetamethod(t, "__index");
                        if (indexMethod.isNil()) {
                            if (!t.isTable()) {
                                runtimeError("attempt to index a " + typeName(t) + " value" + (loop == 0 ? getRVarInfo(pc - 1, B) : ""));
                                return false;
                            }
                            R[A] = Value::nil();
                            done = true;
                            break;
                        } else if (indexMethod.isFunction()) {
                            push(indexMethod);
                            push(t);
                            push(key);
                            frame.ip = pc;
                            size_t prevFrames = currentCoroutine_->frames.size();
                            if (!callValue(2, 2, false, "index")) {
                                return false;
                            }
                            if (currentCoroutine_->frames.size() > prevFrames) {
                                CallFrame& newFrame = currentCoroutine_->frames.back();
                                newFrame.regDest = static_cast<int>(base + A);
                                if (!newFrame.closure->function()->chunk()->hasRCode()) {
                                    hadError_ = true;
                                    return false;
                                }
                                goto next_frame;
                            }
                            R = currentCoroutine_->stack.data() + base;
                            R[A] = pop();
                            done = true;
                            break;
                        } else if (indexMethod.isTable()) {
                            t = indexMethod;
                        } else {
                            t = indexMethod;
                        }
                    }
                    if (!done) {
                        runtimeError("'__index' chain too long; possible loop");
                        return false;
                    }
                    break;
                }
                case ROpCode::ROP_SETTABLE: {
                    // R(A)[R(B)] = R(C)
                    Value tableVal = R[A];
                    Value key = R[B];
                    Value val = R[C];
                    Value t = tableVal;
                    bool done = false;
                    for (int loop = 0; loop < MAX_TAG_LOOP; loop++) {
                        if (t.isTable()) {
                            TableObject* table = t.asTableObj();
                            if (table->getMetatable().isNil() || table->has(key)) {
                                if (key.isNil()) {
                                    runtimeError("table index is nil");
                                    return false;
                                }
                                if (key.isFloat() && std::isnan(key.asNumber())) {
                                    runtimeError("table index is NaN");
                                    return false;
                                }
                                table->set(key, val);
                                if (val.isObj()) {
                                    writeBarrier(table, val.asObj());
                                }
                                done = true;
                                break;
                            }
                        }

                        Value newindexMethod = getMetamethod(t, "__newindex");
                        if (newindexMethod.isNil()) {
                            if (!t.isTable()) {
                                runtimeError("attempt to index a " + typeName(t) + " value" + (loop == 0 ? getRVarInfo(pc - 1, A) : ""));
                                return false;
                            }
                            if (key.isNil()) {
                                runtimeError("table index is nil");
                                return false;
                            }
                            if (key.isFloat() && std::isnan(key.asNumber())) {
                                runtimeError("table index is NaN");
                                return false;
                            }
                            TableObject* table = t.asTableObj();
                            table->set(key, val);
                            if (val.isObj()) {
                                writeBarrier(table, val.asObj());
                            }
                            done = true;
                            break;
                        } else if (newindexMethod.isFunction()) {
                            push(newindexMethod);
                            push(t);
                            push(key);
                            push(val);
                            frame.ip = pc;
                            size_t prevFrames = currentCoroutine_->frames.size();
                            if (!callValue(3, 1, false, "newindex")) {
                                return false;
                            }
                            if (currentCoroutine_->frames.size() > prevFrames) {
                                CallFrame& newFrame = currentCoroutine_->frames.back();
                                newFrame.regDest = -1;
                                if (!newFrame.closure->function()->chunk()->hasRCode()) {
                                    hadError_ = true;
                                    return false;
                                }
                                goto next_frame;
                            }
                            done = true;
                            break;
                        } else if (newindexMethod.isTable()) {
                            t = newindexMethod;
                        } else {
                            t = newindexMethod;
                        }
                    }
                    if (!done) {
                        runtimeError("'__newindex' chain too long; possible loop");
                        return false;
                    }
                    break;
                }
                case ROpCode::ROP_SETTABLEMULTI: {
                    // Multi-value table set: R(A)[R(B) + i] = R(B + 1 + i)
                    Value tblVal = R[A];
                    if (!tblVal.isTable()) {
                        runtimeError("attempt to index a " + typeName(tblVal) + " value");
                        return false;
                    }
                    TableObject* tbl = tblVal.asTableObj();
                    int64_t keyBase = R[B].isInteger() ? R[B].asInteger() : (R[B].isNumber() ? static_cast<int64_t>(R[B].asNumber()) : 1);
                    size_t count = frame.resultCount;
                    for (size_t i = 0; i < count; i++) {
                        tbl->set(Value::integer(keyBase + static_cast<int64_t>(i)), R[B + 1 + i]);
                    }
                    break;
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
                    // For multires args: read count from topReg
                    int argCount = isMultiArg ? std::max(0, static_cast<int>(frame.topReg - (A + 1))) : (B - 1);
                    // For multires returns: use 0 to signal "all results" to callValue
                    // callValue expects C: 0 = all results, C (>0) = C-1 results (so C is wanted + 1).
                    int callRetParam = C;
                    
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
                    if (!callValue(argCount, callRetParam)) {
                        return false;
                    }
                    
                    if (currentCoroutine_->status == CoroutineObject::Status::SUSPENDED) {
                        frame.yieldDest = static_cast<int>(base + A);
                        return true;
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
                            if (newFunc->chunk()->code().empty()) {
                                runtimeError("attempt to execute empty function");
                                return false;
                            }
                            // No register code available and translator removed; fall back to stack VM
                            // The stack VM's run() will execute the callee and leave results on stack top.
                            // We need to copy them to R[A] like the C function path does.
                            size_t stackSizeBeforeCall = currentCoroutine_->stack.size();
                            if (!run(currentCoroutine_->frames.size() - 1)) {
                                return false;
                            }
                            R = currentCoroutine_->stack.data() + base;
                            // Copy results from stack top to R[A]
                            // For non-multires, we want C-1 results. For multires, take all.
                            int wanted = isMultiRet ? -1 : (C - 1);
                            size_t stackSizeAfter = currentCoroutine_->stack.size();
                            int actualRetCount;
                            if (wanted < 0) {
                                // Multires: all values pushed since before the call
                                actualRetCount = static_cast<int>(stackSizeAfter - stackSizeBeforeCall);
                                if (actualRetCount < 0) actualRetCount = 0;
                                currentCoroutine_->lastResultCount = static_cast<size_t>(actualRetCount);
                                frame.resultCount = static_cast<size_t>(actualRetCount);
                                frame.topReg = A + actualRetCount;
                            } else {
                                actualRetCount = wanted;
                            }
                            // Copy from stack top
                            for (int i = 0; i < actualRetCount; i++) {
                                size_t idx = stackSizeAfter - actualRetCount + i;
                                if (idx < currentCoroutine_->stack.size()) {
                                    R[A + i] = currentCoroutine_->stack[idx];
                                } else {
                                    R[A + i] = Value::nil();
                                }
                            }
                            // Pop results from stack
                            for (int i = 0; i < actualRetCount; i++) {
                                if (!currentCoroutine_->stack.empty()) {
                                    currentCoroutine_->stack.pop_back();
                                }
                            }
                            // Clear dead arg registers
                            for (int i = actualRetCount; i <= argCount; i++) {
                                R[A + i] = Value::nil();
                            }
                            break;
                        }
                        if (currentCoroutine_->hookMask & CoroutineObject::MASK_CALL) {
                            callHook("call");
                            if (hadError_) return false;
                        }
                        goto next_frame;
                    }
                    
                    // No new frame (C function); results are on stack top
                    // Copy results back to R(A)..
                    // Re-establish R after potential stack reallocation
                    R = currentCoroutine_->stack.data() + base;
                    int actualRetCount = isMultiRet ? static_cast<int>(currentCoroutine_->stack.size() - stackBaseBefore) : (C - 1);
                    if (actualRetCount < 0) actualRetCount = 0;
                    if (isMultiRet) {
                        currentCoroutine_->lastResultCount = static_cast<size_t>(actualRetCount);
                        frame.resultCount = static_cast<size_t>(actualRetCount);
                        frame.topReg = A + actualRetCount;
                    }
                    size_t stackSize = currentCoroutine_->stack.size();
                    for (int i = 0; i < actualRetCount; i++) {
                        R[A + i] = currentCoroutine_->stack[stackSize - actualRetCount + i];
                    }
                    // Pop results from stack
                    for (int i = 0; i < actualRetCount; i++) {
                        currentCoroutine_->stack.pop_back();
                    }
                    // Clear dead argument registers to avoid pinning objects for GC
                    for (int i = actualRetCount; i <= argCount; i++) {
                        R[A + i] = Value::nil();
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
                    // R(A)..R(A+B-2) = varargs (B=1: no values, B=0: multires)
                    if (B == 1) {
                        break;
                    }
                    if (B == 0) {
                        int count = static_cast<int>(frame.varargs.size());
                        size_t needed = base + A + count;
                        if (currentCoroutine_->stack.size() < needed) {
                            currentCoroutine_->stack.resize(needed, Value::nil());
                        }
                        R = currentCoroutine_->stack.data() + base;
                        for (int i = 0; i < count; i++) {
                            R[A + i] = frame.varargs[i];
                        }
                        frame.topReg = A + count;
                        frame.resultCount = static_cast<size_t>(count);
                        currentCoroutine_->lastResultCount = static_cast<size_t>(count);
                        break;
                    }
                    int count = B - 1;
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
                    if (!currentCoroutine_->caller || currentCoroutine_ == mainCoroutine_) {
                        runtimeError("attempt to yield from outside a coroutine");
                        return false;
                    }
                    if (currentCoroutine_->nonYieldableCount > 0 || currentCoroutine_->isClosing) {
                        runtimeError("attempt to yield across a C-call boundary");
                        return false;
                    }
                    int yieldCount = (B == 0) ? std::max(0, static_cast<int>(frame.topReg - A)) : (B - 1);
                    currentCoroutine_->yieldedValues.clear();
                    for (int i = 0; i < yieldCount; i++) {
                        currentCoroutine_->yieldedValues.push_back(R[A + i]);
                    }
                    if (currentCoroutine_->hookMask & CoroutineObject::MASK_CALL) {
                        callHook("call");
                    }
                    currentCoroutine_->status = CoroutineObject::Status::SUSPENDED;
                    currentCoroutine_->yieldCount = yieldCount;
                    currentCoroutine_->retCount = C;
                    if (currentCoroutine_->hookMask & CoroutineObject::MASK_RET) {
                        callHook("return");
                    }
                    frame.yieldDest = static_cast<int>(base + A);
                    frame.ip = pc;
                    return true;
                }
                case ROpCode::ROP_PACKVARARG: {
                    TableObject* tbl = createTable();
                    for (size_t i = 0; i < frame.varargs.size(); i++) {
                        tbl->set(Value::integer(static_cast<int64_t>(i + 1)), frame.varargs[i]);
                    }
                    tbl->set("n", Value::integer(static_cast<int64_t>(frame.varargs.size())));
                    R[A] = Value::table(tbl);
                    break;
                }
                case ROpCode::ROP_VARARGITEM: {
                    Value key = R[B];
                    if (key.isStringEqual("n")) {
                        R[A] = Value::integer(static_cast<int64_t>(frame.varargs.size()));
                    } else if (key.isInteger()) {
                        int64_t idx = key.asInteger();
                        if (idx >= 1 && static_cast<size_t>(idx) <= frame.varargs.size()) {
                            R[A] = frame.varargs[idx - 1];
                        } else {
                            R[A] = Value::nil();
                        }
                    } else {
                        R[A] = Value::nil();
                    }
                    break;
                }
                case ROpCode::ROP_VARARGCOUNT: {
                    R[A] = Value::integer(static_cast<int64_t>(frame.varargs.size()));
                    break;
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
                case ROpCode::ROP_TBC: {
                    size_t index = base + A;
                    Value val = currentCoroutine_->stack[index];
                    if (!val.isFalsey()) {
                        Value mm = getMetamethod(val, "__close");
                        if (mm.isNil()) {
                            std::string varName = getStringValue(getConstant(Bx));
                            runtimeError("variable '" + varName + "' got a non-closable value");
                            break;
                        }
                    }
                    currentCoroutine_->tbcVariables.push_back(index);
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
                    if (!eq && b.isTable() && c.isTable()) {
                        Value mm1 = getMetamethod(b, "__eq");
                        Value mm2 = getMetamethod(c, "__eq");
                        if (!mm1.isNil() && mm1 == mm2) {
                            push(mm1);
                            push(b);
                            push(c);
                            frame.ip = pc;
                            size_t prevFrames = currentCoroutine_->frames.size();
                            if (callValue(2, 2, false, "eq")) {
                                if (currentCoroutine_->frames.size() > prevFrames) {
                                    if (!runRegister(prevFrames)) return false;
                                }
                                Value res = pop();
                                eq = !res.isFalsey();
                            }
                        }
                    }
                    R = currentCoroutine_->stack.data() + base;
                    if (eq != (A != 0)) pc++;
                    break;
                }
                case ROpCode::ROP_LT: {
                    // if ((R(B) < R(C)) != A) pc++
                    Value b = R[B];
                    Value c = R[C];
                    bool lt = false;
                    if (b.isNumber() && c.isNumber()) {
                        lt = less(b, c).asBool();
                    } else if (b.isString() && c.isString()) {
                        lt = getStringValue(b) < getStringValue(c);
                    } else {
                        Value mm = getMetamethod(b, "__lt");
                        if (mm.isNil()) mm = getMetamethod(c, "__lt");
                        if (!mm.isNil()) {
                            push(mm);
                            push(b);
                            push(c);
                            frame.ip = pc;
                            size_t prevFrames = currentCoroutine_->frames.size();
                            if (callValue(2, 2, false, "lt")) {
                                if (currentCoroutine_->frames.size() > prevFrames) {
                                    if (!runRegister(prevFrames)) return false;
                                }
                                Value res = pop();
                                lt = !res.isFalsey();
                            } else {
                                return false;
                            }
                        } else {
                            std::string ta = typeName(b);
                            std::string tb = typeName(c);
                            if (ta == tb) {
                                runtimeError("attempt to compare two " + ta + " values");
                            } else {
                                runtimeError("attempt to compare " + ta + " with " + tb);
                            }
                            return false;
                        }
                    }
                    R = currentCoroutine_->stack.data() + base;
                    if (lt != (A != 0)) pc++;
                    break;
                }
                case ROpCode::ROP_LE: {
                    // if ((R(B) <= R(C)) != A) pc++
                    Value b = R[B];
                    Value c = R[C];
                    bool le = false;
                    if (b.isNumber() && c.isNumber()) {
                        le = lessEqual(b, c).asBool();
                    } else if (b.isString() && c.isString()) {
                        le = getStringValue(b) <= getStringValue(c);
                    } else {
                        Value mm = getMetamethod(b, "__le");
                        if (mm.isNil()) mm = getMetamethod(c, "__le");
                        if (!mm.isNil()) {
                            push(mm);
                            push(b);
                            push(c);
                            frame.ip = pc;
                            size_t prevFrames = currentCoroutine_->frames.size();
                            if (callValue(2, 2, false, "le")) {
                                if (currentCoroutine_->frames.size() > prevFrames) {
                                    if (!runRegister(prevFrames)) return false;
                                }
                                Value res = pop();
                                le = !res.isFalsey();
                            } else {
                                return false;
                            }
                        } else {
                            Value mmlt = getMetamethod(c, "__lt");
                            if (mmlt.isNil()) mmlt = getMetamethod(b, "__lt");
                            if (!mmlt.isNil()) {
                                push(mmlt);
                                push(c);
                                push(b);
                                frame.ip = pc;
                                size_t prevFrames = currentCoroutine_->frames.size();
                                if (callValue(2, 2, false, "lt")) {
                                    if (currentCoroutine_->frames.size() > prevFrames) {
                                        if (!runRegister(prevFrames)) return false;
                                    }
                                    Value res = pop();
                                    le = res.isFalsey();
                                } else {
                                    return false;
                                }
                            } else {
                                std::string ta = typeName(b);
                                std::string tb = typeName(c);
                                if (ta == tb) {
                                    runtimeError("attempt to compare two " + ta + " values");
                                } else {
                                    runtimeError("attempt to compare " + ta + " with " + tb);
                                }
                                return false;
                            }
                        }
                    }
                    R = currentCoroutine_->stack.data() + base;
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
                    // For multires (B=0), read count from frame.resultCount (set by previous CALL)
                    bool isMultiRet = (B == 0);
                    int retCount = isMultiRet ? std::max(0, static_cast<int>(frame.topReg - A)) : (B - 1);
                    
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
                    uint8_t cRetCount = frame.retCount;
                    size_t frameStackBase = frame.stackBase;
                    
                    // Close upvalues in this frame's window
                    closeUpvalues(frameStackBase);

                    if (currentCoroutine_->hookMask & CoroutineObject::MASK_RET) {
                        callHook("return", -1, 0, static_cast<int>(retVals.size()));
                        if (hadError_) return false;
                    }
                    
                    currentCoroutine_->frames.pop_back();
                    
                    // Adjust retVals according to caller's expected return count if not multires
                    if (cRetCount > 0) {
                        size_t expected = static_cast<size_t>(cRetCount - 1);
                        if (retVals.size() > expected) {
                            retVals.resize(expected);
                        } else {
                            while (retVals.size() < expected) {
                                retVals.push_back(Value::nil());
                            }
                        }
                    }
                    int actualRetCount = static_cast<int>(retVals.size());

                    // Copy results to caller's destination or stack
                    if (dest >= 0) {
                        if (currentCoroutine_->stack.size() < static_cast<size_t>(dest + actualRetCount)) {
                            currentCoroutine_->stack.resize(dest + actualRetCount, Value::nil());
                        }
                        for (int i = 0; i < actualRetCount; i++) {
                            currentCoroutine_->stack[dest + i] = retVals[i];
                        }
                    } else {
                        // Stack VM caller or coroutine return:
                        if (currentCoroutine_->frames.empty()) {
                            currentCoroutine_->stack = std::move(retVals);
                            if (currentCoroutine_->caller && currentCoroutine_ != mainCoroutine_) {
                                currentCoroutine_->status = CoroutineObject::Status::DEAD;
                            }
                        } else {
                            currentCoroutine_->stack.resize(frameStackBase > 0 ? frameStackBase - 1 : 0);
                            for (auto& v : retVals) {
                                currentCoroutine_->stack.push_back(v);
                            }
                        }
                    }
                    
                    // If we've reached target, done
                    if (currentCoroutine_->frames.size() <= targetFrameCount) {
                        currentCoroutine_->lastResultCount = static_cast<size_t>(actualRetCount);
                        return !hadError_;
                    }
                    
                    // Shrink stack to caller's window end to prevent unbounded growth
                    // Caller is now on top of frames
                    if (!currentCoroutine_->frames.empty()) {
                        CallFrame& caller = currentCoroutine_->frames.back();
                        FunctionObject* callerFunc = caller.closure->function();
                        if (callerFunc->chunk()->hasRCode()) {
                            size_t callerEnd = std::max(caller.stackBase + callerFunc->chunk()->rFrameSize(),
                                                        static_cast<size_t>(dest >= 0 ? dest + actualRetCount : 0));
                            if (currentCoroutine_->stack.size() > callerEnd) {
                                currentCoroutine_->stack.resize(callerEnd);
                            }
                        }
                    }
                    
                    // Set resultCount and topReg in caller's frame for multires callers
                    // The caller will read this when it does a multires operation
                    if (!currentCoroutine_->frames.empty()) {
                        CallFrame& caller = currentCoroutine_->frames.back();
                        caller.resultCount = static_cast<size_t>(actualRetCount);
                        if (dest >= 0 && dest >= static_cast<int>(caller.stackBase)) {
                            caller.topReg = static_cast<size_t>(dest - caller.stackBase) + actualRetCount;
                        }
                    }
                    // Also set coroutine-level for backward compatibility (C calls, etc.)
                    currentCoroutine_->lastResultCount = static_cast<size_t>(actualRetCount);
                    
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
