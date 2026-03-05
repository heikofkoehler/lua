#include "vm/vm.hpp"
#include "value/value.hpp"
#include <cmath>
#include <algorithm>
#include <random>
#include <limits>
#include "compiler/chunk.hpp"

namespace {

bool native_math_sqrt(VM* vm, int argCount) {
    if (argCount < 1) { vm->runtimeError("math.sqrt expects 1 argument"); return false; }
    Value val = vm->peek(0);
    vm->pop();
    vm->push(Value::number(std::sqrt(val.asNumber())));
    return true;
}

bool native_math_abs(VM* vm, int argCount) {
    if (argCount < 1) { vm->runtimeError("math.abs expects 1 argument"); return false; }
    Value val = vm->peek(0);
    vm->pop();
    if (val.isInteger()) {
        int64_t i = val.asInteger();
        vm->push(Value::integer(i < 0 ? -i : i));
    } else {
        vm->push(Value::number(std::abs(val.asNumber())));
    }
    return true;
}

bool native_math_floor(VM* vm, int argCount) {
    if (argCount < 1) { vm->runtimeError("math.floor expects 1 argument"); return false; }
    Value val = vm->peek(0);
    vm->pop();
    if (val.isInteger()) {
        vm->push(val);
    } else {
        vm->push(Value::number(std::floor(val.asNumber())));
    }
    return true;
}

bool native_math_ceil(VM* vm, int argCount) {
    if (argCount < 1) { vm->runtimeError("math.ceil expects 1 argument"); return false; }
    Value val = vm->peek(0);
    vm->pop();
    if (val.isInteger()) {
        vm->push(val);
    } else {
        vm->push(Value::number(std::ceil(val.asNumber())));
    }
    return true;
}

bool native_math_sin(VM* vm, int argCount) {
    if (argCount < 1) { vm->runtimeError("math.sin expects 1 argument"); return false; }
    Value val = vm->peek(0);
    vm->pop();
    vm->push(Value::number(std::sin(val.asNumber())));
    return true;
}

bool native_math_cos(VM* vm, int argCount) {
    if (argCount < 1) { vm->runtimeError("math.cos expects 1 argument"); return false; }
    Value val = vm->peek(0);
    vm->pop();
    vm->push(Value::number(std::cos(val.asNumber())));
    return true;
}

bool native_math_tan(VM* vm, int argCount) {
    if (argCount < 1) { vm->runtimeError("math.tan expects 1 argument"); return false; }
    Value val = vm->peek(0);
    vm->pop();
    vm->push(Value::number(std::tan(val.asNumber())));
    return true;
}

bool native_math_exp(VM* vm, int argCount) {
    if (argCount < 1) { vm->runtimeError("math.exp expects 1 argument"); return false; }
    Value val = vm->peek(0);
    vm->pop();
    vm->push(Value::number(std::exp(val.asNumber())));
    return true;
}

bool native_math_log(VM* vm, int argCount) {
    if (argCount < 1) { vm->runtimeError("math.log expects at least 1 argument"); return false; }
    double x = vm->peek(argCount - 1).asNumber();
    double base = (argCount >= 2) ? vm->peek(argCount - 2).asNumber() : std::exp(1.0);
    for (int i = 0; i < argCount; i++) vm->pop();
    if (argCount >= 2) {
        vm->push(Value::number(std::log(x) / std::log(base)));
    } else {
        vm->push(Value::number(std::log(x)));
    }
    return true;
}

bool native_math_min(VM* vm, int argCount) {
    if (argCount < 1) { vm->runtimeError("math.min expects at least 1 argument"); return false; }
    Value minVal = vm->peek(argCount - 1);
    for (int i = 1; i < argCount; i++) {
        Value val = vm->peek(argCount - 1 - i);
        if (val.asNumber() < minVal.asNumber()) {
            minVal = val;
        }
    }
    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(minVal);
    return true;
}

bool native_math_max(VM* vm, int argCount) {
    if (argCount < 1) { vm->runtimeError("math.max expects at least 1 argument"); return false; }
    Value maxVal = vm->peek(argCount - 1);
    for (int i = 1; i < argCount; i++) {
        Value val = vm->peek(argCount - 1 - i);
        if (val.asNumber() > maxVal.asNumber()) {
            maxVal = val;
        }
    }
    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(maxVal);
    return true;
}

bool native_math_acos(VM* vm, int argCount) {
    if (argCount < 1) { vm->runtimeError("math.acos expects 1 argument"); return false; }
    Value val = vm->peek(0);
    vm->pop();
    vm->push(Value::number(std::acos(val.asNumber())));
    return true;
}

bool native_math_asin(VM* vm, int argCount) {
    if (argCount < 1) { vm->runtimeError("math.asin expects 1 argument"); return false; }
    Value val = vm->peek(0);
    vm->pop();
    vm->push(Value::number(std::asin(val.asNumber())));
    return true;
}

bool native_math_atan(VM* vm, int argCount) {
    if (argCount < 1) { vm->runtimeError("math.atan expects at least 1 argument"); return false; }
    double y = vm->peek(argCount - 1).asNumber();
    if (argCount >= 2) {
        double x = vm->peek(argCount - 2).asNumber();
        for (int i = 0; i < argCount; i++) vm->pop();
        vm->push(Value::number(std::atan2(y, x)));
    } else {
        vm->pop();
        vm->push(Value::number(std::atan(y)));
    }
    return true;
}

bool native_math_deg(VM* vm, int argCount) {
    if (argCount < 1) { vm->runtimeError("math.deg expects 1 argument"); return false; }
    Value val = vm->peek(0);
    vm->pop();
    vm->push(Value::number(val.asNumber() * (180.0 / M_PI)));
    return true;
}

bool native_math_rad(VM* vm, int argCount) {
    if (argCount < 1) { vm->runtimeError("math.rad expects 1 argument"); return false; }
    Value val = vm->peek(0);
    vm->pop();
    vm->push(Value::number(val.asNumber() * (M_PI / 180.0)));
    return true;
}

bool native_math_fmod(VM* vm, int argCount) {
    if (argCount < 2) { vm->runtimeError("math.fmod expects 2 arguments"); return false; }
    double x = vm->peek(argCount - 1).asNumber();
    double y = vm->peek(argCount - 2).asNumber();
    vm->pop(); vm->pop();
    vm->push(Value::number(std::fmod(x, y)));
    return true;
}

bool native_math_modf(VM* vm, int argCount) {
    if (argCount < 1) { vm->runtimeError("math.modf expects 1 argument"); return false; }
    double x = vm->peek(0).asNumber();
    vm->pop();
    double i;
    double f = std::modf(x, &i);
    vm->push(Value::number(i));
    vm->push(Value::number(f));
    vm->currentCoroutine()->lastResultCount = 2;
    return true;
}

bool native_math_random(VM* vm, int argCount) {
    static std::mt19937 rng(std::random_device{}());
    if (argCount == 0) {
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        vm->push(Value::number(dist(rng)));
    } else if (argCount == 1) {
        int64_t m = vm->peek(0).asInteger();
        vm->pop();
        if (m < 1) { vm->runtimeError("bad argument #1 to 'random' (interval is empty)"); return false; }
        std::uniform_int_distribution<int64_t> dist(1, m);
        vm->push(Value::integer(dist(rng)));
    } else {
        int64_t m = vm->peek(argCount - 1).asInteger();
        int64_t n = vm->peek(argCount - 2).asInteger();
        vm->pop(); vm->pop();
        if (m > n) { vm->runtimeError("bad argument #1 to 'random' (interval is empty)"); return false; }
        std::uniform_int_distribution<int64_t> dist(m, n);
        vm->push(Value::integer(dist(rng)));
    }
    return true;
}

bool native_math_randomseed(VM* vm, int argCount) {
    static std::mt19937 rng(std::random_device{}());
    if (argCount >= 1) {
        rng.seed(static_cast<unsigned int>(vm->peek(argCount - 1).asNumber()));
    }
    for (int i = 0; i < argCount; i++) vm->pop();
    return true;
}

bool native_math_type(VM* vm, int argCount) {
    if (argCount < 1) { vm->runtimeError("math.type expects 1 argument"); return false; }
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

bool native_math_tointeger(VM* vm, int argCount) {
    if (argCount < 1) { vm->runtimeError("math.tointeger expects 1 argument"); return false; }
    Value val = vm->peek(0);
    vm->pop();
    if (val.isInteger()) {
        vm->push(val);
    } else if (val.isNumber()) {
        double d = val.asNumber();
        double i;
        if (std::modf(d, &i) == 0.0) {
            vm->push(Value::integer(static_cast<int64_t>(i)));
        } else {
            vm->push(Value::nil());
        }
    } else {
        vm->push(Value::nil());
    }
    return true;
}

bool native_math_ult(VM* vm, int argCount) {
    if (argCount < 2) { vm->runtimeError("math.ult expects 2 arguments"); return false; }
    uint64_t m = static_cast<uint64_t>(vm->peek(argCount - 1).asInteger());
    uint64_t n = static_cast<uint64_t>(vm->peek(argCount - 2).asInteger());
    vm->pop(); vm->pop();
    vm->push(Value::boolean(m < n));
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
    vm->addNativeToTable(mathTable, "tointeger", native_math_tointeger);
    vm->addNativeToTable(mathTable, "ult", native_math_ult);

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

    // Add math.maxinteger and math.mininteger
    mathTable->set("maxinteger", Value::integer(0x00007FFFFFFFFFFFULL));
    mathTable->set("mininteger", Value::integer(0x0000800000000000ULL));
}
