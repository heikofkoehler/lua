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

    ClosureObject* closure = vm->getClosure(funcVal.asClosureIndex());
    size_t coIdx = vm->createCoroutine(closure);
    vm->push(Value::thread(coIdx));
    return true;
}

bool native_coroutine_resume(VM* vm, int argCount) {
    if (argCount < 1) {
        vm->runtimeError("coroutine.resume expects at least 1 argument");
        return false;
    }

    // Arguments are on stack: [..., co, arg1, arg2, ...]
    // argCount includes 'co'
    
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

    CoroutineObject* co = vm->getCoroutine(coVal.asThreadIndex());
    if (co->status == CoroutineObject::Status::DEAD) {
        vm->push(Value::boolean(false));
        size_t errIdx = vm->internString("cannot resume dead coroutine");
        vm->push(Value::runtimeString(errIdx));
        return true;
    }

    // Push arguments to coroutine stack
    size_t pushedCount = args.size();
    for (auto it = args.rbegin(); it != args.rend(); ++it) {
        co->stack.push_back(*it);
    }

    // If this is the first resume, we might need to adjust CallFrame varargs
    if (co->ip == 0 && co->frames.size() == 1) {
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
        // We need to adjust the values we just pushed to match what the yield expected.
        uint8_t expected = co->retCount;
        if (expected > 0) {
            if (pushedCount > expected) {
                // Truncate
                co->stack.resize(co->stack.size() - (pushedCount - expected));
            } else if (pushedCount < expected) {
                // Pad with nil
                for (size_t i = 0; i < (size_t)expected - pushedCount; i++) {
                    co->stack.push_back(Value::nil());
                }
            }
        }
    }

    // Save current coroutine and switch
    CoroutineObject* caller = vm->currentCoroutine();
    co->caller = caller;
    co->status = CoroutineObject::Status::RUNNING;
    if (caller) caller->status = CoroutineObject::Status::NORMAL;

    // Use a special VM method to run a coroutine
    // We need to implement VM::resumeCoroutine(co)
    bool success = vm->resumeCoroutine(co);

    // After resume returns (either finished or yielded)
    // Results are on co->stack
    
    // Switch back status
    if (caller) caller->status = CoroutineObject::Status::RUNNING;

    if (!success) {
        // Error occurred
        vm->push(Value::boolean(false));
        // Error message should be on co->stack top? 
        // Actually VM::run already reported it to Log::error.
        // For coroutines, we might want to capture it.
        vm->push(Value::runtimeString(vm->internString("error in coroutine")));
        return true;
    }

    // Success or Yield
    vm->push(Value::boolean(true));
    
    // Transfer results from co->stack to caller->stack
    if (co->status == CoroutineObject::Status::SUSPENDED) {
        // Yielded values are in yieldedValues
#ifdef DEBUG
        std::cout << "DEBUG resume SUSPENDED: transferring " << co->yieldedValues.size() << " values" << std::endl;
#endif
        size_t count = co->yieldedValues.size();
        for (const auto& val : co->yieldedValues) {
            vm->push(val);
        }
        vm->currentCoroutine()->lastResultCount = count + 1;
        co->yieldedValues.clear();
    } else {
        // Returned values
#ifdef DEBUG
        std::cout << "DEBUG resume DEAD: transferring " << co->stack.size() << " values" << std::endl;
#endif
        size_t count = co->stack.size();
        for (const auto& val : co->stack) {
#ifdef DEBUG
            std::cout << "  - " << val << std::endl;
#endif
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

    CoroutineObject* co = vm->getCoroutine(coVal.asThreadIndex());
    size_t strIdx = vm->internString(co->statusToString());
    vm->push(Value::runtimeString(strIdx));
    return true;
}

bool native_coroutine_running(VM* vm, int /*argCount*/) {
    size_t idx = vm->getCoroutineIndex(vm->currentCoroutine());
    if (idx != SIZE_MAX) {
        vm->push(Value::thread(idx));
    } else {
        vm->push(Value::nil());
    }
    return true;
}

bool native_coroutine_yield(VM* vm, int argCount) {
    CoroutineObject* co = vm->currentCoroutine();
    if (!co->caller) {
        vm->runtimeError("attempt to yield from outside a coroutine");
        return false;
    }

    // Move yielded values to co->yieldedValues
    co->yieldedValues.clear();
    for (int i = 0; i < argCount; i++) {
        co->yieldedValues.push_back(vm->pop());
    }
    std::reverse(co->yieldedValues.begin(), co->yieldedValues.end());

    co->status = CoroutineObject::Status::SUSPENDED;
    co->yieldCount = argCount;
    // co->retCount remains what the resume call expected? 
    // Actually, co->retCount should be set by the CALLER (resumer) to indicate how many values it expects back.
    // Wait, in Lua, yield() returns values passed to resume().
    // So the next resume() will push values and we need to know how many.
    // For now, let's just assume we want all of them.
    co->retCount = 0; 

    return true; // Return to resumer (this causes VM::run to exit and return to VM::resumeCoroutine)
}

} // anonymous namespace

void registerCoroutineLibrary(VM* vm, TableObject* coroutineTable) {
    vm->addNativeToTable(coroutineTable, "create", native_coroutine_create);
    vm->addNativeToTable(coroutineTable, "resume", native_coroutine_resume);
    vm->addNativeToTable(coroutineTable, "status", native_coroutine_status);
    vm->addNativeToTable(coroutineTable, "running", native_coroutine_running);
    vm->addNativeToTable(coroutineTable, "yield", native_coroutine_yield);
}
