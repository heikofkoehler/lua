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
    if (argCount < 1 || argCount > 2) {
        vm->runtimeError("math.log expects 1 or 2 arguments");
        return false;
    }
    Value val = vm->peek(argCount - 1);
    if (!val.isNumber()) {
        vm->runtimeError("math.log expects number argument");
        return false;
    }
    double res = std::log(val.asNumber());
    if (argCount == 2) {
        Value base = vm->peek(0);
        if (!base.isNumber()) {
            vm->runtimeError("math.log base expects number");
            return false;
        }
        res /= std::log(base.asNumber());
    }
    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(Value::number(res));
    return true;
}

bool native_math_acos(VM* vm, int argCount) {
    if (argCount != 1) { vm->runtimeError("math.acos expects 1 argument"); return false; }
    Value val = vm->peek(0);
    if (!val.isNumber()) { vm->runtimeError("math.acos expects number argument"); return false; }
    vm->pop();
    vm->push(Value::number(std::acos(val.asNumber())));
    return true;
}

bool native_math_asin(VM* vm, int argCount) {
    if (argCount != 1) { vm->runtimeError("math.asin expects 1 argument"); return false; }
    Value val = vm->peek(0);
    if (!val.isNumber()) { vm->runtimeError("math.asin expects number argument"); return false; }
    vm->pop();
    vm->push(Value::number(std::asin(val.asNumber())));
    return true;
}

bool native_math_atan(VM* vm, int argCount) {
    if (argCount < 1 || argCount > 2) { vm->runtimeError("math.atan expects 1 or 2 arguments"); return false; }
    Value y = vm->peek(argCount - 1);
    if (!y.isNumber()) { vm->runtimeError("math.atan expects number arguments"); return false; }
    double res;
    if (argCount == 1) {
        res = std::atan(y.asNumber());
    } else {
        Value x = vm->peek(0);
        if (!x.isNumber()) { vm->runtimeError("math.atan expects number arguments"); return false; }
        res = std::atan2(y.asNumber(), x.asNumber());
    }
    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(Value::number(res));
    return true;
}

bool native_math_deg(VM* vm, int argCount) {
    if (argCount != 1) { vm->runtimeError("math.deg expects 1 argument"); return false; }
    Value val = vm->peek(0);
    if (!val.isNumber()) { vm->runtimeError("math.deg expects number argument"); return false; }
    vm->pop();
    vm->push(Value::number(val.asNumber() * (180.0 / M_PI)));
    return true;
}

bool native_math_rad(VM* vm, int argCount) {
    if (argCount != 1) { vm->runtimeError("math.rad expects 1 argument"); return false; }
    Value val = vm->peek(0);
    if (!val.isNumber()) { vm->runtimeError("math.rad expects number argument"); return false; }
    vm->pop();
    vm->push(Value::number(val.asNumber() * (M_PI / 180.0)));
    return true;
}

bool native_math_fmod(VM* vm, int argCount) {
    if (argCount != 2) { vm->runtimeError("math.fmod expects 2 arguments"); return false; }
    Value y = vm->peek(0);
    Value x = vm->peek(1);
    if (!x.isNumber() || !y.isNumber()) { vm->runtimeError("math.fmod expects number arguments"); return false; }
    vm->pop(); vm->pop();
    vm->push(Value::number(std::fmod(x.asNumber(), y.asNumber())));
    return true;
}

bool native_math_modf(VM* vm, int argCount) {
    if (argCount != 1) { vm->runtimeError("math.modf expects 1 argument"); return false; }
    Value val = vm->peek(0);
    if (!val.isNumber()) { vm->runtimeError("math.modf expects number argument"); return false; }
    vm->pop();
    double intpart;
    double fracpart = std::modf(val.asNumber(), &intpart);
    vm->push(Value::number(intpart));
    vm->push(Value::number(fracpart));
    vm->currentCoroutine()->lastResultCount = 2;
    return true;
}

bool native_math_min(VM* vm, int argCount) {
    if (argCount == 0) {
        vm->runtimeError("math.min requires at least 1 argument");
        return false;
    }

    Value minVal = vm->peek(argCount - 1);
    for (int i = 1; i < argCount; i++) {
        Value val = vm->peek(argCount - 1 - i);
        if (!val.isNumber()) {
            vm->runtimeError("math.min expects number arguments");
            return false;
        }
        
        bool isLess;
        if (minVal.isInteger() && val.isInteger()) {
            isLess = val.asInteger() < minVal.asInteger();
        } else {
            isLess = val.asNumber() < minVal.asNumber();
        }
        
        if (isLess) minVal = val;
    }

    for(int i=0; i<argCount; i++) vm->pop();
    vm->push(minVal);
    return true;
}

bool native_math_max(VM* vm, int argCount) {
    if (argCount == 0) {
        vm->runtimeError("math.max requires at least 1 argument");
        return false;
    }

    Value maxVal = vm->peek(argCount - 1);
    for (int i = 1; i < argCount; i++) {
        Value val = vm->peek(argCount - 1 - i);
        if (!val.isNumber()) {
            vm->runtimeError("math.max expects number arguments");
            return false;
        }
        
        bool isGreater;
        if (maxVal.isInteger() && val.isInteger()) {
            isGreater = val.asInteger() > maxVal.asInteger();
        } else {
            isGreater = val.asNumber() > maxVal.asNumber();
        }
        
        if (isGreater) maxVal = val;
    }

    for(int i=0; i<argCount; i++) vm->pop();
    vm->push(maxVal);
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

bool native_math_type(VM* vm, int argCount) {
    if (argCount != 1) {
        vm->runtimeError("math.type expects 1 argument");
        return false;
    }
    Value val = vm->peek(0);
    vm->pop();
    if (val.isInteger()) {
        vm->push(Value::runtimeString(vm->internString("integer")));
    } else if (val.isNumber()) {
        vm->push(Value::runtimeString(vm->internString("float")));
    } else {
        vm->push(Value::nil());
    }
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
    vm->addNativeToTable(mathTable, "type", native_math_type);

    vm->addNativeToTable(mathTable, "acos", native_math_acos);
    vm->addNativeToTable(mathTable, "asin", native_math_asin);
    vm->addNativeToTable(mathTable, "atan", native_math_atan);
    vm->addNativeToTable(mathTable, "deg", native_math_deg);
    vm->addNativeToTable(mathTable, "rad", native_math_rad);
    vm->addNativeToTable(mathTable, "fmod", native_math_fmod);
    vm->addNativeToTable(mathTable, "modf", native_math_modf);

    // Add math.pi constant
    mathTable->set("pi", Value::number(M_PI));

    // Add math.huge constant
    mathTable->set("huge", Value::number(HUGE_VAL));
}
