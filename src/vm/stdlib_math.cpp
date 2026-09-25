#include "vm/vm.hpp"
#include "value/value.hpp"
#include <cmath>
#include <algorithm>
#include <random>
#include <limits>
#include <ctime>
#include "compiler/chunk.hpp"

namespace {

static bool checkNumber(VM* vm, const Value& val, const char* funcName, int argNum, Value& out) {
    if (val.isNumber()) {
        out = val;
        return true;
    }
    if (val.isString()) {
        double d;
        int64_t i;
        bool isInt;
        if (vm->stringToNumber(vm->getStringValue(val), d, i, isInt)) {
            out = isInt ? vm->makeInteger(i) : Value::number(d);
            return true;
        }
    }
    vm->runtimeError(std::string("bad argument #") + std::to_string(argNum) + " to '" + funcName + "' (number expected, got " + vm->typeName(val) + ")");
    return false;
}

static bool checkInteger(VM* vm, const Value& val, const char* funcName, int argNum, int64_t& out) {
    if (val.isInteger()) {
        out = val.asInteger();
        return true;
    }
    if (val.isFloat()) {
        double d = val.asNumber();
        double ip;
        if (std::modf(d, &ip) == 0.0 && !std::isinf(d) && !std::isnan(d) && d >= -9223372036854775808.0 && d < 9223372036854775808.0) {
            out = static_cast<int64_t>(d);
            return true;
        }
        vm->runtimeError(std::string("bad argument #") + std::to_string(argNum) + " to '" + funcName + "' (number has no integer representation)");
        return false;
    }
    if (val.isString()) {
        int64_t i;
        if (vm->stringToInteger(vm->getStringValue(val), i)) {
            out = i;
            return true;
        }
        double d;
        bool isInt;
        if (vm->stringToNumber(vm->getStringValue(val), d, i, isInt)) {
            double ip;
            if (std::modf(d, &ip) == 0.0 && !std::isinf(d) && !std::isnan(d) && d >= -9223372036854775808.0 && d < 9223372036854775808.0) {
                out = static_cast<int64_t>(d);
                return true;
            }
            vm->runtimeError(std::string("bad argument #") + std::to_string(argNum) + " to '" + funcName + "' (number has no integer representation)");
            return false;
        }
    }
    vm->runtimeError(std::string("bad argument #") + std::to_string(argNum) + " to '" + funcName + "' (number expected, got " + vm->typeName(val) + ")");
    return false;
}

bool native_math_sqrt(VM* vm, int argCount) {
    if (argCount < 1) { vm->runtimeError("bad argument #1 to 'sqrt' (value expected)"); return false; }
    Value raw = vm->peek(argCount - 1);
    Value val;
    if (!checkNumber(vm, raw, "sqrt", 1, val)) return false;
    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(Value::number(std::sqrt(val.asNumber())));
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_math_abs(VM* vm, int argCount) {
    if (argCount < 1) { vm->runtimeError("bad argument #1 to 'abs' (value expected)"); return false; }
    Value raw = vm->peek(argCount - 1);
    Value val;
    if (!checkNumber(vm, raw, "abs", 1, val)) return false;
    for (int i = 0; i < argCount; i++) vm->pop();
    if (val.isInteger()) {
        int64_t i = val.asInteger();
        if (i < 0) {
            uint64_t u = 0ULL - static_cast<uint64_t>(i);
            vm->push(vm->makeInteger(static_cast<int64_t>(u)));
        } else {
            vm->push(val);
        }
    } else {
        vm->push(Value::number(std::abs(val.asNumber())));
    }
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_math_floor(VM* vm, int argCount) {
    if (argCount < 1) { vm->runtimeError("bad argument #1 to 'floor' (value expected)"); return false; }
    Value raw = vm->peek(argCount - 1);
    Value val;
    if (!checkNumber(vm, raw, "floor", 1, val)) return false;
    for (int i = 0; i < argCount; i++) vm->pop();
    if (val.isInteger()) {
        vm->push(val);
    } else {
        double d = std::floor(val.asNumber());
        int64_t i = 0;
        if (std::isfinite(d) && d >= -9223372036854775808.0 && d < 9223372036854775808.0) {
            i = static_cast<int64_t>(d);
            if (static_cast<double>(i) == d) {
                vm->push(vm->makeInteger(i));
                vm->currentCoroutine()->lastResultCount = 1;
                return true;
            }
        }
        vm->push(Value::number(d));
    }
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_math_ceil(VM* vm, int argCount) {
    if (argCount < 1) { vm->runtimeError("bad argument #1 to 'ceil' (value expected)"); return false; }
    Value raw = vm->peek(argCount - 1);
    Value val;
    if (!checkNumber(vm, raw, "ceil", 1, val)) return false;
    for (int i = 0; i < argCount; i++) vm->pop();
    if (val.isInteger()) {
        vm->push(val);
    } else {
        double d = std::ceil(val.asNumber());
        int64_t i = 0;
        if (std::isfinite(d) && d >= -9223372036854775808.0 && d < 9223372036854775808.0) {
            i = static_cast<int64_t>(d);
            if (static_cast<double>(i) == d) {
                vm->push(vm->makeInteger(i));
                vm->currentCoroutine()->lastResultCount = 1;
                return true;
            }
        }
        vm->push(Value::number(d));
    }
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_math_sin(VM* vm, int argCount) {
    if (argCount < 1) { vm->runtimeError("bad argument #1 to 'sin' (value expected)"); return false; }
    Value raw = vm->peek(argCount - 1);
    Value val;
    if (!checkNumber(vm, raw, "sin", 1, val)) return false;
    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(Value::number(std::sin(val.asNumber())));
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_math_cos(VM* vm, int argCount) {
    if (argCount < 1) { vm->runtimeError("bad argument #1 to 'cos' (value expected)"); return false; }
    Value raw = vm->peek(argCount - 1);
    Value val;
    if (!checkNumber(vm, raw, "cos", 1, val)) return false;
    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(Value::number(std::cos(val.asNumber())));
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_math_tan(VM* vm, int argCount) {
    if (argCount < 1) { vm->runtimeError("bad argument #1 to 'tan' (value expected)"); return false; }
    Value raw = vm->peek(argCount - 1);
    Value val;
    if (!checkNumber(vm, raw, "tan", 1, val)) return false;
    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(Value::number(std::tan(val.asNumber())));
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_math_exp(VM* vm, int argCount) {
    if (argCount < 1) { vm->runtimeError("bad argument #1 to 'exp' (value expected)"); return false; }
    Value raw = vm->peek(argCount - 1);
    Value val;
    if (!checkNumber(vm, raw, "exp", 1, val)) return false;
    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(Value::number(std::exp(val.asNumber())));
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_math_log(VM* vm, int argCount) {
    if (argCount < 1) { vm->runtimeError("bad argument #1 to 'log' (value expected)"); return false; }
    Value rawX = vm->peek(argCount - 1);
    Value xVal;
    if (!checkNumber(vm, rawX, "log", 1, xVal)) return false;
    double x = xVal.asNumber();
    double base = std::exp(1.0);
    if (argCount >= 2) {
        Value rawBase = vm->peek(argCount - 2);
        Value baseVal;
        if (!checkNumber(vm, rawBase, "log", 2, baseVal)) return false;
        base = baseVal.asNumber();
    }
    for (int i = 0; i < argCount; i++) vm->pop();
    if (argCount >= 2) {
        vm->push(Value::number(std::log(x) / std::log(base)));
    } else {
        vm->push(Value::number(std::log(x)));
    }
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_math_min(VM* vm, int argCount) {
    if (argCount < 1) { vm->runtimeError("bad argument #1 to 'min' (value expected)"); return false; }
    Value rawFirst = vm->peek(argCount - 1);
    Value minVal;
    if (!checkNumber(vm, rawFirst, "min", 1, minVal)) return false;
    for (int i = 1; i < argCount; i++) {
        Value raw = vm->peek(argCount - 1 - i);
        Value val;
        if (!checkNumber(vm, raw, "min", i + 1, val)) return false;
        if (vm->less(val, minVal).asBool()) {
            minVal = val;
        }
    }
    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(minVal);
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_math_max(VM* vm, int argCount) {
    if (argCount < 1) { vm->runtimeError("bad argument #1 to 'max' (value expected)"); return false; }
    Value rawFirst = vm->peek(argCount - 1);
    Value maxVal;
    if (!checkNumber(vm, rawFirst, "max", 1, maxVal)) return false;
    for (int i = 1; i < argCount; i++) {
        Value raw = vm->peek(argCount - 1 - i);
        Value val;
        if (!checkNumber(vm, raw, "max", i + 1, val)) return false;
        if (vm->less(maxVal, val).asBool()) {
            maxVal = val;
        }
    }
    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(maxVal);
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_math_acos(VM* vm, int argCount) {
    if (argCount < 1) { vm->runtimeError("bad argument #1 to 'acos' (value expected)"); return false; }
    Value raw = vm->peek(argCount - 1);
    Value val;
    if (!checkNumber(vm, raw, "acos", 1, val)) return false;
    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(Value::number(std::acos(val.asNumber())));
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_math_asin(VM* vm, int argCount) {
    if (argCount < 1) { vm->runtimeError("bad argument #1 to 'asin' (value expected)"); return false; }
    Value raw = vm->peek(argCount - 1);
    Value val;
    if (!checkNumber(vm, raw, "asin", 1, val)) return false;
    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(Value::number(std::asin(val.asNumber())));
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_math_atan(VM* vm, int argCount) {
    if (argCount < 1) { vm->runtimeError("bad argument #1 to 'atan' (value expected)"); return false; }
    Value rawY = vm->peek(argCount - 1);
    Value yVal;
    if (!checkNumber(vm, rawY, "atan", 1, yVal)) return false;
    double y = yVal.asNumber();
    if (argCount >= 2) {
        Value rawX = vm->peek(argCount - 2);
        Value xVal;
        if (!checkNumber(vm, rawX, "atan", 2, xVal)) return false;
        double x = xVal.asNumber();
        for (int i = 0; i < argCount; i++) vm->pop();
        vm->push(Value::number(std::atan2(y, x)));
    } else {
        for (int i = 0; i < argCount; i++) vm->pop();
        vm->push(Value::number(std::atan(y)));
    }
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_math_deg(VM* vm, int argCount) {
    if (argCount < 1) { vm->runtimeError("bad argument #1 to 'deg' (value expected)"); return false; }
    Value raw = vm->peek(argCount - 1);
    Value val;
    if (!checkNumber(vm, raw, "deg", 1, val)) return false;
    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(Value::number(val.asNumber() * (180.0 / M_PI)));
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_math_rad(VM* vm, int argCount) {
    if (argCount < 1) { vm->runtimeError("bad argument #1 to 'rad' (value expected)"); return false; }
    Value raw = vm->peek(argCount - 1);
    Value val;
    if (!checkNumber(vm, raw, "rad", 1, val)) return false;
    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(Value::number(val.asNumber() * (M_PI / 180.0)));
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_math_fmod(VM* vm, int argCount) {
    if (argCount < 1) { vm->runtimeError("bad argument #1 to 'fmod' (value expected)"); return false; }
    if (argCount < 2) { vm->runtimeError("bad argument #2 to 'fmod' (value expected)"); return false; }
    Value rawX = vm->peek(argCount - 1);
    Value rawY = vm->peek(argCount - 2);
    Value xVal, yVal;
    if (!checkNumber(vm, rawX, "fmod", 1, xVal)) return false;
    if (!checkNumber(vm, rawY, "fmod", 2, yVal)) return false;
    if (xVal.isInteger() && yVal.isInteger()) {
        int64_t d = yVal.asInteger();
        if (static_cast<uint64_t>(d) + 1ULL <= 1ULL) {
            if (d == 0) {
                vm->runtimeError("bad argument #2 to 'fmod' (zero)");
                return false;
            }
            for (int i = 0; i < argCount; i++) vm->pop();
            vm->push(vm->makeInteger(0));
        } else {
            for (int i = 0; i < argCount; i++) vm->pop();
            vm->push(vm->makeInteger(xVal.asInteger() % d));
        }
    } else {
        for (int i = 0; i < argCount; i++) vm->pop();
        vm->push(Value::number(std::fmod(xVal.asNumber(), yVal.asNumber())));
    }
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_math_modf(VM* vm, int argCount) {
    if (argCount < 1) { vm->runtimeError("bad argument #1 to 'modf' (number expected, got no value)"); return false; }
    Value arg = vm->peek(argCount - 1);
    if (!arg.isNumber() && !arg.isInteger()) {
        if (arg.isString()) {
            double d;
            int64_t i;
            bool isInt;
            if (vm->stringToNumber(vm->getStringValue(arg), d, i, isInt)) {
                arg = isInt ? Value::integer(i) : Value::number(d);
            } else {
                vm->runtimeError("bad argument #1 to 'modf' (number expected, got string)");
                return false;
            }
        } else {
            vm->runtimeError("bad argument #1 to 'modf' (number expected, got " + arg.typeToString() + ")");
            return false;
        }
    }
    for (int i = 0; i < argCount; i++) vm->pop();
    if (arg.isInteger()) {
        vm->push(arg);
        vm->push(Value::number(0.0));
    } else {
        double x = arg.asNumber();
        double i;
        double f = std::modf(x, &i);
        vm->push(Value::number(i));
        vm->push(Value::number(f));
    }
    vm->currentCoroutine()->lastResultCount = 2;
    return true;
}

bool native_math_frexp(VM* vm, int argCount) {
    if (argCount < 1) { vm->runtimeError("bad argument #1 to 'frexp' (value expected)"); return false; }
    Value arg = vm->peek(argCount - 1);
    if (!arg.isNumber()) {
        if (arg.isString()) {
            double d; int64_t i; bool isInt;
            if (vm->stringToNumber(vm->getStringValue(arg), d, i, isInt)) {
                arg = isInt ? vm->makeInteger(i) : Value::number(d);
            } else {
                vm->runtimeError("bad argument #1 to 'frexp' (number expected, got string)");
                return false;
            }
        } else {
            vm->runtimeError("bad argument #1 to 'frexp' (number expected, got " + arg.typeToString() + ")");
            return false;
        }
    }
    for (int i = 0; i < argCount; i++) vm->pop();
    double x = arg.asNumber();
    int exp = 0;
    double m = std::frexp(x, &exp);
    vm->push(Value::number(m));
    vm->push(vm->makeInteger(exp));
    vm->currentCoroutine()->lastResultCount = 2;
    return true;
}

bool native_math_ldexp(VM* vm, int argCount) {
    if (argCount < 1) { vm->runtimeError("bad argument #1 to 'ldexp' (value expected)"); return false; }
    if (argCount < 2) { vm->runtimeError("bad argument #2 to 'ldexp' (value expected)"); return false; }
    Value v1 = vm->peek(argCount - 1);
    Value v2 = vm->peek(argCount - 2);
    if (!v1.isNumber()) {
        if (v1.isString()) {
            double d; int64_t i; bool isInt;
            if (vm->stringToNumber(vm->getStringValue(v1), d, i, isInt)) {
                v1 = isInt ? vm->makeInteger(i) : Value::number(d);
            } else {
                vm->runtimeError("bad argument #1 to 'ldexp' (number expected, got string)");
                return false;
            }
        } else {
            vm->runtimeError("bad argument #1 to 'ldexp' (number expected, got " + v1.typeToString() + ")");
            return false;
        }
    }
    int64_t exp = 0;
    if (!vm->toInteger(v2, exp)) {
        vm->runtimeError("bad argument #2 to 'ldexp' (number has no integer representation)");
        return false;
    }
    for (int i = 0; i < argCount; i++) vm->pop();
    int e = (exp > 10000) ? 10000 : (exp < -10000 ? -10000 : static_cast<int>(exp));
    double res = std::ldexp(v1.asNumber(), e);
    vm->push(Value::number(res));
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

static inline uint64_t rotl(uint64_t x, int n) {
    return (x << n) | (x >> (64 - n));
}

static uint64_t nextrand(uint64_t* state) {
    uint64_t state0 = state[0];
    uint64_t state1 = state[1];
    uint64_t state2 = state[2] ^ state0;
    uint64_t state3 = state[3] ^ state1;
    uint64_t res = rotl(state1 * 5, 7) * 9;
    state[0] = state0 ^ state3;
    state[1] = state1 ^ state2;
    state[2] = state2 ^ (state1 << 17);
    state[3] = rotl(state3, 45);
    return res;
}

static double I2d(uint64_t x) {
    int64_t sx = static_cast<int64_t>(x >> (64 - 53));
    double res = static_cast<double>(sx) * (0.5 / static_cast<double>(1ULL << 52));
    if (sx < 0)
        res += 1.0;
    return res;
}

static uint64_t project(uint64_t ran, uint64_t n, uint64_t* state) {
    if ((n & (n + 1)) == 0) {
        return ran & n;
    } else {
        uint64_t lim = n;
        lim |= (lim >> 1);
        lim |= (lim >> 2);
        lim |= (lim >> 4);
        lim |= (lim >> 8);
        lim |= (lim >> 16);
        lim |= (lim >> 32);
        while ((ran &= lim) > n) {
            ran = nextrand(state);
        }
        return ran;
    }
}

static void setseed(uint64_t* state, uint64_t n1, uint64_t n2) {
    state[0] = n1;
    state[1] = 0xff;
    state[2] = n2;
    state[3] = 0;
    for (int i = 0; i < 16; i++) {
        nextrand(state);
    }
}

bool native_math_random(VM* vm, int argCount) {
    uint64_t* state = vm->rngState();
    uint64_t rv = nextrand(state);
    if (argCount == 0) {
        vm->push(Value::number(I2d(rv)));
        vm->currentCoroutine()->lastResultCount = 1;
        return true;
    }
    int64_t low = 1, up = 0;
    if (argCount == 1) {
        if (!checkInteger(vm, vm->peek(argCount - 1), "random", 1, up)) return false;
        if (up == 0) {
            for (int i = 0; i < argCount; i++) vm->pop();
            vm->push(vm->makeInteger(static_cast<int64_t>(rv)));
            vm->currentCoroutine()->lastResultCount = 1;
            return true;
        }
    } else if (argCount == 2) {
        if (!checkInteger(vm, vm->peek(argCount - 1), "random", 1, low)) return false;
        if (!checkInteger(vm, vm->peek(argCount - 2), "random", 2, up)) return false;
    } else {
        vm->runtimeError("wrong number of arguments");
        return false;
    }

    if (low > up) {
        vm->runtimeError("bad argument #1 to 'random' (interval is empty)");
        return false;
    }

    for (int i = 0; i < argCount; i++) vm->pop();
    uint64_t n = static_cast<uint64_t>(up) - static_cast<uint64_t>(low);
    uint64_t p = project(rv, n, state);
    int64_t res = static_cast<int64_t>(p + static_cast<uint64_t>(low));
    vm->push(vm->makeInteger(res));
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_math_randomseed(VM* vm, int argCount) {
    uint64_t* state = vm->rngState();
    uint64_t n1 = 0, n2 = 0;
    if (argCount == 0) {
        uint64_t seed1 = static_cast<uint64_t>(time(nullptr));
        uint64_t seed2 = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(vm));
        n1 = seed1;
        n2 = seed2;
    } else {
        int64_t val1 = 0;
        if (!checkInteger(vm, vm->peek(argCount - 1), "randomseed", 1, val1)) return false;
        n1 = static_cast<uint64_t>(val1);
        if (argCount >= 2) {
            int64_t val2 = 0;
            if (!checkInteger(vm, vm->peek(argCount - 2), "randomseed", 2, val2)) return false;
            n2 = static_cast<uint64_t>(val2);
        } else {
            n2 = 0;
        }
    }
    setseed(state, n1, n2);
    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(vm->makeInteger(static_cast<int64_t>(n1)));
    vm->push(vm->makeInteger(static_cast<int64_t>(n2)));
    vm->currentCoroutine()->lastResultCount = 2;
    return true;
}

bool native_math_type(VM* vm, int argCount) {
    if (argCount < 1) { vm->runtimeError("math.type expects 1 argument"); return false; }
    Value val = vm->peek(argCount - 1);
    for (int i = 0; i < argCount; i++) vm->pop();
    if (val.isInteger()) {
        vm->push(Value::runtimeString(vm->internString("integer")));
    } else if (val.isNumber()) {
        vm->push(Value::runtimeString(vm->internString("float")));
    } else {
        vm->push(Value::nil());
    }
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_math_tointeger(VM* vm, int argCount) {
    if (argCount < 1) { vm->runtimeError("math.tointeger expects 1 argument"); return false; }
    Value val = vm->peek(argCount - 1);
    for (int i = 0; i < argCount; i++) vm->pop();
    int64_t i = 0;
    if (vm->toInteger(val, i)) {
        vm->push(vm->makeInteger(i));
    } else {
        vm->push(Value::nil());
    }
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_math_ult(VM* vm, int argCount) {
    if (argCount < 2) { vm->runtimeError("math.ult expects 2 arguments"); return false; }
    Value v1 = vm->peek(argCount - 1);
    Value v2 = vm->peek(argCount - 2);
    int64_t m = 0, n = 0;
    if (!vm->toInteger(v1, m) || !vm->toInteger(v2, n)) {
        vm->runtimeError("number has no integer representation");
        return false;
    }
    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(Value::boolean(static_cast<uint64_t>(m) < static_cast<uint64_t>(n)));
    vm->currentCoroutine()->lastResultCount = 1;
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
    vm->addNativeToTable(mathTable, "frexp", native_math_frexp);
    vm->addNativeToTable(mathTable, "ldexp", native_math_ldexp);

    // Add math.pi constant
    mathTable->set("pi", Value::number(M_PI));

    // Add math.huge constant
    mathTable->set("huge", Value::number(HUGE_VAL));

    // Add math.maxinteger and math.mininteger
    mathTable->set("maxinteger", vm->makeInteger(std::numeric_limits<int64_t>::max()));
    mathTable->set("mininteger", vm->makeInteger(std::numeric_limits<int64_t>::min()));
}
