#include "vm/vm.hpp"
#include "vm/jit.hpp"
#include "value/string.hpp"
#include "value/table.hpp"
#include "value/closure.hpp"
#include "value/upvalue.hpp"
#include "value/coroutine.hpp"
#include "value/userdata.hpp"
#include <iostream>
#include <algorithm>

std::string VM::getVarInfo(size_t opIp, int operandIndex) {
    if (currentCoroutine_->frames.empty() || !currentFrame().closure || !currentFrame().chunk) {
        return "";
    }
    FunctionObject* func = currentFrame().closure->function();
    const Chunk* chunk = currentFrame().chunk;
    const auto& code = chunk->code();
    if (opIp >= code.size()) return "";

    struct AbstractVal {
        enum Source { UNKNOWN, GLOBAL, LOCAL, UPVALUE, FIELD };
        Source source = UNKNOWN;
        std::string name;
        bool isConstStr = false;
        std::string constStr;
    };

    std::vector<AbstractVal> astack;
    size_t cur = 0;
    while (cur < opIp && cur < code.size()) {
        size_t len = chunk->instructionLength(cur);
        if (len == 0) break;
        OpCode op = static_cast<OpCode>(code[cur]);
        switch (op) {
            case OpCode::OP_CONSTANT: {
                uint8_t c = code[cur + 1];
                if (c < chunk->constants().size() && chunk->constants()[c].isString()) {
                    astack.push_back({AbstractVal::UNKNOWN, "", true, getStringValue(chunk->constants()[c])});
                } else {
                    astack.push_back({AbstractVal::UNKNOWN, "", false, ""});
                }
                break;
            }
            case OpCode::OP_CONSTANT_LONG: {
                uint32_t c = code[cur + 1] | (code[cur + 2] << 8) | (code[cur + 3] << 16);
                if (c < chunk->constants().size() && chunk->constants()[c].isString()) {
                    astack.push_back({AbstractVal::UNKNOWN, "", true, getStringValue(chunk->constants()[c])});
                } else {
                    astack.push_back({AbstractVal::UNKNOWN, "", false, ""});
                }
                break;
            }
            case OpCode::OP_NIL:
            case OpCode::OP_TRUE:
            case OpCode::OP_FALSE: {
                astack.push_back({AbstractVal::UNKNOWN, "", false, ""});
                break;
            }
            case OpCode::OP_GET_GLOBAL: {
                uint8_t nameIndex = code[cur + 1];
                std::string gname = chunk->getIdentifier(nameIndex);
                astack.push_back({AbstractVal::GLOBAL, gname, false, ""});
                break;
            }
            case OpCode::OP_SET_GLOBAL: {
                if (!astack.empty()) astack.pop_back();
                break;
            }
            case OpCode::OP_GET_LOCAL: {
                uint8_t slot = code[cur + 1];
                std::string lname;
                for (const auto& l : func->localVars()) {
                    if (l.slot == slot && cur >= l.startPC && cur <= l.endPC) {
                        lname = l.name;
                        break;
                    }
                }
                if (lname.empty()) {
                    for (const auto& l : func->localVars()) {
                        if (l.slot == slot) {
                            lname = l.name;
                        }
                    }
                }
                astack.push_back({AbstractVal::LOCAL, lname, false, ""});
                break;
            }
            case OpCode::OP_SET_LOCAL: {
                if (!astack.empty()) astack.pop_back();
                break;
            }
            case OpCode::OP_GET_UPVALUE: {
                uint8_t idx = code[cur + 1];
                std::string uname = func->getUpvalueName(idx);
                astack.push_back({AbstractVal::UPVALUE, uname, false, ""});
                break;
            }
            case OpCode::OP_SET_UPVALUE: {
                if (!astack.empty()) astack.pop_back();
                break;
            }
            case OpCode::OP_GET_TABUP: {
                uint8_t upIndex = code[cur + 1];
                uint8_t constIndex = code[cur + 2];
                std::string kname;
                if (constIndex < chunk->constants().size() && chunk->constants()[constIndex].isString()) {
                    kname = getStringValue(chunk->constants()[constIndex]);
                }
                std::string upname = func->getUpvalueName(upIndex);
                if (upIndex == 0 || upname == "_ENV") {
                    astack.push_back({AbstractVal::GLOBAL, kname, false, ""});
                } else {
                    astack.push_back({AbstractVal::FIELD, kname, false, ""});
                }
                break;
            }
            case OpCode::OP_SET_TABUP: {
                if (!astack.empty()) astack.pop_back();
                break;
            }
            case OpCode::OP_GET_TABUP_LONG: {
                uint8_t upIndex = code[cur + 1];
                uint32_t constIndex = code[cur + 2] | (code[cur + 3] << 8) | (code[cur + 4] << 16);
                std::string kname;
                if (constIndex < chunk->constants().size() && chunk->constants()[constIndex].isString()) {
                    kname = getStringValue(chunk->constants()[constIndex]);
                }
                std::string upname = func->getUpvalueName(upIndex);
                if (upIndex == 0 || upname == "_ENV") {
                    astack.push_back({AbstractVal::GLOBAL, kname, false, ""});
                } else {
                    astack.push_back({AbstractVal::FIELD, kname, false, ""});
                }
                break;
            }
            case OpCode::OP_SET_TABUP_LONG: {
                if (!astack.empty()) astack.pop_back();
                break;
            }
            case OpCode::OP_GET_TABLE: {
                AbstractVal key = astack.empty() ? AbstractVal{} : astack.back();
                if (!astack.empty()) astack.pop_back();
                if (!astack.empty()) astack.pop_back();
                if (key.isConstStr) {
                    astack.push_back({AbstractVal::FIELD, key.constStr, false, ""});
                } else {
                    astack.push_back({AbstractVal::UNKNOWN, "", false, ""});
                }
                break;
            }
            case OpCode::OP_SET_TABLE: {
                if (!astack.empty()) astack.pop_back();
                if (!astack.empty()) astack.pop_back();
                if (!astack.empty()) astack.pop_back();
                break;
            }
            case OpCode::OP_POP: {
                if (!astack.empty()) astack.pop_back();
                break;
            }
            case OpCode::OP_DUP: {
                if (!astack.empty()) astack.push_back(astack.back());
                break;
            }
            case OpCode::OP_SWAP: {
                if (astack.size() >= 2) std::swap(astack[astack.size() - 1], astack[astack.size() - 2]);
                break;
            }
            case OpCode::OP_ROTATE: {
                uint8_t n = code[cur + 1];
                if (astack.size() >= n && n > 1) {
                    AbstractVal top = astack.back();
                    astack.erase(astack.end() - 1);
                    astack.insert(astack.end() - (n - 1), top);
                }
                break;
            }
            case OpCode::OP_ADD:
            case OpCode::OP_SUB:
            case OpCode::OP_MUL:
            case OpCode::OP_DIV:
            case OpCode::OP_IDIV:
            case OpCode::OP_MOD:
            case OpCode::OP_POW:
            case OpCode::OP_BAND:
            case OpCode::OP_BOR:
            case OpCode::OP_BXOR:
            case OpCode::OP_SHL:
            case OpCode::OP_SHR:
            case OpCode::OP_CONCAT:
            case OpCode::OP_EQUAL:
            case OpCode::OP_LESS:
            case OpCode::OP_LESS_EQUAL:
            case OpCode::OP_GREATER:
            case OpCode::OP_GREATER_EQUAL: {
                if (!astack.empty()) astack.pop_back();
                if (!astack.empty()) astack.pop_back();
                astack.push_back({AbstractVal::UNKNOWN, "", false, ""});
                break;
            }
            case OpCode::OP_NEG:
            case OpCode::OP_NOT:
            case OpCode::OP_BNOT:
            case OpCode::OP_LEN: {
                if (!astack.empty()) astack.pop_back();
                astack.push_back({AbstractVal::UNKNOWN, "", false, ""});
                break;
            }
            case OpCode::OP_NEW_TABLE: {
                astack.push_back({AbstractVal::UNKNOWN, "", false, ""});
                break;
            }
            case OpCode::OP_CALL:
            case OpCode::OP_CALL_MULTI: {
                uint8_t argCount = code[cur + 1];
                uint8_t retCount = code[cur + 2];
                for (size_t i = 0; i <= argCount && !astack.empty(); i++) {
                    astack.pop_back();
                }
                if (retCount != 255) {
                    for (size_t i = 0; i < retCount; i++) {
                        astack.push_back({AbstractVal::UNKNOWN, "", false, ""});
                    }
                } else {
                    astack.push_back({AbstractVal::UNKNOWN, "", false, ""});
                }
                break;
            }
            case OpCode::OP_CLOSURE:
            case OpCode::OP_CLOSURE_LONG: {
                astack.push_back({AbstractVal::UNKNOWN, "", false, ""});
                break;
            }
            case OpCode::OP_DEF_GLOBAL:
            case OpCode::OP_DEF_GLOBAL_LONG: {
                if (!astack.empty()) astack.pop_back();
                break;
            }
            case OpCode::OP_DEF_GLOBAL_TABLE: {
                if (!astack.empty()) astack.pop_back();
                if (!astack.empty()) astack.pop_back();
                if (!astack.empty()) astack.pop_back();
                break;
            }
            case OpCode::OP_CLOSE: {
                uint8_t slot = code[cur + 1];
                if (astack.size() > slot) astack.resize(slot);
                break;
            }
            case OpCode::OP_CLOSE_UPVALUE: {
                if (!astack.empty()) astack.pop_back();
                break;
            }
            default:
                break;
        }
        cur += len;
    }

    AbstractVal target;
    if (operandIndex == 1) {
        if (!astack.empty()) target = astack.back();
    } else {
        if (astack.size() >= 2) target = astack[astack.size() - 2];
        else if (!astack.empty()) target = astack.back();
    }

    if (target.source == AbstractVal::GLOBAL && !target.name.empty()) {
        return " (global '" + target.name + "')";
    } else if (target.source == AbstractVal::LOCAL && !target.name.empty()) {
        return " (local '" + target.name + "')";
    } else if (target.source == AbstractVal::UPVALUE && !target.name.empty()) {
        return " (upvalue '" + target.name + "')";
    } else if (target.source == AbstractVal::FIELD && !target.name.empty()) {
        return " (field '" + target.name + "')";
    }
    return "";
}

