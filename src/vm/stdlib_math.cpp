#include "vm/vm.hpp"
#include "compiler/chunk.hpp"
#include <cmath>
#include <algorithm>
#include <random>

namespace {

static std::mt19937 rng(std::random_device{}());

bool native_math_sqrt(VM* vm, int argCount) {
    if (argCount != 1) {
        vm->runtimeError("math.sqrt expects 1 argument");
        return false;
    }
    Value val = vm->peek(0);
    if (!val.isNumber()) {
        vm->runtimeError("math.sqrt expects number argument");
        return false;
    }
    double res = std::sqrt(val.asNumber());
    vm->pop();
    vm->push(Value::number(res));
    return true;
}

bool native_math_abs(VM* vm, int argCount) {
    if (argCount != 1) {
        vm->runtimeError("math.abs expects 1 argument");
        return false;
    }
    Value val = vm->peek(0);
    if (!val.isNumber()) {
        vm->runtimeError("math.abs expects number argument");
        return false;
    }
    double res = std::abs(val.asNumber());
    vm->pop();
    vm->push(Value::number(res));
    return true;
}

bool native_math_floor(VM* vm, int argCount) {
    if (argCount != 1) {
        vm->runtimeError("math.floor expects 1 argument");
        return false;
    }
    Value val = vm->peek(0);
    if (!val.isNumber()) {
        vm->runtimeError("math.floor expects number argument");
        return false;
    }
    double res = std::floor(val.asNumber());
    vm->pop();
    vm->push(Value::number(res));
    return true;
}

bool native_math_ceil(VM* vm, int argCount) {
    if (argCount != 1) {
        vm->runtimeError("math.ceil expects 1 argument");
        return false;
    }
    Value val = vm->peek(0);
    if (!val.isNumber()) {
        vm->runtimeError("math.ceil expects number argument");
        return false;
    }
    double res = std::ceil(val.asNumber());
    vm->pop();
    vm->push(Value::number(res));
    return true;
}

bool native_math_sin(VM* vm, int argCount) {
    if (argCount != 1) {
        vm->runtimeError("math.sin expects 1 argument");
        return false;
    }
    Value val = vm->peek(0);
    if (!val.isNumber()) {
        vm->runtimeError("math.sin expects number argument");
        return false;
    }
    double res = std::sin(val.asNumber());
    vm->pop();
    vm->push(Value::number(res));
    return true;
}

bool native_math_cos(VM* vm, int argCount) {
    if (argCount != 1) {
        vm->runtimeError("math.cos expects 1 argument");
        return false;
    }
    Value val = vm->peek(0);
    if (!val.isNumber()) {
        vm->runtimeError("math.cos expects number argument");
        return false;
    }
    double res = std::cos(val.asNumber());
    vm->pop();
    vm->push(Value::number(res));
    return true;
}

bool native_math_tan(VM* vm, int argCount) {
    if (argCount != 1) {
        vm->runtimeError("math.tan expects 1 argument");
        return false;
    }
    Value val = vm->peek(0);
    if (!val.isNumber()) {
        vm->runtimeError("math.tan expects number argument");
        return false;
    }
    double res = std::tan(val.asNumber());
    vm->pop();
    vm->push(Value::number(res));
    return true;
}

bool native_math_exp(VM* vm, int argCount) {
    if (argCount != 1) {
        vm->runtimeError("math.exp expects 1 argument");
        return false;
    }
    Value val = vm->peek(0);
    if (!val.isNumber()) {
        vm->runtimeError("math.exp expects number argument");
        return false;
    }
    double res = std::exp(val.asNumber());
    vm->pop();
    vm->push(Value::number(res));
    return true;
}

bool native_math_log(VM* vm, int argCount) {
    if (argCount != 1) {
        vm->runtimeError("math.log expects 1 argument");
        return false;
    }
    Value val = vm->peek(0);
    if (!val.isNumber()) {
        vm->runtimeError("math.log expects number argument");
        return false;
    }
    double res = std::log(val.asNumber());
    vm->pop();
    vm->push(Value::number(res));
    return true;
}

bool native_math_min(VM* vm, int argCount) {
    if (argCount == 0) {
        vm->runtimeError("math.min requires at least 1 argument");
        return false;
    }

    double minVal = INFINITY;
    for (int i = 0; i < argCount; i++) {
        Value val = vm->peek(i);
        if (!val.isNumber()) {
            vm->runtimeError("math.min expects number arguments");
            return false;
        }
        minVal = std::min(minVal, val.asNumber());
    }

    for(int i=0; i<argCount; i++) vm->pop();
    vm->push(Value::number(minVal));
    return true;
}

bool native_math_max(VM* vm, int argCount) {
    if (argCount == 0) {
        vm->runtimeError("math.max requires at least 1 argument");
        return false;
    }

    double maxVal = -INFINITY;
    for (int i = 0; i < argCount; i++) {
        Value val = vm->peek(i);
        if (!val.isNumber()) {
            vm->runtimeError("math.max expects number arguments");
            return false;
        }
        maxVal = std::max(maxVal, val.asNumber());
    }

    for(int i=0; i<argCount; i++) vm->pop();
    vm->push(Value::number(maxVal));
    return true;
}

bool native_math_random(VM* vm, int argCount) {
    Value result = Value::nil();
    if (argCount == 0) {
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        result = Value::number(dist(rng));
    } else if (argCount == 1) {
        Value nVal = vm->peek(0);
        if (!nVal.isNumber()) {
            vm->runtimeError("math.random expects number argument");
            return false;
        }
        int n = static_cast<int>(nVal.asNumber());
        if (n < 1) {
            vm->runtimeError("math.random interval is empty");
            return false;
        }
        std::uniform_int_distribution<int> dist(1, n);
        result = Value::number(dist(rng));
    } else if (argCount == 2) {
        Value mVal = vm->peek(0);
        Value nVal = vm->peek(1);
        if (!nVal.isNumber() || !mVal.isNumber()) {
            vm->runtimeError("math.random expects number arguments");
            return false;
        }
        int n = static_cast<int>(nVal.asNumber());
        int m = static_cast<int>(mVal.asNumber());
        if (n > m) {
            vm->runtimeError("math.random interval is empty");
            return false;
        }
        std::uniform_int_distribution<int> dist(n, m);
        result = Value::number(dist(rng));
    } else {
        vm->runtimeError("math.random expects 0, 1, or 2 arguments");
        return false;
    }
    
    for(int i=0; i<argCount; i++) vm->pop();
    vm->push(result);
    return true;
}

bool native_math_randomseed(VM* vm, int argCount) {
    if (argCount == 0) {
        rng.seed(std::random_device{}());
        vm->push(Value::number(0));
        vm->push(Value::number(0));
        return true;
    }
    
    Value seedVal = vm->peek(argCount - 1);
    if (seedVal.isNumber()) {
        rng.seed(static_cast<uint32_t>(seedVal.asNumber()));
    }
    
    for(int i=0; i<argCount; i++) vm->pop();
    vm->push(Value::nil());
    
    return true;
}

} // anonymous namespace

void registerMathLibrary(VM* vm, TableObject* mathTable) {
    vm->addNativeToTable(mathTable, "sqrt", native_math_sqrt);
    vm->addNativeToTable(mathTable, "abs", native_math_abs);
    vm->addNativeToTable(mathTable, "floor", native_math_floor);
    vm->addNativeToTable(mathTable, "ceil", native_math_ceil);
    vm->addNativeToTable(mathTable, "sin", native_math_sin);
    vm->addNativeToTable(mathTable, "cos", native_math_cos);
    vm->addNativeToTable(mathTable, "tan", native_math_tan);
    vm->addNativeToTable(mathTable, "exp", native_math_exp);
    vm->addNativeToTable(mathTable, "log", native_math_log);
    vm->addNativeToTable(mathTable, "min", native_math_min);
    vm->addNativeToTable(mathTable, "max", native_math_max);
    vm->addNativeToTable(mathTable, "random", native_math_random);
    vm->addNativeToTable(mathTable, "randomseed", native_math_randomseed);

    // Add math.pi constant
    mathTable->set("pi", Value::number(M_PI));

    // Add math.huge constant
    mathTable->set("huge", Value::number(HUGE_VAL));
}
