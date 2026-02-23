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
    for (auto it = args.rbegin(); it != args.rend(); ++it) {
        co->stack.push_back(*it);
    }

    // If this is the first resume, we might need to adjust CallFrame varargCount
    if (co->frames.size() == 1 && co->frames[0].ip == 0) {
        co->frames[0].varargCount = static_cast<uint8_t>(args.size());
        co->frames[0].varargBase = co->stack.size() - args.size();
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
    if (co->status == CoroutineObject::Status::RUNNING) {
        co->status = CoroutineObject::Status::DEAD;
    }
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
        // Yielded values
        size_t count = co->yieldCount;
        std::vector<Value> yielded;
        for (size_t i = 0; i < count; i++) {
            yielded.push_back(co->stack.back());
            co->stack.pop_back();
        }
        for (auto it = yielded.rbegin(); it != yielded.rend(); ++it) {
            vm->push(*it);
        }
    } else {
        // Returned values
        // Everything on co->stack are results
        for (const auto& val : co->stack) {
            vm->push(val);
        }
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

// coroutine.wrap is better implemented in Lua if possible, or using a special native closure
// For now, let's skip wrap or implement it simply.

} // anonymous namespace

void registerCoroutineLibrary(VM* vm, TableObject* coroutineTable) {
    vm->addNativeToTable(coroutineTable, "create", native_coroutine_create);
    vm->addNativeToTable(coroutineTable, "resume", native_coroutine_resume);
    vm->addNativeToTable(coroutineTable, "status", native_coroutine_status);
    vm->addNativeToTable(coroutineTable, "running", native_coroutine_running);
}