bool VM::run(size_t targetFrameCount) {
    // Main execution loop
    while (true) {
        if (currentCoroutine_->status == CoroutineObject::Status::SUSPENDED) {
            return true;
        }
        if (currentCoroutine_->frames.size() <= targetFrameCount) {
            return !hadError_;
        }

        if (interrupted_) {
            interrupted_ = 0;
            setupSigintHandler();
            runtimeError("interrupted!");
        }

        // Handle debug hooks
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
                    currentLine = currentFrame().chunk->getLine(currentFrame().ip);
                    if (currentLine != currentCoroutine_->lastLine) {
                        triggerLine = true;
                        currentCoroutine_->lastLine = currentLine;
                    }
                }
            }

            if (triggerCount) callHook("count");
            if (hadError_) return false;
            
            if (triggerLine) callHook("line", currentLine);
            if (hadError_) return false;
        }

        if (traceExecution_) {
            traceExecution();
        }

#ifdef USE_JIT
        if (currentFrame().closure && currentFrame().ip == 0) {
            JITFunc jitCode = currentFrame().closure->function()->getJITCode();
            if (isJitEnabled() && jitCode) {
                size_t entryIp = currentFrame().ip;
                isJitExecuting_ = true;
                int64_t res = jitCode(this);
                isJitExecuting_ = false;
                if (hadError_) {
                    throw RuntimeError(lastErrorMessage_);
                }
                if (currentCoroutine_->status == CoroutineObject::Status::DEAD || 
                    currentCoroutine_->status == CoroutineObject::Status::SUSPENDED) {
                    return true;
                }
                if (res == -2) {
                    continue;
                }
                if (res >= 0 && static_cast<size_t>(res) != entryIp) {
                    currentFrame().ip = static_cast<size_t>(res);
                    continue;
                }
                // If res == entryIp, it didn't do anything, fall through to interpreter
            }
        }
