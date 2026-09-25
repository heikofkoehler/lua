#include "vm/vm.hpp"
#include "value/coroutine.hpp"
#include "value/closure.hpp"
#include <iostream>
#include <vector>
#include <algorithm>

namespace {

bool native_coroutine_create(VM* vm, int argCount) {
    if (argCount != 1) {
        vm->runtimeError("bad argument #1 to 'create' (value expected)");
        return false;
    }
    Value funcVal = vm->pop();
    if (!funcVal.isClosure() && !funcVal.isNativeFunction() && !funcVal.isCFunction()) {
        vm->runtimeError("bad argument #1 to 'create' (function expected, got " + funcVal.typeToString() + ")");
        return false;
    }

    CoroutineObject* co = vm->createCoroutine(funcVal);
    vm->push(Value::thread(co));
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_coroutine_resume(VM* vm, int argCount) {
    if (argCount < 1) {
        vm->runtimeError("coroutine.resume expects at least 1 argument");
        return false;
    }

    // Extract arguments
    std::vector<Value> args;
    for (int i = 0; i < argCount - 1; i++) {
        args.push_back(vm->pop());
    }
    
    Value coVal = vm->pop();
    if (!coVal.isThread()) {
        vm->runtimeError("coroutine.resume expects a thread as first argument");
        return false;
    }

    CoroutineObject* co = coVal.asThreadObj();
    if (co->status == CoroutineObject::Status::DEAD) {
        vm->push(Value::boolean(false));
        StringObject* errStr = vm->internString("cannot resume dead coroutine");
        vm->push(Value::runtimeString(errStr));
        vm->currentCoroutine()->lastResultCount = 2;
        return true;
    }
    if (co->status != CoroutineObject::Status::SUSPENDED) {
        vm->push(Value::boolean(false));
        StringObject* errStr = vm->internString("cannot resume non-suspended coroutine");
        vm->push(Value::runtimeString(errStr));
        vm->currentCoroutine()->lastResultCount = 2;
        return true;
    }

    // Push arguments to coroutine stack
    size_t pushedCount = args.size();
    for (auto it = args.rbegin(); it != args.rend(); ++it) {
        co->stack.push_back(*it);
    }

    // If this is the first resume, we might need to adjust CallFrame varargs
    if (!co->frames.empty() && co->frames[0].ip == 0 && co->frames.size() == 1 &&
        co->frames[0].closure && !co->frames[0].closure->isC()) {
        FunctionObject* func = co->frames[0].closure->function();
        int arity = func->arity();
        bool hasVarargs = func->hasVarargs();
        int aCount = static_cast<int>(args.size());

        if (aCount < arity) {
            for (int i = 0; i < arity - aCount; i++) {
                co->stack.push_back(Value::nil());
            }
        } else if (aCount > arity) {
            uint8_t extraCount = aCount - arity;
            if (hasVarargs) {
                for (int i = 0; i < extraCount; i++) {
                    co->frames[0].varargs.push_back(co->stack.back());
                    co->stack.pop_back();
                }
                std::reverse(co->frames[0].varargs.begin(), co->frames[0].varargs.end());
            } else {
                for (int i = 0; i < extraCount; i++) {
                    co->stack.pop_back();
                }
            }
        }
    } else {
        // Not first resume - we are resuming from a yield.
        uint8_t expectedRetCount = co->retCount;
        if (expectedRetCount > 0) {
            size_t expected = static_cast<size_t>(expectedRetCount - 1);
            if (pushedCount > expected) {
                co->stack.resize(co->stack.size() - (pushedCount - expected));
                pushedCount = expected;
            } else if (pushedCount < expected) {
                for (size_t i = 0; i < expected - pushedCount; i++) {
                    co->stack.push_back(Value::nil());
                }
                pushedCount = expected;
            }
        }
        co->lastResultCount = pushedCount;
    }

    // Switch coroutines
    CoroutineObject* caller = vm->currentCoroutine();
    co->caller = caller;
    co->status = CoroutineObject::Status::RUNNING;
    if (caller) caller->status = CoroutineObject::Status::NORMAL;

    bool success = vm->resumeCoroutine(co);

    if (caller) caller->status = CoroutineObject::Status::RUNNING;

    if (!success) {
        vm->push(Value::boolean(false));
        Value errObj = !vm->lastErrorObject().isNil() ? vm->lastErrorObject() : Value::runtimeString(vm->internString(vm->lastErrorMessage()));
        vm->push(errObj);
        vm->setLastErrorObject(Value::nil());
        vm->currentCoroutine()->lastResultCount = 2;
        return true;
    }

    // Success results are already pushed by VM::resumeCoroutine
    size_t count = vm->currentCoroutine()->lastResultCount;
    
    // We need to shift them to push 'true' at the beginning
    std::vector<Value> results;
    for(size_t i=0; i<count; i++) results.push_back(vm->pop());
    std::reverse(results.begin(), results.end());

    vm->push(Value::boolean(true));
    for(const auto& v : results) vm->push(v);
    vm->currentCoroutine()->lastResultCount = count + 1;
    
    if (co->status == CoroutineObject::Status::DEAD) {
        co->stack.clear();
    } else {
        co->yieldedValues.clear();
    }
    return true;
}

bool native_coroutine_status(VM* vm, int argCount) {
    if (argCount != 1) {
        vm->runtimeError("coroutine.status expects 1 argument");
        return false;
    }
    Value coVal = vm->pop();
    if (!coVal.isThread()) {
        vm->runtimeError("coroutine.status expects a thread");
        return false;
    }

    CoroutineObject* co = coVal.asThreadObj();
    StringObject* str = vm->internString(co->statusToString());
    vm->push(Value::runtimeString(str));
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_coroutine_running(VM* vm, int argCount) {
    for(int i=0; i<argCount; i++) vm->pop();
    CoroutineObject* co = vm->currentCoroutine();
    vm->push(Value::thread(co));
    vm->push(Value::boolean(co == vm->mainCoroutine()));
    vm->currentCoroutine()->lastResultCount = 2;
    return true;
}

bool native_coroutine_yield(VM* vm, int argCount) {
    CoroutineObject* co = vm->currentCoroutine();
    if (!co->caller || co == vm->mainCoroutine()) {
        vm->runtimeError("attempt to yield from outside a coroutine");
        return false;
    }
    if (co->nonYieldableCount > 0 || co->isClosing) {
        vm->runtimeError("attempt to yield across a C-call boundary");
        return false;
    }

    co->yieldedValues.clear();
    for (int i = 0; i < argCount; i++) {
        co->yieldedValues.push_back(vm->pop());
    }
    std::reverse(co->yieldedValues.begin(), co->yieldedValues.end());

    co->status = CoroutineObject::Status::SUSPENDED;
    co->yieldCount = argCount;
    co->retCount = !co->frames.empty() ? co->frames.back().retCount : 0; 
    vm->currentCoroutine()->lastResultCount = 0;

    return true;
}

bool native_coroutine_wrap(VM* vm, int argCount) {
    if (argCount != 1) {
        vm->runtimeError("coroutine.wrap expects 1 argument");
        return false;
    }
    
    Value func = vm->pop();
    if (!func.isClosure() && !func.isNativeFunction() && !func.isCFunction()) {
        vm->runtimeError("bad argument #1 to 'wrap' (function expected, got " + func.typeToString() + ")");
        return false;
    }
    
    // Create the coroutine
    vm->push(func);
    if (!native_coroutine_create(vm, 1)) return false;
    Value co = vm->pop();

    // Create a closure that captures 'co' and calls resume
    std::string wrapScript = 
        "local co = ...\n"
        "return function(...)\n"
        "    local res = table.pack(coroutine.resume(co, ...))\n"
        "    if not res[1] then error(res[2], 0) end\n"
        "    return table.unpack(res, 2, res.n)\n"
        "end\n";
        
    FunctionObject* wrapperFunc = vm->compileSource(wrapScript, "coroutine.wrap");
    if (!wrapperFunc) return false;

    // Create closure for the wrapper
    ClosureObject* wrapperClosure = vm->createClosure(wrapperFunc);
    vm->setupRootUpvalues(wrapperClosure);
    
    // Call it with 'co' as argument to get the actual returned function
    vm->push(Value::closure(wrapperClosure));
    vm->push(co);
    
    // Use targetFrameCount to return from callValue after the wrapper is created
    size_t baseFrames = vm->currentCoroutine()->frames.size();
    if (!vm->callValue(1, 2)) return false;
    
    if (vm->currentCoroutine()->frames.size() > baseFrames) {
        if (!vm->run(baseFrames)) return false;
    }
    
    // The result (the actual function returned by wrapScript) is now on stack. 
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_coroutine_close(VM* vm, int argCount) {
    CoroutineObject* co = nullptr;
    if (argCount == 0) {
        co = vm->currentCoroutine();
    } else {
        Value coVal = vm->peek(argCount - 1);
        if (!coVal.isThread()) {
            vm->runtimeError("bad argument #1 to 'close' (thread expected, got " + coVal.typeToString() + ")");
            return false;
        }
        co = coVal.asThreadObj();
    }
    for (int i = 0; i < argCount; i++) vm->pop();

    if (co->isClosing) {
        vm->push(Value::boolean(true));
        vm->currentCoroutine()->lastResultCount = 1;
        return true;
    }

    if (co->status == CoroutineObject::Status::NORMAL) {
        vm->runtimeError("cannot close a normal coroutine");
        return false;
    }

    if (co->status == CoroutineObject::Status::RUNNING) {
        if (co == vm->mainCoroutine()) {
            vm->runtimeError("cannot close main thread");
            return false;
        }
        // Coroutine closing itself
        co->isClosing = true;
        co->nonYieldableCount++;
        Value closeErr = Value::nil();
        try {
            vm->closeUpvalues(0, co);
            if (!co->closeError.isNil()) {
                closeErr = co->closeError;
                co->closeError = Value::nil();
            }
        } catch (const RuntimeError& e) {
            closeErr = !vm->lastErrorObject().isNil() ? vm->lastErrorObject() : Value::runtimeString(vm->internString(e.what()));
            vm->setLastErrorObject(Value::nil());
        } catch (const std::exception& e) {
            closeErr = Value::runtimeString(vm->internString(e.what()));
        }
        co->nonYieldableCount--;
        co->isClosing = false;
        co->status = CoroutineObject::Status::DEAD;
        throw CoroutineCloseSelfException{closeErr};
    }

    if (co->status == CoroutineObject::Status::DEAD) {
        if (!co->closeError.isNil()) {
            Value err = co->closeError;
            co->closeError = Value::nil();
            vm->push(Value::boolean(false));
            vm->push(err);
            vm->currentCoroutine()->lastResultCount = 2;
            return true;
        }
        vm->push(Value::boolean(true));
        vm->currentCoroutine()->lastResultCount = 1;
        return true;
    }

    // co is SUSPENDED:
    co->isClosing = true;
    co->nonYieldableCount++;
    Value closeErr = Value::nil();
    try {
        vm->closeCoroutine(co);
        if (!co->closeError.isNil()) {
            closeErr = co->closeError;
            co->closeError = Value::nil();
        }
    } catch (const RuntimeError& e) {
        closeErr = !vm->lastErrorObject().isNil() ? vm->lastErrorObject() : Value::runtimeString(vm->internString(e.what()));
        vm->setLastErrorObject(Value::nil());
    } catch (const std::exception& e) {
        closeErr = Value::runtimeString(vm->internString(e.what()));
    }
    co->nonYieldableCount--;
    co->isClosing = false;
    co->status = CoroutineObject::Status::DEAD;

    if (!closeErr.isNil()) {
        vm->push(Value::boolean(false));
        vm->push(closeErr);
        vm->currentCoroutine()->lastResultCount = 2;
        return true;
    }

    vm->push(Value::boolean(true));
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_coroutine_isyieldable(VM* vm, int argCount) {
    CoroutineObject* co = vm->currentCoroutine();
    if (argCount >= 1) {
        Value val = vm->peek(argCount - 1);
        if (val.isThread()) {
            co = val.asThreadObj();
        }
    }
    
    bool yieldable = (co != vm->mainCoroutine() && co->nonYieldableCount == 0 && !co->isClosing);
    
    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(Value::boolean(yieldable));
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

} // anonymous namespace

void registerCoroutineLibrary(VM* vm, TableObject* coroutineTable) {
    vm->addNativeToTable(coroutineTable, "create", native_coroutine_create);
    vm->addNativeToTable(coroutineTable, "resume", native_coroutine_resume);
    vm->addNativeToTable(coroutineTable, "status", native_coroutine_status);
    vm->addNativeToTable(coroutineTable, "running", native_coroutine_running);
    vm->addNativeToTable(coroutineTable, "yield", native_coroutine_yield);
    vm->addNativeToTable(coroutineTable, "wrap", native_coroutine_wrap);
    vm->addNativeToTable(coroutineTable, "isyieldable", native_coroutine_isyieldable);
    vm->addNativeToTable(coroutineTable, "close", native_coroutine_close);
}
