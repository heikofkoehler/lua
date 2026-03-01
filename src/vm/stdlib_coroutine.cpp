#include "vm/vm.hpp"
#include "value/coroutine.hpp"
#include "value/closure.hpp"
#include <iostream>

namespace {

bool native_coroutine_create(VM* vm, int argCount) {
    if (argCount != 1) {
        vm->runtimeError("coroutine.create expects 1 argument");
        return false;
    }
    Value funcVal = vm->pop();
    if (!funcVal.isClosure()) {
        vm->runtimeError("coroutine.create expects a closure");
        return false;
    }

    ClosureObject* closure = funcVal.asClosureObj();
    CoroutineObject* co = vm->createCoroutine(closure);
    vm->push(Value::thread(co));
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
        return true;
    }

    // Push arguments to coroutine stack
    size_t pushedCount = args.size();
    for (auto it = args.rbegin(); it != args.rend(); ++it) {
        co->stack.push_back(*it);
    }

    // If this is the first resume, we might need to adjust CallFrame varargs
    if (!co->frames.empty() && co->frames[0].ip == 0 && co->frames.size() == 1) {
        FunctionObject* func = co->frames[0].closure->function();
        int arity = func->arity();
        bool hasVarargs = func->hasVarargs();
        int argCount = static_cast<int>(args.size());

        if (argCount < arity) {
            for (int i = 0; i < arity - argCount; i++) {
                co->stack.push_back(Value::nil());
            }
        } else if (argCount > arity) {
            uint8_t extraCount = argCount - arity;
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
            } else if (pushedCount < expected) {
                for (size_t i = 0; i < expected - pushedCount; i++) {
                    co->stack.push_back(Value::nil());
                }
            }
        }
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
        vm->push(Value::runtimeString(vm->internString("error in coroutine")));
        return true;
    }

    vm->push(Value::boolean(true));
    
    if (co->status == CoroutineObject::Status::SUSPENDED) {
        size_t count = co->yieldedValues.size();
        for (const auto& val : co->yieldedValues) {
            vm->push(val);
        }
        vm->currentCoroutine()->lastResultCount = count + 1;
        co->yieldedValues.clear();
    } else {
        size_t count = co->stack.size();
        for (const auto& val : co->stack) {
            vm->push(val);
        }
        vm->currentCoroutine()->lastResultCount = count + 1;
        co->stack.clear();
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
    return true;
}

bool native_coroutine_running(VM* vm, int /*argCount*/) {
    vm->push(Value::thread(vm->currentCoroutine()));
    return true;
}

bool native_coroutine_yield(VM* vm, int argCount) {
    CoroutineObject* co = vm->currentCoroutine();
    if (!co->caller) {
        vm->runtimeError("attempt to yield from outside a coroutine");
        return false;
    }

    co->yieldedValues.clear();
    for (int i = 0; i < argCount; i++) {
        co->yieldedValues.push_back(vm->pop());
    }
    std::reverse(co->yieldedValues.begin(), co->yieldedValues.end());

    co->status = CoroutineObject::Status::SUSPENDED;
    co->yieldCount = argCount;
    co->retCount = 0; 

    return true;
}

bool native_coroutine_wrap(VM* vm, int argCount) {
    if (argCount != 1) {
        vm->runtimeError("coroutine.wrap expects 1 argument");
        return false;
    }
    
    Value func = vm->pop();
    if (!func.isClosure() && !func.isNativeFunction()) {
        vm->runtimeError("coroutine.wrap expects a function");
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
        "    local res = {coroutine.resume(co, ...)}\n"
        "    if not res[1] then error(res[2]) end\n"
        "    return table.unpack(res, 2)\n"
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

} // anonymous namespace

void registerCoroutineLibrary(VM* vm, TableObject* coroutineTable) {
    vm->addNativeToTable(coroutineTable, "create", native_coroutine_create);
    vm->addNativeToTable(coroutineTable, "resume", native_coroutine_resume);
    vm->addNativeToTable(coroutineTable, "status", native_coroutine_status);
    vm->addNativeToTable(coroutineTable, "running", native_coroutine_running);
    vm->addNativeToTable(coroutineTable, "yield", native_coroutine_yield);
    vm->addNativeToTable(coroutineTable, "wrap", native_coroutine_wrap);
}