#endif
        
        uint8_t instruction = readByte();
        OpCode op = static_cast<OpCode>(instruction);

        try {
        switch (op) {
            case OpCode::OP_CONSTANT: {
                Value constant = readConstant();
                push(constant);
                break;
            }
            case OpCode::OP_CONSTANT_LONG: {
                uint32_t index = readByte();
                index |= (readByte() << 8);
                index |= (readByte() << 16);
                push(getConstant(index));
                break;
            }

            case OpCode::OP_NIL:
                push(Value::nil());
                break;

            case OpCode::OP_TRUE:
                push(Value::boolean(true));
                break;

            case OpCode::OP_FALSE:
                push(Value::boolean(false));
                break;

            case OpCode::OP_GET_GLOBAL: {
                uint8_t nameIndex = readByte();
                const std::string& varName = currentFrame().chunk->getIdentifier(nameIndex);
                auto it = globals_.find(varName);
                if (it == globals_.end()) {
                    // Try to look up in _G table if it exists
                    auto git = globals_.find("_G");
                    if (git != globals_.end() && git->second.isTable()) {
                        Value val = git->second.asTableObj()->get(varName);
                        if (!val.isNil()) {
                            push(val);
                            break;
                        }
                    }
                    runtimeError("Undefined variable '" + varName + "'");
                    push(Value::nil());
                } else {
                    push(it->second);
                }
                break;
            }

            case OpCode::OP_SET_GLOBAL: {
                uint8_t nameIndex = readByte();
                const std::string& varName = currentFrame().chunk->getIdentifier(nameIndex);
                setGlobal(varName, peek(0));
                break;
            }

            case OpCode::OP_GET_LOCAL: {
                uint8_t slot = readByte();
                // Add stackBase offset if inside a function
                size_t actualSlot = currentCoroutine_->frames.empty() ? slot : (currentFrame().stackBase + slot);
                if (actualSlot >= currentCoroutine_->stack.size()) {
                    runtimeError("Invalid local slot " + std::to_string(actualSlot));
                    push(Value::nil());
                    break;
                }
                push(currentCoroutine_->stack[actualSlot]);
                break;
            }

            case OpCode::OP_SET_LOCAL: {
                uint8_t slot = readByte();
                // Add stackBase offset if inside a function
                size_t actualSlot = currentCoroutine_->frames.empty() ? slot : (currentFrame().stackBase + slot);
                Value val = peek(0);
                if (val.isObj()) writeBarrierBackward(currentCoroutine_, val.asObj());
                currentCoroutine_->stack[actualSlot] = val;
                break;
            }

            case OpCode::OP_GET_UPVALUE: {
                uint8_t upvalueIndex = readByte();
                if (!currentCoroutine_->frames.empty()) {
                    UpvalueObject* upvalue = currentFrame().closure->getUpvalueObj(upvalueIndex);
                    if (upvalue) {
                        push(upvalue->get(currentCoroutine_->stack));
                    } else {
                        push(Value::nil());
                    }
                } else {
                    runtimeError("Upvalue access outside of closure");
                    push(Value::nil());
                }
                break;
            }

            case OpCode::OP_SET_UPVALUE: {
                uint8_t upvalueIndex = readByte();
                if (!currentCoroutine_->frames.empty()) {
                    UpvalueObject* upvalue = currentFrame().closure->getUpvalueObj(upvalueIndex);
                    if (upvalue) {
                        upvalue->set(currentCoroutine_->stack, peek(0));
                    }
                } else {
                    runtimeError("Upvalue access outside of closure");
                }
                break;
            }

            case OpCode::OP_GET_TABUP:
            case OpCode::OP_GET_TABUP_LONG: {
                uint8_t upIndex = readByte();
                Value key;
                if (op == OpCode::OP_GET_TABUP) {
                    key = readConstant();
                } else {
                    uint32_t keyIndex = readByte();
                    keyIndex |= (readByte() << 8);
                    keyIndex |= (readByte() << 16);
                    key = getConstant(keyIndex);
                }

                if (currentCoroutine_->frames.empty() || currentFrame().closure == nullptr) {
                    runtimeError("Upvalue access outside of closure");
                    push(Value::nil());
                    break;
                }

                UpvalueObject* upvalue = currentFrame().closure->getUpvalueObj(upIndex);
                if (upvalue == nullptr && upIndex == 0 && currentFrame().closure->upvalueCount() > 0) {
                    upvalue = currentFrame().closure->getUpvalueObj(0);
                }

                if (upvalue == nullptr) {
                    runtimeError("Invalid upvalue index " + std::to_string(upIndex));
                    push(Value::nil());
                    break;
                }
                Value upTable = upvalue->get(currentCoroutine_->stack);

                Value t = upTable;
                bool done = false;
                for (int loop = 0; loop < 100; loop++) {
                    if (t.isTable()) {
                        TableObject* table = t.asTableObj();
                        Value value = table->get(key);
                        if (!value.isNil()) {
                            push(value);
                            done = true;
                            break;
                        }
                    }

                    Value indexMethod = getMetamethod(t, "__index");
                    if (indexMethod.isNil()) {
                        if (!t.isTable()) {
                            runtimeError("attempt to index a " + t.typeToString() + " value");
                        }
                        push(Value::nil());
                        done = true;
                        break;
                    } else if (indexMethod.isFunction()) {
                        push(indexMethod);
                        push(t);
                        push(key);
                        callValue(2, 2);
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
                    push(Value::nil());
                }
                break;
            }
            case OpCode::OP_SET_TABUP:
            case OpCode::OP_SET_TABUP_LONG: {
                uint8_t upIndex = readByte();
                Value key;
                if (op == OpCode::OP_SET_TABUP) {
                    key = readConstant();
                } else {
                    uint32_t keyIndex = readByte();
                    keyIndex |= (readByte() << 8);
                    keyIndex |= (readByte() << 16);
                    key = getConstant(keyIndex);
                }
                Value value = peek(0); // Peek instead of pop to root it

                if (currentCoroutine_->frames.empty() || currentFrame().closure == nullptr) {
                    runtimeError("Upvalue access outside of closure");
                    break;
                }

                UpvalueObject* upvalue = currentFrame().closure->getUpvalueObj(upIndex);
                if (upvalue == nullptr && upIndex == 0 && currentFrame().closure->upvalueCount() > 0) {
                    upvalue = currentFrame().closure->getUpvalueObj(0);
                }

                if (upvalue == nullptr) {
                    runtimeError("Invalid upvalue index " + std::to_string(upIndex));
                    break;
                }
                Value upTable = upvalue->get(currentCoroutine_->stack);

                Value t = upTable;
                bool done = false;
                for (int loop = 0; loop < 100; loop++) {
                    if (t.isTable()) {
                        TableObject* table = t.asTableObj();
                        if (table->has(key)) {
                            table->set(key, value);
                            pop();
                            done = true;
                            break;
                        }
                    }

                    Value newIndex = getMetamethod(t, "__newindex");
                    if (newIndex.isNil()) {
                        if (t.isTable()) {
                            t.asTableObj()->set(key, value);
                        } else {
                            runtimeError("attempt to index a " + t.typeToString() + " value");
                        }
                        pop();
                        done = true;
                        break;
                    } else if (newIndex.isFunction()) {
                        pop();
                        push(newIndex);
                        push(t);
                        push(key);
                        push(value);
                        callValue(3, 1);
                        done = true;
                        break;
                    } else if (newIndex.isTable()) {
                        t = newIndex;
                    } else {
                        t = newIndex;
                    }
                }
                if (!done) {
                    runtimeError("'__newindex' chain too long; possible loop");
                    pop();
                }
                break;
            }
            case OpCode::OP_CLOSE_UPVALUE: {
                // Close upvalue at top of stack (and TBC variables)
                currentFrame().ip -= 1;
                closeUpvalues(currentCoroutine_->stack.size() - 1);
                if (currentCoroutine_->status == CoroutineObject::Status::SUSPENDED) return true;
                currentFrame().ip += 1;
                pop();
                break;
            }

            case OpCode::OP_TBC: {
                uint8_t slot = readByte();
                uint8_t nameIdx = readByte();
                size_t index = currentFrame().stackBase + slot;
                Value val = currentCoroutine_->stack[index];
                if (!val.isFalsey()) {
                    Value mm = getMetamethod(val, "__close");
                    if (mm.isNil()) {
                        std::string varName = getStringValue(getConstant(nameIdx));
                        runtimeError("variable '" + varName + "' got a non-closable value");
                        break;
                    }
                }
                currentCoroutine_->tbcVariables.push_back(index);
                break;
            }

            case OpCode::OP_CLOSE: {
                uint8_t slot = readByte();
                size_t targetIndex = currentFrame().stackBase + slot;
                currentFrame().ip -= 2;
                closeUpvalues(targetIndex);
                if (currentCoroutine_->status == CoroutineObject::Status::SUSPENDED) return true;
                currentFrame().ip += 2;
                while (currentCoroutine_->stack.size() > targetIndex) {
                    pop();
                }
                break;
            }

            case OpCode::OP_ADD: {
                Value b = pop();
                Value a = pop();
                Value ca = a, cb = b;
                if (coerceToNumber(ca) && coerceToNumber(cb)) {
                    push(add(ca, cb));
                } else if (!callBinaryMetamethod(a, b, "__add")) {
                    runtimeError("attempt to perform arithmetic on " + a.typeToString() + " and " + b.typeToString());
                }
                break;
            }

            case OpCode::OP_SUB: {
                Value b = pop();
                Value a = pop();
                Value ca = a, cb = b;
                if (coerceToNumber(ca) && coerceToNumber(cb)) {
                    push(subtract(ca, cb));
                } else if (!callBinaryMetamethod(a, b, "__sub")) {
                    runtimeError("attempt to perform arithmetic on " + a.typeToString() + " and " + b.typeToString());
                }
                break;
            }

            case OpCode::OP_MUL: {
                Value b = pop();
                Value a = pop();
                Value ca = a, cb = b;
                if (coerceToNumber(ca) && coerceToNumber(cb)) {
                    push(multiply(ca, cb));
                } else if (!callBinaryMetamethod(a, b, "__mul")) {
                    runtimeError("attempt to perform arithmetic on " + a.typeToString() + " and " + b.typeToString());
                }
                break;
            }

            case OpCode::OP_DIV: {
                Value b = pop();
                Value a = pop();
                Value ca = a, cb = b;
                if (coerceToNumber(ca) && coerceToNumber(cb)) {
                    push(divide(ca, cb));
                } else if (!callBinaryMetamethod(a, b, "__div")) {
                    runtimeError("attempt to perform arithmetic on " + a.typeToString() + " and " + b.typeToString());
                }
                break;
            }

            case OpCode::OP_IDIV: {
                Value b = pop();
                Value a = pop();
                Value ca = a, cb = b;
                if (coerceToNumber(ca) && coerceToNumber(cb)) {
                    push(integerDivide(ca, cb));
                } else if (!callBinaryMetamethod(a, b, "__idiv")) {
                    runtimeError("attempt to perform arithmetic on " + a.typeToString() + " and " + b.typeToString());
                }
                break;
            }

            case OpCode::OP_MOD: {
                Value b = pop();
                Value a = pop();
                Value ca = a, cb = b;
                if (coerceToNumber(ca) && coerceToNumber(cb)) {
                    push(modulo(ca, cb));
                } else if (!callBinaryMetamethod(a, b, "__mod")) {
                    runtimeError("attempt to perform arithmetic on " + a.typeToString() + " and " + b.typeToString());
                }
                break;
            }

            case OpCode::OP_POW: {
                Value b = pop();
                Value a = pop();
                Value ca = a, cb = b;
                if (coerceToNumber(ca) && coerceToNumber(cb)) {
                    push(power(ca, cb));
                } else if (!callBinaryMetamethod(a, b, "__pow")) {
                    runtimeError("attempt to perform arithmetic on " + a.typeToString() + " and " + b.typeToString());
                }
                break;
            }

            case OpCode::OP_BAND: {
                Value b = pop();
                Value a = pop();
                int64_t ia, ib;
                bool okA = toIntegerNoString(a, ia);
                bool okB = toIntegerNoString(b, ib);
                if (okA && okB) {
                    push(makeInteger(ia & ib));
                } else if (!callBinaryMetamethod(a, b, "__band")) {
                    if (a.isNumber() && b.isNumber()) {
                        int opIdx = !okA ? 0 : 1;
                        runtimeError("number" + getVarInfo(currentFrame().ip - 1, opIdx) + " has no integer representation");
                    } else {
                        runtimeError("attempt to perform bitwise operation on " + a.typeToString() + " and " + b.typeToString());
                    }
                }
                break;
            }

            case OpCode::OP_BOR: {
                Value b = pop();
                Value a = pop();
                int64_t ia, ib;
                bool okA = toIntegerNoString(a, ia);
                bool okB = toIntegerNoString(b, ib);
                if (okA && okB) {
                    push(makeInteger(ia | ib));
                } else if (!callBinaryMetamethod(a, b, "__bor")) {
                    if (a.isNumber() && b.isNumber()) {
                        int opIdx = !okA ? 0 : 1;
                        runtimeError("number" + getVarInfo(currentFrame().ip - 1, opIdx) + " has no integer representation");
                    } else {
                        runtimeError("attempt to perform bitwise operation on " + a.typeToString() + " and " + b.typeToString());
                    }
                }
                break;
            }

            case OpCode::OP_BXOR: {
                Value b = pop();
                Value a = pop();
                int64_t ia, ib;
                bool okA = toIntegerNoString(a, ia);
                bool okB = toIntegerNoString(b, ib);
                if (okA && okB) {
                    push(makeInteger(ia ^ ib));
                } else if (!callBinaryMetamethod(a, b, "__bxor")) {
                    if (a.isNumber() && b.isNumber()) {
                        int opIdx = !okA ? 0 : 1;
                        runtimeError("number" + getVarInfo(currentFrame().ip - 1, opIdx) + " has no integer representation");
                    } else {
                        runtimeError("attempt to perform bitwise operation on " + a.typeToString() + " and " + b.typeToString());
                    }
                }
                break;
            }

            case OpCode::OP_SHL: {
                Value b = pop();
                Value a = pop();
                int64_t ia, ib;
                bool okA = toIntegerNoString(a, ia);
                bool okB = toIntegerNoString(b, ib);
                if (okA && okB) {
                    push(shiftLeft(a, b));
                } else if (!callBinaryMetamethod(a, b, "__shl")) {
                    if (a.isNumber() && b.isNumber()) {
                        int opIdx = !okA ? 0 : 1;
                        runtimeError("number" + getVarInfo(currentFrame().ip - 1, opIdx) + " has no integer representation");
                    } else {
                        runtimeError("attempt to perform bitwise operation on " + a.typeToString() + " and " + b.typeToString());
                    }
                }
                break;
            }

            case OpCode::OP_SHR: {
                Value b = pop();
                Value a = pop();
                int64_t ia, ib;
                bool okA = toIntegerNoString(a, ia);
                bool okB = toIntegerNoString(b, ib);
                if (okA && okB) {
                    push(shiftRight(a, b));
                } else if (!callBinaryMetamethod(a, b, "__shr")) {
                    if (a.isNumber() && b.isNumber()) {
                        int opIdx = !okA ? 0 : 1;
                        runtimeError("number" + getVarInfo(currentFrame().ip - 1, opIdx) + " has no integer representation");
                    } else {
                        runtimeError("attempt to perform bitwise operation on " + a.typeToString() + " and " + b.typeToString());
                    }
                }
                break;
            }

            case OpCode::OP_CONCAT: {
                Value b = pop();
                Value a = pop();
                if ((a.isString() || a.isNumber()) && (b.isString() || b.isNumber())) {
                    push(concat(a, b));
                } else if (!callBinaryMetamethod(a, b, "__concat")) {
                    runtimeError("attempt to concatenate " + a.typeToString() + " and " + b.typeToString());
                }
                break;
            }

            case OpCode::OP_NEG: {
                Value a = pop();
                Value ca = a;
                if (coerceToNumber(ca)) {
                    push(negate(ca));
                } else if (!callBinaryMetamethod(a, a, "__unm")) {
                    runtimeError("attempt to perform arithmetic on " + a.typeToString());
                }
                break;
            }

            case OpCode::OP_NOT: {
                Value a = pop();
                push(logicalNot(a));
                break;
            }

            case OpCode::OP_BNOT: {
                Value a = pop();
                int64_t ia;
                if (toIntegerNoString(a, ia)) {
                    push(makeInteger(~ia));
                } else if (!callBinaryMetamethod(a, a, "__bnot")) { // Unary bitwise NOT
                    if (a.isNumber()) {
                        runtimeError("number" + getVarInfo(currentFrame().ip - 1, 0) + " has no integer representation");
                    } else {
                        runtimeError("attempt to perform bitwise operation on " + a.typeToString());
                    }
                }
                break;
            }

            case OpCode::OP_LEN: {
                Value a = pop();
                if (a.isString()) {
                    push(Value::integer(static_cast<int64_t>(getStringValue(a).length())));
                } else if (a.isTable()) {
                    Value mm = getMetamethod(a, "__len");
                    if (!mm.isNil()) {
                        push(mm);
                        push(a);
                        push(a);
                        callValue(2, 2); // Expect 1 result (1+1=2)
                    } else {
                        push(Value::integer(static_cast<int64_t>(a.asTableObj()->length())));
                    }
                } else {
                    Value mm = getMetamethod(a, "__len");
                    if (!mm.isNil()) {
                        push(mm);
                        push(a);
                        push(a);
                        callValue(2, 2);
                    } else {
                        runtimeError("attempt to get length of a " + a.typeToString() + " value");
                    }
                }
                break;
            }

            case OpCode::OP_EQUAL: {
                Value b = pop();
                Value a = pop();
                if (equal(a, b).asBool()) {
                    push(Value::boolean(true));
                } else if (a.isTable() && b.isTable() && callBinaryMetamethod(a, b, "__eq")) {
                    // Metamethod called, result will be pushed
                } else {
                    push(Value::boolean(false));
                }
                break;
            }

            case OpCode::OP_LESS: {
                Value b = pop();
                Value a = pop();
                if (a.isNumber() && b.isNumber()) {
                    push(less(a, b));
                } else if ((a.isString() || a.isRuntimeString()) && (b.isString() || b.isRuntimeString())) {
                    push(Value::boolean(getStringValue(a) < getStringValue(b)));
                } else if (!callBinaryMetamethod(a, b, "__lt")) {
                    runtimeError("attempt to compare " + a.typeToString() + " and " + b.typeToString());
                }
                break;
            }

            case OpCode::OP_LESS_EQUAL: {
                Value b = pop();
                Value a = pop();
                if (a.isNumber() && b.isNumber()) {
                    push(lessEqual(a, b));
                } else if ((a.isString() || a.isRuntimeString()) && (b.isString() || b.isRuntimeString())) {
                    push(Value::boolean(getStringValue(a) <= getStringValue(b)));
                } else {
                    if (!callBinaryMetamethod(a, b, "__le")) {
                        runtimeError("attempt to compare " + a.typeToString() + " and " + b.typeToString());
                    }
                }
                break;
            }

            case OpCode::OP_GREATER: {
                Value b = pop();
                Value a = pop();
                if (a.isNumber() && b.isNumber()) {
                    push(less(b, a));
                } else if ((a.isString() || a.isRuntimeString()) && (b.isString() || b.isRuntimeString())) {
                    push(Value::boolean(getStringValue(a) > getStringValue(b)));
                } else {
                    if (!callBinaryMetamethod(b, a, "__lt")) {
                        runtimeError("attempt to compare " + a.typeToString() + " and " + b.typeToString());
                    }
                }
                break;
            }

            case OpCode::OP_GREATER_EQUAL: {
                Value b = pop();
                Value a = pop();
                if (a.isNumber() && b.isNumber()) {
                    push(Value::boolean(a.asNumber() >= b.asNumber()));
                } else if ((a.isString() || a.isRuntimeString()) && (b.isString() || b.isRuntimeString())) {
                    push(Value::boolean(getStringValue(a) >= getStringValue(b)));
                } else {
                    if (!callBinaryMetamethod(b, a, "__le")) {
                        runtimeError("attempt to compare " + a.typeToString() + " and " + b.typeToString());
                    }
                }
                break;
            }

            case OpCode::OP_POP:
                pop();
                break;

            case OpCode::OP_DUP:
                push(peek(0));
                break;

            case OpCode::OP_SWAP: {
                Value a = pop();
                Value b = pop();
                push(a);
                push(b);
                break;
            }

            case OpCode::OP_ROTATE: {
                uint8_t n = readByte();
                if (n >= 2 && currentCoroutine_->stack.size() >= n) {
                    // move the n-th value from top to the top of the stack
                    // Stack: [..., v_n, v_n-1, ..., v_1] -> [..., v_n-1, ..., v_1, v_n]
                    auto it_top = currentCoroutine_->stack.end();
                    auto it_dest = currentCoroutine_->stack.end() - n;
                    std::rotate(it_dest, it_dest + 1, it_top);
                }
                break;
            }

            case OpCode::OP_JUMP: {
                uint16_t offset = readByte() | (readByte() << 8);
                currentFrame().ip += offset;
                break;
            }

            case OpCode::OP_JUMP_IF_FALSE: {
                uint16_t offset = readByte() | (readByte() << 8);
                if (peek(0).isFalsey()) {
                    currentFrame().ip += offset;
                }
                break;
            }

            case OpCode::OP_LOOP: {
                uint16_t offset = readByte() | (readByte() << 8);
                currentFrame().ip -= offset;

                // JIT Hotness tracking
                if (currentFrame().closure) {
                    FunctionObject* func = currentFrame().closure->function();
                    if (isJitEnabled() && !func->getJITCode() && func->incrementHotness() >= 50) {
#ifdef USE_JIT
                        if (!jit()->compile(func)) {
                            // If compilation failed, reset hotness to prevent immediate retry
                            func->resetHotness(-1000); 
                        }
#endif
                    }
                }
                break;
            }

            case OpCode::OP_CLOSURE: {
                uint8_t constantIndex = readByte();
                Value funcValue = currentFrame().chunk->constants()[constantIndex];
                size_t funcIndex = funcValue.asFunctionIndex();
                FunctionObject* function = currentFrame().chunk->getFunction(funcIndex);

                ClosureObject* closure = createClosure(function);

                // Capture upvalues
                for (size_t i = 0; i < closure->upvalueCount(); i++) {
                    uint8_t isLocal = readByte();
                    uint8_t index = readByte();

                    if (isLocal) {
                        size_t stackIndex = currentFrame().stackBase + index;
                        UpvalueObject* upvalue = captureUpvalue(stackIndex);
                        closure->setUpvalue(i, upvalue);
                    } else {
                        UpvalueObject* upvalue = currentFrame().closure->getUpvalueObj(index);
                        closure->setUpvalue(i, upvalue);
                    }
                }

                push(Value::closure(closure));
                break;
            }

            case OpCode::OP_CLOSURE_LONG: {
                uint32_t constantIndex = readByte();
                constantIndex |= (readByte() << 8);
                constantIndex |= (readByte() << 16);
                Value funcValue = currentFrame().chunk->constants()[constantIndex];
                size_t funcIndex = funcValue.asFunctionIndex();
                FunctionObject* function = currentFrame().chunk->getFunction(funcIndex);

                ClosureObject* closure = createClosure(function);

                // Capture upvalues
                for (size_t i = 0; i < closure->upvalueCount(); i++) {
                    uint8_t isLocal = readByte();
                    uint8_t index = readByte();

                    if (isLocal) {
                        size_t stackIndex = currentFrame().stackBase + index;
                        UpvalueObject* upvalue = captureUpvalue(stackIndex);
                        closure->setUpvalue(i, upvalue);
                    } else {
                        UpvalueObject* upvalue = currentFrame().closure->getUpvalueObj(index);
                        closure->setUpvalue(i, upvalue);
                    }
                }

                push(Value::closure(closure));
                break;
            }

            case OpCode::OP_CALL: {
                uint8_t argCount = readByte();
                uint8_t retCount = readByte();  // Number of return values to keep (0 = all)
                
                // JIT Hotness tracking
                Value callee = peek(argCount);
                if (callee.isClosure()) {
                    FunctionObject* func = callee.asClosureObj()->function();
                    if (isJitEnabled() && !func->getJITCode() && func->incrementHotness() >= 10) {
#ifdef USE_JIT
                        if (!jit()->compile(func)) {
                            func->resetHotness(-1000); 
                        }
#endif
                    }
                }

                size_t prevFrames = currentCoroutine_->frames.size();
                if (!callValue(argCount, retCount)) {
                    return false;
                }
                if (currentCoroutine_->status == CoroutineObject::Status::SUSPENDED) return true;
                // If it was a Lua call, trigger hook
                if (currentCoroutine_->frames.size() > prevFrames && 
                    (currentCoroutine_->hookMask & CoroutineObject::MASK_CALL)) {
                    callHook("call");
                }
                break;
            }

            case OpCode::OP_CALL_MULTI: {
                uint8_t fixedArgCount = readByte();
                uint8_t retCount = readByte();
                // actual argCount = fixedArgs + lastResultCount
                int actualArgCount = static_cast<int>(fixedArgCount) + static_cast<int>(currentCoroutine_->lastResultCount);

                // JIT Hotness tracking
                Value callee = peek(actualArgCount);
                if (callee.isClosure()) {
                    FunctionObject* func = callee.asClosureObj()->function();
                    if (isJitEnabled() && !func->getJITCode() && func->incrementHotness() >= 10) {
#ifdef USE_JIT
                        if (!jit()->compile(func)) {
                            func->resetHotness(-1000); 
                        }
#endif
                    }
                }

                size_t prevFrames = currentCoroutine_->frames.size();
                if (!callValue(actualArgCount, retCount)) {
                    return false;
                }
                if (currentCoroutine_->status == CoroutineObject::Status::SUSPENDED) return true;
                // If it was a Lua call, trigger hook
                if (currentCoroutine_->frames.size() > prevFrames && 
                    (currentCoroutine_->hookMask & CoroutineObject::MASK_CALL)) {
                    callHook("call");
                }
                break;
            }

            case OpCode::OP_TAILCALL: {
                uint8_t argCount = readByte();
                // A tailcall always expects ALL return values (retCount = 0)
                size_t prevFrames = currentCoroutine_->frames.size();
                if (!callValue(argCount, 0, true)) {
                    return false;
                }
                // If it was a Lua call, trigger hook
                if (currentCoroutine_->frames.size() > prevFrames && 
                    (currentCoroutine_->hookMask & CoroutineObject::MASK_CALL)) {
                    callHook("call");
                }
                break;
            }

            case OpCode::OP_TAILCALL_MULTI: {
                uint8_t fixedArgCount = readByte();
                int actualArgCount = static_cast<int>(fixedArgCount) + static_cast<int>(currentCoroutine_->lastResultCount);
                size_t prevFrames = currentCoroutine_->frames.size();
                if (!callValue(actualArgCount, 0, true)) {
                    return false;
                }
                // If it was a Lua call, trigger hook
                if (currentCoroutine_->frames.size() > prevFrames && 
                    (currentCoroutine_->hookMask & CoroutineObject::MASK_CALL)) {
                    callHook("call");
                }
                break;
            }

            case OpCode::OP_RETURN_VALUE:
            case OpCode::OP_RETURN_VALUE_MULTI: {
                uint8_t operand = readByte();

                if (!currentFrame().isReturning) {
                    currentFrame().isReturning = true;
                    size_t actualCount;
                    if (op == OpCode::OP_RETURN_VALUE_MULTI) {
                        actualCount = static_cast<size_t>(operand) + currentCoroutine_->lastResultCount;
                    } else {
                        actualCount = operand;
                    }

                    // Pop all return values from the stack (in reverse order)
                    std::vector<Value> returnValues;
                    returnValues.reserve(actualCount);
                    for (size_t i = 0; i < actualCount; i++) {
                        returnValues.push_back(pop());
                    }
                    // Reverse so they're in correct order
                    std::reverse(returnValues.begin(), returnValues.end());

                    currentCoroutine_->pendingReturns.push_back(std::move(returnValues));
                }

                size_t stackBase = currentFrame().stackBase;
                currentFrame().ip -= 2;
                closeUpvalues(stackBase);
                
                if (currentCoroutine_->status == CoroutineObject::Status::SUSPENDED) {
                    return true;
                }
                currentFrame().ip += 2;

                std::vector<Value> returnValues = std::move(currentCoroutine_->pendingReturns.back());
                currentCoroutine_->pendingReturns.pop_back();
                currentFrame().isReturning = false;

                // Handle debug hook before returning
                if (currentCoroutine_->hookMask & CoroutineObject::MASK_RET) {
                    callHook("return");
                }

                if (currentFrame().isPcall) {
                    returnValues.insert(returnValues.begin(), Value::boolean(true));
                }

                // Get the expected return count from the call frame
                uint8_t expectedRetCount = currentFrame().retCount;

                // Adjust return values based on what caller expects
                if (expectedRetCount > 0) {
                    size_t expected = static_cast<size_t>(expectedRetCount - 1);
                    if (returnValues.size() > expected) {
                        // Keep only the first expected values
                        returnValues.resize(expected);
                    } else if (returnValues.size() < expected) {
                        // Pad with nils if we returned fewer than expected
                        while (returnValues.size() < expected) {
                            returnValues.push_back(Value::nil());
                        }
                    }
                }
                // If expectedRetCount == 0, keep all values (no adjustment)

                // Pop all remaining locals and arguments (down to stackBase)
                while (currentCoroutine_->stack.size() > stackBase) {
                    pop();
                }

                // Also pop the closure object itself (it's at stackBase - 1)
                pop();

                // Get return state before popping frame
                const Chunk* returnChunk = currentFrame().callerChunk;

                // Pop call frame
                currentCoroutine_->frames.pop_back();

                // Check if we hit the target frame count (for pcall/load) or if this was the last frame
                bool shouldExit = false;
                if (targetFrameCount > 0) {
                    shouldExit = (currentCoroutine_->frames.size() <= targetFrameCount);
                } else {
                    shouldExit = currentCoroutine_->frames.empty();
                }

                if (shouldExit) {
                    if (currentCoroutine_->frames.empty()) {
                        currentCoroutine_->status = CoroutineObject::Status::DEAD;
                        currentCoroutine_->chunk = nullptr;
                    } else {
                        currentCoroutine_->chunk = returnChunk;
                    }
                    
                    // Push all return values before returning
                    currentCoroutine_->lastResultCount = returnValues.size();
                    for (const auto& value : returnValues) {
                        push(value);
                    }
                    return !hadError_;
                }

                while (!currentCoroutine_->frames.empty() && currentFrame().isC) {
                    CallFrame cframe = currentFrame();
                    if (cframe.isErrorUnwinding) {
                        if (!currentCoroutine_->tbcVariables.empty() && currentCoroutine_->tbcVariables.back() >= cframe.stackBase) {
                            Value errObj = !currentCoroutine_->closeError.isNil() ? currentCoroutine_->closeError :
                                           (!lastErrorObject_.isNil() ? lastErrorObject_ : Value::runtimeString(internString(lastErrorMessage_)));
                            try {
                                closeUpvalues(cframe.stackBase, nullptr, errObj);
                            } catch (const RuntimeError& e) {
                                errObj = lastErrorObject_.isNil() ? Value::runtimeString(internString(e.what())) : lastErrorObject_;
                                hadError_ = false;
                                currentCoroutine_->closeError = errObj;
                            } catch (const std::exception& e) {
                                errObj = Value::runtimeString(internString(e.what()));
                                hadError_ = false;
                                currentCoroutine_->closeError = errObj;
                            }
                            if (currentCoroutine_->status == CoroutineObject::Status::SUSPENDED) {
                                return true;
                            }
                        }
                        Value errObj = !currentCoroutine_->closeError.isNil() ? currentCoroutine_->closeError :
                                       (!lastErrorObject_.isNil() ? lastErrorObject_ : Value::runtimeString(internString(lastErrorMessage_)));
                        currentCoroutine_->frames.pop_back();
                        currentCoroutine_->closeError = Value::nil();
                        lastErrorObject_ = Value::nil();
                        hadError_ = false;
                        isHandlingError_ = false;

                        while (currentCoroutine_->stack.size() > cframe.stackBase - 1) {
                            pop();
                        }

                        returnValues.clear();
                        returnValues.push_back(Value::boolean(false));
                        returnValues.push_back(errObj);

                        uint8_t cRetCount = cframe.retCount;
                        if (cRetCount > 0) {
                            size_t expected = static_cast<size_t>(cRetCount - 1);
                            if (returnValues.size() > expected) {
                                returnValues.resize(expected);
                            } else {
                                while (returnValues.size() < expected) {
                                    returnValues.push_back(Value::nil());
                                }
                            }
                        }

                        if (targetFrameCount > 0 && currentCoroutine_->frames.size() <= targetFrameCount) {
                            shouldExit = true;
                        } else if (currentCoroutine_->frames.empty()) {
                            shouldExit = true;
                            currentCoroutine_->status = CoroutineObject::Status::DEAD;
                        }

                        if (shouldExit) {
                            currentCoroutine_->lastResultCount = returnValues.size();
                            for (const auto& value : returnValues) {
                                push(value);
                            }
                            return true;
                        }
                        continue;
                    }

                    currentCoroutine_->frames.pop_back();

                    if (cframe.isPcall) {
                        returnValues.insert(returnValues.begin(), Value::boolean(true));
                    }

                    uint8_t cRetCount = cframe.retCount;
                    if (cRetCount > 0) {
                        size_t expected = static_cast<size_t>(cRetCount - 1);
                        if (returnValues.size() > expected) {
                            returnValues.resize(expected);
                        } else {
                            while (returnValues.size() < expected) {
                                returnValues.push_back(Value::nil());
                            }
                        }
                    }

                    while (currentCoroutine_->stack.size() > cframe.stackBase - 1) {
                        pop();
                    }

                    if (targetFrameCount > 0 && currentCoroutine_->frames.size() <= targetFrameCount) {
                        shouldExit = true;
                    } else if (currentCoroutine_->frames.empty()) {
                        shouldExit = true;
                        currentCoroutine_->status = CoroutineObject::Status::DEAD;
                    }

                    if (shouldExit) {
                        currentCoroutine_->lastResultCount = returnValues.size();
                        for (const auto& value : returnValues) {
                            push(value);
                        }
                        return !hadError_;
                    }
                }

                // Restore execution state
                if (!currentCoroutine_->frames.empty()) {
                    currentCoroutine_->chunk = currentFrame().chunk;
                } else {
                    currentCoroutine_->chunk = returnChunk;
                }
                // Note: currentFrame().ip is now the caller's ip

                // Set lastResultCount before pushing so it matches the number of returned values
                currentCoroutine_->lastResultCount = returnValues.size();

                // Push all return values (replaces where function was)
                for (const auto& value : returnValues) {
                    push(value);
                }
                break;
            }

            case OpCode::OP_NEW_TABLE: {
                TableObject* table = createTable();
                push(Value::table(table));
                break;
            }

            case OpCode::OP_GET_TABLE: {
                Value key = pop();
                Value tableValue = pop();

                Value t = tableValue;
                bool done = false;
                for (int loop = 0; loop < 100; loop++) {
                    if (t.isTable()) {
                        TableObject* table = t.asTableObj();
                        Value value = table->get(key);
                        if (!value.isNil() || table->getMetatable().isNil()) {
                            push(value);
                            done = true;
                            break;
                        }
                    }

                    if (key.isString()) {
                        Value mm = getMetamethod(t, getStringValue(key));
                        if (!mm.isNil()) {
                            push(mm);
                            done = true;
                            break;
                        }
                    }

                    Value indexMethod = getMetamethod(t, "__index");
                    if (indexMethod.isNil()) {
                        if (!t.isTable()) {
                            runtimeError("attempt to index a " + t.typeToString() + " value");
                        }
                        push(Value::nil());
                        done = true;
                        break;
                    } else if (indexMethod.isFunction()) {
                        push(indexMethod);
                        push(t);
                        push(key);
                        callValue(2, 2); // Expect 1 result (1 + 1 = 2)
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
                    push(Value::nil());
                }
                break;
            }

            case OpCode::OP_SET_TABLE: {
                Value value = peek(0);
                Value key = peek(1);
                Value tableValue = peek(2);

                Value t = tableValue;
                bool done = false;
                for (int loop = 0; loop < 100; loop++) {
                    if (t.isTable()) {
                        TableObject* table = t.asTableObj();
                        if (table->getMetatable().isNil() || table->has(key)) {
                            if (key.isNil()) {
                                runtimeError("table index is nil");
                                pop(); pop(); pop();
                                done = true;
                                break;
                            }
                            if (key.isFloat() && std::isnan(key.asNumber())) {
                                runtimeError("table index is NaN");
                                pop(); pop(); pop();
                                done = true;
                                break;
                            }
                            table->set(key, value);
                            pop(); pop(); pop();
                            done = true;
                            break;
                        }
                    }

                    Value newIndex = getMetamethod(t, "__newindex");
                    if (newIndex.isNil()) {
                        if (t.isTable()) {
                            if (key.isNil()) {
                                runtimeError("table index is nil");
                                pop(); pop(); pop();
                                done = true;
                                break;
                            }
                            if (key.isFloat() && std::isnan(key.asNumber())) {
                                runtimeError("table index is NaN");
                                pop(); pop(); pop();
                                done = true;
                                break;
                            }
                            TableObject* table = t.asTableObj();
                            table->set(key, value);
                        } else {
                            runtimeError("attempt to index a " + t.typeToString() + " value");
                        }
                        pop(); pop(); pop();
                        done = true;
                        break;
                    } else if (newIndex.isFunction()) {
                        currentCoroutine_->stack[currentCoroutine_->stack.size() - 3] = t;
                        currentCoroutine_->stack.insert(currentCoroutine_->stack.end() - 3, newIndex);
                        callValue(3, 1); // Expect 0 results (0 + 1 = 1)
                        done = true;
                        break;
                    } else if (newIndex.isTable()) {
                        t = newIndex;
                    } else {
                        t = newIndex;
                    }
                }
                if (!done) {
                    runtimeError("'__newindex' chain too long; possible loop");
                    pop(); pop(); pop();
                }
                break;
            }

            case OpCode::OP_GET_VARARG: {
                uint8_t retCount = readByte();
                // Push varargs onto the stack
                if (currentCoroutine_->frames.empty()) {
                    runtimeError("Cannot access varargs outside of a function");
                    break;
                }

                CallFrame& frame = currentFrame();
                FunctionObject* func = frame.closure ? frame.closure->function() : nullptr;

                if (func && func->hasNamedVarargs() && !func->isVarargOptimized()) {
                    int slot = func->namedVarargSlot();
                    Value tblVal = currentCoroutine_->stack[frame.stackBase + slot];
                    if (!tblVal.isTable()) {
                        runtimeError("vararg table has no proper 'n'");
                        break;
                    }
                    TableObject* tbl = tblVal.asTableObj();
                    Value nVal = tbl->get("n");
                    if (!nVal.isInteger()) {
                        runtimeError("vararg table has no proper 'n'");
                        break;
                    }
                    int64_t n = nVal.asInteger();
                    if (n < 0 || n > 1000000) {
                        runtimeError("vararg table has no proper 'n'");
                        break;
                    }

                    if (retCount == 0) {
                        for (int64_t i = 1; i <= n; i++) {
                            push(tbl->get(Value::integer(i)));
                        }
                        currentCoroutine_->lastResultCount = static_cast<size_t>(n);
                    } else {
                        int count = (int)retCount - 1;
                        for (int i = 0; i < count; i++) {
                            if (i < n) {
                                push(tbl->get(Value::integer(static_cast<int64_t>(i + 1))));
                            } else {
                                push(Value::nil());
                            }
                        }
                        currentCoroutine_->lastResultCount = count;
                    }
                } else {
                    const auto& varargs = frame.varargs;
                    
                    if (retCount == 0) {
                        // Push all varargs
                        for (size_t i = 0; i < varargs.size(); i++) {
                            push(varargs[i]);
                        }
                        currentCoroutine_->lastResultCount = varargs.size();
                    } else {
                        // Push exactly retCount - 1 values
                        int count = (int)retCount - 1;
                        for (int i = 0; i < count; i++) {
                            if (i < (int)varargs.size()) {
                                push(varargs[i]);
                            } else {
                                push(Value::nil());
                            }
                        }
                        currentCoroutine_->lastResultCount = count;
                    }
                }
                break;
            }

            case OpCode::OP_PACK_VARARG_TABLE: {
                CallFrame& frame = currentFrame();
                TableObject* tbl = createTable();
                const auto& varargs = frame.varargs;
                for (size_t i = 0; i < varargs.size(); i++) {
                    tbl->set(Value::integer(static_cast<int64_t>(i + 1)), varargs[i]);
                }
                tbl->set("n", Value::integer(static_cast<int64_t>(varargs.size())));
                push(Value::table(tbl));
                break;
            }

            case OpCode::OP_GET_VARARG_ITEM: {
                Value key = pop();
                CallFrame& frame = currentFrame();
                const auto& varargs = frame.varargs;
                
                if (key.isStringEqual("n")) {
                    push(Value::integer(static_cast<int64_t>(varargs.size())));
                } else if (key.isInteger()) {
                    int64_t idx = key.asInteger();
                    if (idx >= 1 && static_cast<size_t>(idx) <= varargs.size()) {
                        push(varargs[idx - 1]);
                    } else {
                        push(Value::nil());
                    }
                } else if (key.isNumber()) {
                    double num = key.asNumber();
                    int64_t idx = static_cast<int64_t>(num);
                    if (static_cast<double>(idx) == num && idx >= 1 && static_cast<size_t>(idx) <= varargs.size()) {
                        push(varargs[idx - 1]);
                    } else {
                        push(Value::nil());
                    }
                } else {
                    push(Value::nil());
                }
                break;
            }

            case OpCode::OP_GET_VARARG_COUNT: {
                CallFrame& frame = currentFrame();
                push(Value::integer(static_cast<int64_t>(frame.varargs.size())));
                break;
            }

            case OpCode::OP_DEF_GLOBAL:
            case OpCode::OP_DEF_GLOBAL_LONG: {
                uint8_t upIndex = readByte();
                Value key;
                if (op == OpCode::OP_DEF_GLOBAL) {
                    key = readConstant();
                } else {
                    uint32_t keyIndex = readByte();
                    keyIndex |= (readByte() << 8);
                    keyIndex |= (readByte() << 16);
                    key = getConstant(keyIndex);
                }
                Value value = peek(0);

                if (currentCoroutine_->frames.empty() || currentFrame().closure == nullptr) {
                    runtimeError("Upvalue access outside of closure");
                    break;
                }

                UpvalueObject* upvalue = currentFrame().closure->getUpvalueObj(upIndex);
                if (upvalue == nullptr && upIndex == 0 && currentFrame().closure->upvalueCount() > 0) {
                    upvalue = currentFrame().closure->getUpvalueObj(0);
                }

                if (upvalue == nullptr) {
                    runtimeError("Invalid upvalue index " + std::to_string(upIndex));
                    break;
                }
                Value upTable = upvalue->get(currentCoroutine_->stack);

                if (!upTable.isTable()) {
                    runtimeError("attempt to index a " + upTable.typeToString() + " value");
                    pop();
                    break;
                }
                TableObject* table = upTable.asTableObj();
                std::string keyStr = getStringValue(key);
                if (!table->get(keyStr).isNil()) {
                    runtimeError("global '" + keyStr + "' already defined");
                    pop();
                    break;
                }
                table->set(keyStr, value);
                pop();
                break;
            }

            case OpCode::OP_DEF_GLOBAL_TABLE: {
                // Stack: [value, env_table, key]
                Value key = pop();
                Value envTable = pop();
                Value value = pop();

                if (!envTable.isTable()) {
                    runtimeError("attempt to index a " + envTable.typeToString() + " value");
                    break;
                }
                TableObject* table = envTable.asTableObj();
                std::string keyStr = getStringValue(key);
                if (!table->get(keyStr).isNil()) {
                    runtimeError("global '" + keyStr + "' already defined");
                    break;
                }
                table->set(keyStr, value);
                break;
            }

            case OpCode::OP_SET_TABLE_MULTI: {
                // Stack: [..., table, key_base, val1, val2, ..., valN]
                // key_base is the FIRST numeric key to start with.
                // N is lastResultCount.
                size_t n = currentCoroutine_->lastResultCount;
                
                std::vector<Value> values;
                values.reserve(n);
                for (size_t i = 0; i < n; i++) {
                    values.push_back(pop());
                }
                std::reverse(values.begin(), values.end());
                
                Value keyBaseVal = pop();
                Value tableValue = pop();
                
                if (!tableValue.isTable()) {
                    runtimeError("Attempt to index a non-table value");
                    break;
                }
                
                TableObject* table = tableValue.asTableObj();
                double keyBase = keyBaseVal.asNumber();
                
                for (size_t i = 0; i < n; i++) {
                    table->set(Value::number(keyBase + i), values[i]);
                }
                break;
            }

            case OpCode::OP_YIELD:
            case OpCode::OP_YIELD_MULTI: {
                uint8_t operand = readByte();
                uint8_t retCount = readByte();

                if (!currentCoroutine_->caller || currentCoroutine_ == mainCoroutine_) {
                    runtimeError("attempt to yield from outside a coroutine");
                    return false;
                }
                if (currentCoroutine_->nonYieldableCount > 0 || currentCoroutine_->isClosing) {
                    runtimeError("attempt to yield across a C-call boundary");
                    return false;
                }

                size_t actualCount;
                if (op == OpCode::OP_YIELD_MULTI) {
                    actualCount = static_cast<size_t>(operand) + currentCoroutine_->lastResultCount;
                } else {
                    actualCount = operand;
                }
                
                // Pop yielded values and save them
                currentCoroutine_->yieldedValues.clear();
                for (size_t i = 0; i < actualCount; i++) {
                    currentCoroutine_->yieldedValues.push_back(pop());
                }
                // Reverse so they are in original order
                std::reverse(currentCoroutine_->yieldedValues.begin(), currentCoroutine_->yieldedValues.end());

                if (currentCoroutine_->hookMask & CoroutineObject::MASK_CALL) {
                    callHook("call");
                }

                currentCoroutine_->status = CoroutineObject::Status::SUSPENDED;
                currentCoroutine_->yieldCount = actualCount;
                currentCoroutine_->retCount = retCount;
                if (currentCoroutine_->hookMask & CoroutineObject::MASK_RET) {
                    callHook("return");
                }
                return true; // Return to resumer
            }

            case OpCode::OP_FORPREP: {
                uint8_t base = readByte();
                uint16_t offset = readByte() | (readByte() << 8);
                size_t actualBase = currentCoroutine_->frames.empty() ? base : (currentFrame().stackBase + base);
                if (actualBase + 3 >= currentCoroutine_->stack.size()) {
                    runtimeError("Invalid stack for numeric for");
                    break;
                }
                Value& v_init = currentCoroutine_->stack[actualBase];
                Value& v_limit = currentCoroutine_->stack[actualBase + 1];
                Value& v_step = currentCoroutine_->stack[actualBase + 2];
                Value& v_ext = currentCoroutine_->stack[actualBase + 3];

                auto convertVal = [this](const Value& v, const char* name, int64_t& outI, double& outD, bool& isInt) -> bool {
                    if (v.isInteger()) {
                        outI = v.asInteger();
                        outD = static_cast<double>(outI);
                        isInt = true;
                        return true;
                    } else if (v.isNumber()) {
                        outD = v.asNumber();
                        isInt = false;
                        return true;
                    } else if (v.isString()) {
                        if (stringToNumber(getStringValue(v), outD, outI, isInt)) {
                            return true;
                        }
                    }
                    runtimeError(std::string("'for' ") + name + " must be a number");
                    return false;
                };

                int64_t initI = 0, limitI = 0, stepI = 0;
                double initD = 0.0, limitD = 0.0, stepD = 0.0;
                bool initIsInt = false, limitIsInt = false, stepIsInt = false;

                if (!convertVal(v_init, "initial value", initI, initD, initIsInt)) break;
                if (!convertVal(v_limit, "limit", limitI, limitD, limitIsInt)) break;
                if (!convertVal(v_step, "step", stepI, stepD, stepIsInt)) break;

                if ((stepIsInt && stepI == 0) || (!stepIsInt && stepD == 0.0)) {
                    runtimeError("'for' step is zero");
                    break;
                }

                if (initIsInt && stepIsInt) {
                    bool skipLoop = false;
                    if (limitIsInt) {
                        if (stepI > 0 ? (initI > limitI) : (initI < limitI)) {
                            skipLoop = true;
                        }
                    } else {
                        if (std::isnan(limitD)) {
                            skipLoop = true;
                        } else if (stepI > 0) {
                            if (limitD < static_cast<double>(std::numeric_limits<int64_t>::min())) {
                                skipLoop = true;
                            } else if (limitD >= static_cast<double>(std::numeric_limits<int64_t>::max())) {
                                limitI = std::numeric_limits<int64_t>::max();
                                if (initI > limitI) skipLoop = true;
                            } else {
                                limitI = static_cast<int64_t>(std::floor(limitD));
                                if (initI > limitI) skipLoop = true;
                            }
                        } else {
                            if (limitD > static_cast<double>(std::numeric_limits<int64_t>::max())) {
                                skipLoop = true;
                            } else if (limitD <= static_cast<double>(std::numeric_limits<int64_t>::min())) {
                                limitI = std::numeric_limits<int64_t>::min();
                                if (initI < limitI) skipLoop = true;
                            } else {
                                limitI = static_cast<int64_t>(std::ceil(limitD));
                                if (initI < limitI) skipLoop = true;
                            }
                        }
                    }

                    if (skipLoop) {
                        currentFrame().ip += offset;
                        break;
                    }

                    v_init = makeInteger(initI);
                    v_limit = makeInteger(limitI);
                    v_step = makeInteger(stepI);
                    if (v_init.isObj()) writeBarrierBackward(currentCoroutine_, v_init.asObj());
                    if (v_limit.isObj()) writeBarrierBackward(currentCoroutine_, v_limit.asObj());
                    if (v_step.isObj()) writeBarrierBackward(currentCoroutine_, v_step.asObj());
                    v_ext = v_init;
                } else {
                    double initF = initIsInt ? static_cast<double>(initI) : initD;
                    double limitF = limitIsInt ? static_cast<double>(limitI) : limitD;
                    double stepF = stepIsInt ? static_cast<double>(stepI) : stepD;

                    bool skipLoop = false;
                    if (std::isnan(initF) || std::isnan(limitF) || std::isnan(stepF)) {
                        skipLoop = true;
                    } else if (stepF > 0 ? (initF > limitF) : (initF < limitF)) {
                        skipLoop = true;
                    }

                    if (skipLoop) {
                        currentFrame().ip += offset;
                        break;
                    }

                    v_init = Value::number(initF);
                    v_limit = Value::number(limitF);
                    v_step = Value::number(stepF);
                    v_ext = v_init;
                }
                break;
            }

            case OpCode::OP_FORLOOP: {
                uint8_t base = readByte();
                uint16_t offset = readByte() | (readByte() << 8);
                size_t actualBase = currentCoroutine_->frames.empty() ? base : (currentFrame().stackBase + base);
                if (actualBase + 3 >= currentCoroutine_->stack.size()) {
                    runtimeError("Invalid stack for numeric for loop");
                    break;
                }
                Value& v_init = currentCoroutine_->stack[actualBase];
                Value& v_limit = currentCoroutine_->stack[actualBase + 1];
                Value& v_step = currentCoroutine_->stack[actualBase + 2];
                Value& v_ext = currentCoroutine_->stack[actualBase + 3];

                bool canContinue = false;
                if (v_init.isInteger() && v_step.isInteger()) {
                    int64_t initI = v_init.asInteger();
                    int64_t stepI = v_step.asInteger();
                    int64_t limitI = v_limit.asInteger();

                    int64_t nextI;
                    bool overflow = __builtin_add_overflow(initI, stepI, &nextI);
                    if (!overflow) {
                        canContinue = (stepI > 0) ? (nextI <= limitI) : (nextI >= limitI);
                    }
                    if (canContinue) {
                        v_init = makeInteger(nextI);
                        if (v_init.isObj()) writeBarrierBackward(currentCoroutine_, v_init.asObj());
                        v_ext = v_init;
                        currentFrame().ip -= offset;

                        // JIT Hotness tracking
                        if (currentFrame().closure) {
                            FunctionObject* func = currentFrame().closure->function();
                            if (isJitEnabled() && !func->getJITCode() && func->incrementHotness() >= 50) {
#ifdef USE_JIT
                                if (!jit()->compile(func)) {
                                    func->resetHotness(-1000);
                                }
#endif
                            }
                        }
                    }
                } else {
                    double initF = v_init.asNumber();
                    double stepF = v_step.asNumber();
                    double limitF = v_limit.asNumber();
                    double nextF = initF + stepF;
                    canContinue = (stepF > 0) ? (nextF <= limitF) : (nextF >= limitF);
                    if (canContinue) {
                        v_init = Value::number(nextF);
                        v_ext = v_init;
                        currentFrame().ip -= offset;

                        // JIT Hotness tracking
                        if (currentFrame().closure) {
                            FunctionObject* func = currentFrame().closure->function();
                            if (isJitEnabled() && !func->getJITCode() && func->incrementHotness() >= 50) {
#ifdef USE_JIT
                                if (!jit()->compile(func)) {
                                    func->resetHotness(-1000);
                                }
#endif
                            }
                        }
                    }
                }
                break;
            }

            case OpCode::OP_RETURN: {
                size_t stackBase = currentFrame().stackBase;
                currentFrame().ip -= 1;
                closeUpvalues(stackBase);

                if (currentCoroutine_->status == CoroutineObject::Status::SUSPENDED) {
                    return true;
                }
                currentFrame().ip += 1;

                if (currentCoroutine_->hookMask & CoroutineObject::MASK_RET) {
                    callHook("return");
                }

                {
                    // Pop all locals and arguments
                    while (currentCoroutine_->stack.size() > stackBase) {
                        pop();
                    }

                    // Pop closure if it exists
                    if (stackBase > 0) pop();

                    uint8_t expectedRetCount = currentFrame().retCount;
                    const Chunk* returnChunk = currentFrame().callerChunk;
                    bool frameIsPcall = currentFrame().isPcall;
                    currentCoroutine_->frames.pop_back();

                    // Check if we hit the target frame count (for pcall/load) or if this was the last frame
                    bool shouldExit = false;
                    if (targetFrameCount > 0) {
                        shouldExit = (currentCoroutine_->frames.size() <= targetFrameCount);
                    } else {
                        shouldExit = currentCoroutine_->frames.empty();
                    }

                    size_t toPush = 0;
                    if (expectedRetCount > 0) {
                        toPush = static_cast<size_t>(expectedRetCount - 1);
                    }

                    if (shouldExit) {
                        if (currentCoroutine_->frames.empty()) {
                            currentCoroutine_->status = CoroutineObject::Status::DEAD;
                            currentCoroutine_->chunk = nullptr;
                        } else {
                            currentCoroutine_->chunk = returnChunk;
                            if (frameIsPcall) {
                                push(Value::boolean(true));
                                if (expectedRetCount > 1) {
                                    for (size_t i = 0; i < expectedRetCount - 2; i++) push(Value::nil());
                                    currentCoroutine_->lastResultCount = expectedRetCount - 1;
                                } else {
                                    currentCoroutine_->lastResultCount = 1;
                                }
                            } else {
                                for (size_t i = 0; i < toPush; i++) {
                                    push(Value::nil());
                                }
                                currentCoroutine_->lastResultCount = toPush;
                            }
                        }
                        return !hadError_;
                    }

                    std::vector<Value> returnValues;
                    for (size_t i = 0; i < toPush; i++) {
                        returnValues.push_back(Value::nil());
                    }
                    if (frameIsPcall) {
                        returnValues.insert(returnValues.begin(), Value::boolean(true));
                    }

                    while (!currentCoroutine_->frames.empty() && currentFrame().isC) {
                        CallFrame cframe = currentFrame();
                        if (cframe.isErrorUnwinding) {
                            if (!currentCoroutine_->tbcVariables.empty() && currentCoroutine_->tbcVariables.back() >= cframe.stackBase) {
                                Value errObj = !currentCoroutine_->closeError.isNil() ? currentCoroutine_->closeError :
                                               (!lastErrorObject_.isNil() ? lastErrorObject_ : Value::runtimeString(internString(lastErrorMessage_)));
                                try {
                                    closeUpvalues(cframe.stackBase, nullptr, errObj);
                                } catch (const RuntimeError& e) {
                                    errObj = lastErrorObject_.isNil() ? Value::runtimeString(internString(e.what())) : lastErrorObject_;
                                    hadError_ = false;
                                    currentCoroutine_->closeError = errObj;
                                } catch (const std::exception& e) {
                                    errObj = Value::runtimeString(internString(e.what()));
                                    hadError_ = false;
                                    currentCoroutine_->closeError = errObj;
                                }
                                if (currentCoroutine_->status == CoroutineObject::Status::SUSPENDED) {
                                    return true;
                                }
                            }
                            Value errObj = !currentCoroutine_->closeError.isNil() ? currentCoroutine_->closeError :
                                           (!lastErrorObject_.isNil() ? lastErrorObject_ : Value::runtimeString(internString(lastErrorMessage_)));
                            currentCoroutine_->frames.pop_back();
                            currentCoroutine_->closeError = Value::nil();
                            lastErrorObject_ = Value::nil();
                            hadError_ = false;
                            isHandlingError_ = false;

                            while (currentCoroutine_->stack.size() > cframe.stackBase - 1) {
                                pop();
                            }

                            returnValues.clear();
                            returnValues.push_back(Value::boolean(false));
                            returnValues.push_back(errObj);

                            uint8_t cRetCount = cframe.retCount;
                            if (cRetCount > 0) {
                                size_t expected = static_cast<size_t>(cRetCount - 1);
                                if (returnValues.size() > expected) {
                                    returnValues.resize(expected);
                                } else {
                                    while (returnValues.size() < expected) {
                                        returnValues.push_back(Value::nil());
                                    }
                                }
                            }

                            if (targetFrameCount > 0 && currentCoroutine_->frames.size() <= targetFrameCount) {
                                shouldExit = true;
                            } else if (currentCoroutine_->frames.empty()) {
                                shouldExit = true;
                                currentCoroutine_->status = CoroutineObject::Status::DEAD;
                            }

                            if (shouldExit) {
                                currentCoroutine_->lastResultCount = returnValues.size();
                                for (const auto& value : returnValues) {
                                    push(value);
                                }
                                return true;
                            }
                            continue;
                        }

                        currentCoroutine_->frames.pop_back();

                        if (cframe.isPcall) {
                            returnValues.insert(returnValues.begin(), Value::boolean(true));
                        }

                        uint8_t cRetCount = cframe.retCount;
                        if (cRetCount > 0) {
                            size_t expected = static_cast<size_t>(cRetCount - 1);
                            if (returnValues.size() > expected) {
                                returnValues.resize(expected);
                            } else {
                                while (returnValues.size() < expected) {
                                    returnValues.push_back(Value::nil());
                                }
                            }
                        }

                        while (currentCoroutine_->stack.size() > cframe.stackBase - 1) {
                            pop();
                        }

                        if (targetFrameCount > 0 && currentCoroutine_->frames.size() <= targetFrameCount) {
                            shouldExit = true;
                        } else if (currentCoroutine_->frames.empty()) {
                            shouldExit = true;
                            currentCoroutine_->status = CoroutineObject::Status::DEAD;
                        }

                        if (shouldExit) {
                            currentCoroutine_->lastResultCount = returnValues.size();
                            for (const auto& value : returnValues) {
                                push(value);
                            }
                            return !hadError_;
                        }
                    }

                    if (!currentCoroutine_->frames.empty()) {
                        currentCoroutine_->chunk = currentFrame().chunk;
                    } else {
                        currentCoroutine_->chunk = returnChunk;
                    }

                    for (const auto& val : returnValues) {
                        push(val);
                    }
                    currentCoroutine_->lastResultCount = returnValues.size();
                    break;
                }
            }

            default:
                runtimeError("Unknown opcode");
                return false;
        }
        } catch (const RuntimeError& e) {
            if (targetFrameCount > 0) {
                throw;
            }
            int pcallIdx = -1;
            for (int i = (int)currentCoroutine_->frames.size() - 1; i >= 0; i--) {
                if (currentCoroutine_->frames[i].isPcall) {
                    pcallIdx = i;
                    break;
                }
            }
            if (pcallIdx >= 0) {
                Value errObj = lastErrorObject_.isNil() ? Value::runtimeString(internString(lastErrorMessage_)) : lastErrorObject_;
                currentCoroutine_->frames[pcallIdx].isErrorUnwinding = true;
                currentCoroutine_->closeError = errObj;

                while ((int)currentCoroutine_->frames.size() - 1 > pcallIdx) {
                    if (currentCoroutine_->frames.back().isReturning && !currentCoroutine_->pendingReturns.empty()) {
                        currentCoroutine_->pendingReturns.pop_back();
                    }
                    currentCoroutine_->frames.pop_back();
                }

                CallFrame& pframe = currentCoroutine_->frames[pcallIdx];
                size_t base = pframe.stackBase;
                try {
                    closeUpvalues(base, nullptr, errObj);
                } catch (const RuntimeError& e) {
                    errObj = lastErrorObject_.isNil() ? Value::runtimeString(internString(e.what())) : lastErrorObject_;
                    hadError_ = false;
                    currentCoroutine_->closeError = errObj;
                } catch (const std::exception& e) {
                    errObj = Value::runtimeString(internString(e.what()));
                    hadError_ = false;
                    currentCoroutine_->closeError = errObj;
                }

                if (currentCoroutine_->status == CoroutineObject::Status::SUSPENDED) {
                    return true;
                }

                Value finalErr = !currentCoroutine_->closeError.isNil() ? currentCoroutine_->closeError :
                                 (!lastErrorObject_.isNil() ? lastErrorObject_ : errObj);
                currentCoroutine_->closeError = Value::nil();
                lastErrorObject_ = Value::nil();
                hadError_ = false;
                isHandlingError_ = false;

                while (currentCoroutine_->stack.size() > pframe.stackBase - 1) {
                    pop();
                }

                const Chunk* retChunk = pframe.callerChunk;
                uint8_t cRetCount = pframe.retCount;
                currentCoroutine_->frames.pop_back();

                std::vector<Value> returnValues = { Value::boolean(false), finalErr };
                if (cRetCount > 0) {
                    size_t expected = static_cast<size_t>(cRetCount - 1);
                    if (returnValues.size() > expected) {
                        returnValues.resize(expected);
                    } else {
                        while (returnValues.size() < expected) {
                            returnValues.push_back(Value::nil());
                        }
                    }
                }

                bool shouldExit = false;
                while (!currentCoroutine_->frames.empty() && currentFrame().isC) {
                    CallFrame cframe = currentFrame();
                    currentCoroutine_->frames.pop_back();

                    if (cframe.isPcall) {
                        returnValues.insert(returnValues.begin(), Value::boolean(true));
                    }

                    uint8_t cRetCount2 = cframe.retCount;
                    if (cRetCount2 > 0) {
                        size_t expected = static_cast<size_t>(cRetCount2 - 1);
                        if (returnValues.size() > expected) {
                            returnValues.resize(expected);
                        } else {
                            while (returnValues.size() < expected) {
                                returnValues.push_back(Value::nil());
                            }
                        }
                    }

                    while (currentCoroutine_->stack.size() > cframe.stackBase - 1) {
                        pop();
                    }

                    if (targetFrameCount > 0 && currentCoroutine_->frames.size() <= targetFrameCount) {
                        shouldExit = true;
                    } else if (currentCoroutine_->frames.empty()) {
                        shouldExit = true;
                        currentCoroutine_->status = CoroutineObject::Status::DEAD;
                    }

                    if (shouldExit) {
                        currentCoroutine_->lastResultCount = returnValues.size();
                        for (const auto& value : returnValues) {
                            push(value);
                        }
                        return !hadError_;
                    }
                }

                if (targetFrameCount > 0 && currentCoroutine_->frames.size() <= targetFrameCount) {
                    shouldExit = true;
                } else if (currentCoroutine_->frames.empty()) {
                    shouldExit = true;
                    currentCoroutine_->status = CoroutineObject::Status::DEAD;
                }

                if (shouldExit) {
                    currentCoroutine_->lastResultCount = returnValues.size();
                    for (const auto& val : returnValues) {
                        push(val);
                    }
                    return true;
                }

                if (!currentCoroutine_->frames.empty()) {
                    currentCoroutine_->chunk = currentFrame().chunk;
                } else {
                    currentCoroutine_->chunk = retChunk;
                }

                for (const auto& val : returnValues) {
                    push(val);
                }
                currentCoroutine_->lastResultCount = returnValues.size();
                continue;
            }
            throw;
        }

        if (hadError_) {
            return false;
        }
    }
}
