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

bool VM::run(size_t targetFrameCount) {
    // Main execution loop
    while (true) {
        if (currentCoroutine_->frames.empty()) {
            return !hadError_;
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
            if (jitCode) {
                // TODO: Execute JIT code once templates are implemented
                // For now, continue interpreting
            }
        }
#endif
        
        uint8_t instruction = readByte();
        OpCode op = static_cast<OpCode>(instruction);

        switch (op) {
            case OpCode::OP_CONSTANT: {
                Value constant = readConstant();
                push(constant);
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

            case OpCode::OP_GET_TABUP: {
                uint8_t upIndex = readByte();
                Value key = readConstant();

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

                if (upTable.isTable()) {
                    push(upTable.asTableObj()->get(key));
                } else {
                    runtimeError("attempt to index a " + upTable.typeToString() + " value");
                }
                break;
            }
            case OpCode::OP_SET_TABUP: {
                uint8_t upIndex = readByte();
                Value key = readConstant();
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

                if (upTable.isTable()) {
                    upTable.asTableObj()->set(key, value);
                } else {
                    runtimeError("attempt to index a " + upTable.typeToString() + " value");
                }
                
                pop(); // Pop after setting
                break;
            }
            case OpCode::OP_CLOSE_UPVALUE: {
                // Close upvalue at top of stack
                closeUpvalues(currentCoroutine_->stack.size() - 1);
                pop();
                break;
            }

            case OpCode::OP_ADD: {
                Value b = pop();
                Value a = pop();
                if (a.isNumber() && b.isNumber()) {
                    push(add(a, b));
                } else if (!callBinaryMetamethod(a, b, "__add")) {
                    runtimeError("attempt to perform arithmetic on " + a.typeToString() + " and " + b.typeToString());
                }
                break;
            }

            case OpCode::OP_SUB: {
                Value b = pop();
                Value a = pop();
                if (a.isNumber() && b.isNumber()) {
                    push(subtract(a, b));
                } else if (!callBinaryMetamethod(a, b, "__sub")) {
                    runtimeError("attempt to perform arithmetic on " + a.typeToString() + " and " + b.typeToString());
                }
                break;
            }

            case OpCode::OP_MUL: {
                Value b = pop();
                Value a = pop();
                if (a.isNumber() && b.isNumber()) {
                    push(multiply(a, b));
                } else if (!callBinaryMetamethod(a, b, "__mul")) {
                    runtimeError("attempt to perform arithmetic on " + a.typeToString() + " and " + b.typeToString());
                }
                break;
            }

            case OpCode::OP_DIV: {
                Value b = pop();
                Value a = pop();
                if (a.isNumber() && b.isNumber()) {
                    push(divide(a, b));
                } else if (!callBinaryMetamethod(a, b, "__div")) {
                    runtimeError("attempt to perform arithmetic on " + a.typeToString() + " and " + b.typeToString());
                }
                break;
            }

            case OpCode::OP_IDIV: {
                Value b = pop();
                Value a = pop();
                if (a.isNumber() && b.isNumber()) {
                    push(integerDivide(a, b));
                } else if (!callBinaryMetamethod(a, b, "__idiv")) {
                    runtimeError("attempt to perform arithmetic on " + a.typeToString() + " and " + b.typeToString());
                }
                break;
            }

            case OpCode::OP_MOD: {
                Value b = pop();
                Value a = pop();
                if (a.isNumber() && b.isNumber()) {
                    push(modulo(a, b));
                } else if (!callBinaryMetamethod(a, b, "__mod")) {
                    runtimeError("attempt to perform arithmetic on " + a.typeToString() + " and " + b.typeToString());
                }
                break;
            }

            case OpCode::OP_POW: {
                Value b = pop();
                Value a = pop();
                if (a.isNumber() && b.isNumber()) {
                    push(power(a, b));
                } else if (!callBinaryMetamethod(a, b, "__pow")) {
                    runtimeError("attempt to perform arithmetic on " + a.typeToString() + " and " + b.typeToString());
                }
                break;
            }

            case OpCode::OP_BAND: {
                Value b = pop();
                Value a = pop();
                if (a.isNumber() && b.isNumber()) {
                    push(bitwiseAnd(a, b));
                } else if (!callBinaryMetamethod(a, b, "__band")) {
                    runtimeError("attempt to perform bitwise operation on " + a.typeToString() + " and " + b.typeToString());
                }
                break;
            }

            case OpCode::OP_BOR: {
                Value b = pop();
                Value a = pop();
                if (a.isNumber() && b.isNumber()) {
                    push(bitwiseOr(a, b));
                } else if (!callBinaryMetamethod(a, b, "__bor")) {
                    runtimeError("attempt to perform bitwise operation on " + a.typeToString() + " and " + b.typeToString());
                }
                break;
            }

            case OpCode::OP_BXOR: {
                Value b = pop();
                Value a = pop();
                if (a.isNumber() && b.isNumber()) {
                    push(bitwiseXor(a, b));
                } else if (!callBinaryMetamethod(a, b, "__bxor")) {
                    runtimeError("attempt to perform bitwise operation on " + a.typeToString() + " and " + b.typeToString());
                }
                break;
            }

            case OpCode::OP_SHL: {
                Value b = pop();
                Value a = pop();
                if (a.isNumber() && b.isNumber()) {
                    push(shiftLeft(a, b));
                } else if (!callBinaryMetamethod(a, b, "__shl")) {
                    runtimeError("attempt to perform bitwise operation on " + a.typeToString() + " and " + b.typeToString());
                }
                break;
            }

            case OpCode::OP_SHR: {
                Value b = pop();
                Value a = pop();
                if (a.isNumber() && b.isNumber()) {
                    push(shiftRight(a, b));
                } else if (!callBinaryMetamethod(a, b, "__shr")) {
                    runtimeError("attempt to perform bitwise operation on " + a.typeToString() + " and " + b.typeToString());
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
                if (a.isNumber()) {
                    push(Value::number(-a.asNumber()));
                } else {
                    Value func = getMetamethod(a, "__unm");
                    if (!func.isNil()) {
                        push(func);
                        push(a);
                        callValue(1, 2); // Expect 1 result (1+1=2)
                    } else {
                        runtimeError("attempt to perform arithmetic on " + a.typeToString());
                    }
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
                if (a.isNumber()) {
                    push(bitwiseNot(a));
                } else if (!callBinaryMetamethod(a, a, "__bnot")) { // Unary bitwise NOT
                    runtimeError("attempt to perform bitwise operation on " + a.typeToString());
                }
                break;
            }

            case OpCode::OP_LEN: {
                Value a = pop();
                if (a.isString()) {
                    push(Value::number(static_cast<double>(getStringValue(a).length())));
                } else if (a.isTable()) {
                    Value mm = getMetamethod(a, "__len");
                    if (!mm.isNil()) {
                        push(mm);
                        push(a);
                        callValue(1, 2); // Expect 1 result (1+1=2)
                    } else {
                        push(Value::number(static_cast<double>(a.asTableObj()->length())));
                    }
                } else {
                    Value mm = getMetamethod(a, "__len");
                    if (!mm.isNil()) {
                        push(mm);
                        push(a);
                        callValue(1, 2);
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
                    push(Value::boolean(a.asNumber() < b.asNumber()));
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
                    push(Value::boolean(a.asNumber() <= b.asNumber()));
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
                    push(Value::boolean(a.asNumber() > b.asNumber()));
                } else if ((a.isString() || a.isRuntimeString()) && (b.isString() || b.isRuntimeString())) {
                    push(Value::boolean(getStringValue(a) > getStringValue(b)));
                } else if (!callBinaryMetamethod(b, a, "__lt")) {
                    runtimeError("attempt to compare " + a.typeToString() + " and " + b.typeToString());
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
                } else if (!callBinaryMetamethod(b, a, "__le")) {
                    runtimeError("attempt to compare " + a.typeToString() + " and " + b.typeToString());
                }
                break;
            }

            case OpCode::OP_PRINT: {
                Value value = pop();
                // Special handling for strings
                if (value.isString()) {
                    size_t index = value.asStringIndex();
                    StringObject* str = nullptr;

                    // Check if it's a runtime string or compile-time string
                    if (value.isRuntimeString()) {
                        // Runtime string from VM pool
                        str = getString(index);
                    } else {
                        // Use current chunk if possible, otherwise fall back to root
                        str = currentFrame().chunk ? currentFrame().chunk->getString(index) : currentCoroutine_->rootChunk->getString(index);
                    }

                    if (str) {
                        std::cout << str->chars() << std::endl;
                    } else {
                        std::cout << "<invalid string>" << std::endl;
                    }
                } else {
                    std::cout << value << std::endl;
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
                    if (!func->getJITCode() && func->incrementHotness() >= 50) {
#ifdef USE_JIT
                        jit()->compile(func);
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

            case OpCode::OP_CALL: {
                uint8_t argCount = readByte();
                uint8_t retCount = readByte();  // Number of return values to keep (0 = all)
                
                // JIT Hotness tracking
                Value callee = peek(argCount);
                if (callee.isClosure()) {
                    FunctionObject* func = callee.asClosureObj()->function();
                    if (!func->getJITCode() && func->incrementHotness() >= 500) {
#ifdef USE_JIT
                        // printf("DEBUG: Function %s is HOT (call), compiling...\n", func->name().c_str());
                        jit()->compile(func);
#endif
                    }
                }

                size_t prevFrames = currentCoroutine_->frames.size();
                if (!callValue(argCount, retCount)) {
                    return false;
                }
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
                    if (!func->getJITCode() && func->incrementHotness() >= 500) {
#ifdef USE_JIT
                        // printf("DEBUG: Function %s is HOT (call_multi), compiling...\n", func->name().c_str());
                        jit()->compile(func);
#endif
                    }
                }

                size_t prevFrames = currentCoroutine_->frames.size();
                if (!callValue(actualArgCount, retCount)) {
                    return false;
                }
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

            case OpCode::OP_RETURN_VALUE: {
                // Handle debug hook before cleanup
                if (currentCoroutine_->hookMask & CoroutineObject::MASK_RET) {
                    callHook("return");
                }

                // Read the count of return values
                uint8_t count = readByte();
                size_t actualCount = count;
                if (count == 0) {
                    actualCount = currentCoroutine_->lastResultCount;
                }

                // Pop all return values from the stack (in reverse order)
                std::vector<Value> returnValues;
                returnValues.reserve(actualCount);
                for (size_t i = 0; i < actualCount; i++) {
                    returnValues.push_back(pop());
                }
                // Reverse so they're in correct order
                std::reverse(returnValues.begin(), returnValues.end());

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

                // Close upvalues for locals that are going out of scope
                size_t stackBase = currentFrame().stackBase;
                closeUpvalues(stackBase);

                // Pop all locals and arguments (down to stackBase)
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
                    }
                    
                    // Push all return values before returning
                    currentCoroutine_->lastResultCount = returnValues.size();
                    for (const auto& value : returnValues) {
                        push(value);
                    }
                    return !hadError_;
                }

                // Restore execution state
                currentCoroutine_->chunk = returnChunk;
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

                if (tableValue.isTable()) {
                    TableObject* table = tableValue.asTableObj();
                    Value value = table->get(key);
                    if (!value.isNil()) {
                        push(value);
                        break;
                    }
                }

                // Not found in table or not a table, check metatable for the property itself
                // (e.g. string methods are in the string metatable or its __index)
                if (key.isString()) {
                    Value mm = getMetamethod(tableValue, getStringValue(key));
                    if (!mm.isNil()) {
                        push(mm);
                        break;
                    }
                }

                // If not found as property, check standard __index metamethod
                Value indexMethod = getMetamethod(tableValue, "__index");
                if (indexMethod.isNil()) {
                    if (!tableValue.isTable()) {
                        runtimeError("attempt to index a " + tableValue.typeToString() + " value");
                    }
                    push(Value::nil());
                } else if (indexMethod.isFunction()) {
                    push(indexMethod);
                    push(tableValue);
                    push(key);
                    callValue(2, 2); // Expect 1 result (1 + 1 = 2)
                } else if (indexMethod.isTable()) {
                    // Recurse into the __index table
                    TableObject* indexTable = indexMethod.asTableObj();
                    Value result = key.isString() ? indexTable->get(getStringValue(key)) : indexTable->get(key);
                    push(result);
                } else {
                    push(Value::nil());
                }
                break;
            }

            case OpCode::OP_SET_TABLE: {
                Value value = peek(0);
                Value key = peek(1);
                Value tableValue = peek(2);

                if (tableValue.isTable()) {
                    TableObject* table = tableValue.asTableObj();
                    if (table->has(key)) {
                        table->set(key, value);
                        pop(); pop(); pop();
                        break;
                    }
                }

                Value newIndex = getMetamethod(tableValue, "__newindex");
                if (newIndex.isNil()) {
                    if (tableValue.isTable()) {
                        TableObject* table = tableValue.asTableObj();
                        table->set(key, value);
                    } else {
                        runtimeError("attempt to index a " + tableValue.typeToString() + " value");
                    }
                    pop(); pop(); pop();
                } else if (newIndex.isFunction()) {
                    // To call mm(table, key, value), we need to insert mm below the 3 arguments
                    // Stack currently has: [..., tableValue, key, value]
                    // We need it to be: [..., mm, tableValue, key, value]
                    currentCoroutine_->stack.insert(currentCoroutine_->stack.end() - 3, newIndex);
                    callValue(3, 1); // Expect 0 results (0 + 1 = 1)
                } else if (newIndex.isTable()) {
                    TableObject* niTable = newIndex.asTableObj();
                    if (key.isString()) {
                        niTable->set(getStringValue(key), value);
                    } else {
                        niTable->set(key, value);
                    }
                    pop(); pop(); pop();
                } else {
                    if (tableValue.isTable()) {
                        TableObject* table = tableValue.asTableObj();
                        table->set(key, value);
                    } else {
                        runtimeError("attempt to index a " + tableValue.typeToString() + " value");
                    }
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

            case OpCode::OP_YIELD: {
                uint8_t count = readByte();
                uint8_t retCount = readByte();
                
                // Pop yielded values and save them
                currentCoroutine_->yieldedValues.clear();
                for (int i = 0; i < count; i++) {
                    currentCoroutine_->yieldedValues.push_back(pop());
                }
                // Reverse so they are in original order
                std::reverse(currentCoroutine_->yieldedValues.begin(), currentCoroutine_->yieldedValues.end());

                currentCoroutine_->status = CoroutineObject::Status::SUSPENDED;
                currentCoroutine_->yieldCount = count;
                currentCoroutine_->retCount = retCount;
                return true; // Return to resumer
            }

            case OpCode::OP_RETURN:
                if (currentCoroutine_->hookMask & CoroutineObject::MASK_RET) {
                    callHook("return");
                }

                {
                    size_t stackBase = currentFrame().stackBase;
                    closeUpvalues(stackBase);

                    // Pop all locals and arguments
                    while (currentCoroutine_->stack.size() > stackBase) {
                        pop();
                    }

                    // Pop closure if it exists
                    if (stackBase > 0) pop();

                    const Chunk* returnChunk = currentFrame().callerChunk;
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
                        } else {
                            // If returning to a caller, push nil as result
                            push(Value::nil());
                            currentCoroutine_->lastResultCount = 1;
                        }
                        return !hadError_;
                    }

                    currentCoroutine_->chunk = returnChunk;
                    push(Value::nil());
                    currentCoroutine_->lastResultCount = 1;
                }
                break;

            default:
                runtimeError("Unknown opcode");
                return false;
        }

        if (hadError_) {
            return false;
        }
    }
}
