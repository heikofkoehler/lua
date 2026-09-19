#include "vm/vm.hpp"
#include "compiler/chunk.hpp"
#include "value/table.hpp"
#include "value/string.hpp"
#include <string>
#include <vector>
#include <algorithm>
#include <limits>
#include <cmath>

namespace {

static Value table_get(VM* vm, const Value& tableVal, const Value& key) {
    if (tableVal.isTable()) {
        TableObject* table = tableVal.asTableObj();
        Value v = table->get(key);
        if (!v.isNil() || table->getMetatable().isNil()) {
            return v;
        }
    }
    Value indexMethod = vm->getMetamethod(tableVal, "__index");
    if (indexMethod.isNil()) {
        if (!tableVal.isTable()) {
            vm->runtimeError("attempt to index a " + tableVal.typeToString() + " value");
        }
        return Value::nil();
    }
    if (indexMethod.isFunction()) {
        vm->push(indexMethod);
        vm->push(tableVal);
        vm->push(key);
        size_t prevFrames = vm->currentCoroutine()->frames.size();
        if (!vm->callValue(2, 2)) return Value::nil();
        if (vm->currentCoroutine()->frames.size() > prevFrames) {
            if (!vm->run(prevFrames)) return Value::nil();
        }
        return vm->pop();
    } else if (indexMethod.isTable()) {
        return table_get(vm, indexMethod, key);
    }
    return Value::nil();
}

static bool table_set(VM* vm, const Value& tableVal, const Value& key, const Value& value) {
    if (tableVal.isTable()) {
        TableObject* table = tableVal.asTableObj();
        if (table->getMetatable().isNil() || table->has(key)) {
            table->set(key, value);
            return true;
        }
    }
    Value newIndex = vm->getMetamethod(tableVal, "__newindex");
    if (newIndex.isNil()) {
        if (tableVal.isTable()) {
            tableVal.asTableObj()->set(key, value);
            return true;
        }
        vm->runtimeError("attempt to index a " + tableVal.typeToString() + " value");
        return false;
    }
    if (newIndex.isFunction()) {
        vm->push(newIndex);
        vm->push(tableVal);
        vm->push(key);
        vm->push(value);
        size_t prevFrames = vm->currentCoroutine()->frames.size();
        if (!vm->callValue(3, 1)) return false;
        if (vm->currentCoroutine()->frames.size() > prevFrames) {
            if (!vm->run(prevFrames)) return false;
        }
        return true;
    } else if (newIndex.isTable()) {
        return table_set(vm, newIndex, key, value);
    }
    return false;
}

static bool get_table_length(VM* vm, const Value& tableVal, int64_t& outLen) {
    Value lenMeta = vm->getMetamethod(tableVal, "__len");
    if (!lenMeta.isNil()) {
        vm->push(lenMeta);
        vm->push(tableVal);
        size_t prevFrames = vm->currentCoroutine()->frames.size();
        if (!vm->callValue(1, 2)) return false;
        if (vm->currentCoroutine()->frames.size() > prevFrames) {
            if (!vm->run(prevFrames)) return false;
        }
        Value res = vm->pop();
        if (!res.isInteger() && !res.isNumber()) {
            vm->runtimeError("object length is not an integer");
            return false;
        }
        outLen = res.asInteger();
        return true;
    }
    if (tableVal.isTable()) {
        outLen = static_cast<int64_t>(tableVal.asTableObj()->length());
        return true;
    }
    vm->runtimeError("bad argument to length (table expected)");
    return false;
}

bool native_table_create(VM* vm, int argCount) {
    if (argCount < 1) {
        vm->runtimeError("bad argument #1 to 'create' (value expected)");
        return false;
    }
    Value v1 = vm->peek(argCount - 1);
    if (!v1.isInteger() && !v1.isNumber()) {
        vm->runtimeError("bad argument #1 to 'create' (number expected, got " + v1.typeToString() + ")");
        return false;
    }
    int64_t sizeseq = v1.asInteger();
    int64_t sizerest = 0;
    if (argCount >= 2) {
        Value v2 = vm->peek(argCount - 2);
        if (!v2.isNil()) {
            if (!v2.isInteger() && !v2.isNumber()) {
                vm->runtimeError("bad argument #2 to 'create' (number expected, got " + v2.typeToString() + ")");
                return false;
            }
            sizerest = v2.asInteger();
        }
    }

    if (sizeseq < 0 || sizeseq > INT32_MAX) {
        vm->runtimeError("bad argument #1 to 'create' (out of range)");
        return false;
    }
    if (sizerest < 0 || sizerest > INT32_MAX) {
        vm->runtimeError("bad argument #2 to 'create' (out of range)");
        return false;
    }
    if (sizeseq > (1 << 30) || sizerest > (1 << 30)) {
        vm->runtimeError("table overflow");
        return false;
    }

    TableObject* table = vm->createTable(static_cast<size_t>(sizeseq), static_cast<size_t>(sizerest));
    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(Value::table(table));
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_table_insert(VM* vm, int argCount) {
    if (argCount < 2 || argCount > 3) {
        vm->runtimeError("wrong number of arguments to 'insert'");
        return false;
    }

    Value tableVal = vm->peek(argCount - 1);
    if (!tableVal.isTable()) {
        vm->runtimeError("bad argument #1 to 'insert' (table expected, got " + tableVal.typeToString() + ")");
        return false;
    }

    int64_t len = 0;
    if (!get_table_length(vm, tableVal, len)) return false;

    int64_t e = len + 1;
    int64_t pos = e;
    Value valueVal;

    if (argCount == 2) {
        valueVal = vm->peek(argCount - 2);
    } else {
        Value posVal = vm->peek(argCount - 2);
        if (!posVal.isInteger() && !posVal.isNumber()) {
            vm->runtimeError("bad argument #2 to 'insert' (number expected, got " + posVal.typeToString() + ")");
            return false;
        }
        pos = posVal.asInteger();
        if (pos < 1 || pos > e) {
            vm->runtimeError("bad argument #2 to 'insert' (position out of bounds)");
            return false;
        }
        valueVal = vm->peek(argCount - 3);

        for (int64_t i = e; i > pos; i--) {
            Value prev = table_get(vm, tableVal, vm->makeInteger(i - 1));
            table_set(vm, tableVal, vm->makeInteger(i), prev);
        }
    }

    table_set(vm, tableVal, vm->makeInteger(pos), valueVal);

    for (int i = 0; i < argCount; i++) vm->pop();
    vm->currentCoroutine()->lastResultCount = 0;
    return true;
}

bool native_table_remove(VM* vm, int argCount) {
    if (argCount < 1 || argCount > 2) {
        vm->runtimeError("wrong number of arguments to 'remove'");
        return false;
    }

    Value tableVal = vm->peek(argCount - 1);
    if (!tableVal.isTable()) {
        vm->runtimeError("bad argument #1 to 'remove' (table expected, got " + tableVal.typeToString() + ")");
        return false;
    }

    int64_t len = 0;
    if (!get_table_length(vm, tableVal, len)) return false;

    int64_t pos = len;
    if (argCount == 2) {
        Value posVal = vm->peek(argCount - 2);
        if (!posVal.isNil()) {
            if (!posVal.isInteger() && !posVal.isNumber()) {
                vm->runtimeError("bad argument #2 to 'remove' (number expected, got " + posVal.typeToString() + ")");
                return false;
            }
            pos = posVal.asInteger();
        }
    }

    if (pos != len) {
        if (pos < 1 || pos > len + 1) {
            vm->runtimeError("bad argument #2 to 'remove' (position out of bounds)");
            return false;
        }
    }

    Value removed = table_get(vm, tableVal, vm->makeInteger(pos));
    int64_t i = pos;
    for (; i < len; i++) {
        Value nextVal = table_get(vm, tableVal, vm->makeInteger(i + 1));
        table_set(vm, tableVal, vm->makeInteger(i), nextVal);
    }
    table_set(vm, tableVal, vm->makeInteger(i), Value::nil());

    for (int k = 0; k < argCount; k++) vm->pop();
    vm->push(removed);
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_table_concat(VM* vm, int argCount) {
    if (argCount < 1) {
        vm->runtimeError("table.concat expects at least 1 argument");
        return false;
    }

    Value tableVal = vm->peek(argCount - 1);
    if (!tableVal.isTable()) {
        vm->runtimeError("bad argument #1 to 'concat' (table expected, got " + tableVal.typeToString() + ")");
        return false;
    }

    std::string sep = "";
    if (argCount >= 2) {
        Value sepVal = vm->peek(argCount - 2);
        if (!sepVal.isNil()) {
            sep = vm->getStringValue(sepVal);
        }
    }

    int64_t startIdx = 1;
    if (argCount >= 3) {
        Value startVal = vm->peek(argCount - 3);
        if (!startVal.isNil()) {
            if (!startVal.isInteger() && !startVal.isNumber()) {
                vm->runtimeError("bad argument #3 to 'concat' (number expected)");
                return false;
            }
            startIdx = startVal.asInteger();
        }
    }

    int64_t endIdx = 0;
    if (argCount >= 4) {
        Value endVal = vm->peek(argCount - 4);
        if (!endVal.isNil()) {
            if (!endVal.isInteger() && !endVal.isNumber()) {
                vm->runtimeError("bad argument #4 to 'concat' (number expected)");
                return false;
            }
            endIdx = endVal.asInteger();
        }
    } else {
        if (!get_table_length(vm, tableVal, endIdx)) return false;
    }

    std::string result;
    if (startIdx <= endIdx) {
        int64_t k = startIdx;
        while (true) {
            Value val = table_get(vm, tableVal, vm->makeInteger(k));
            if (!val.isString() && !val.isNumber() && !val.isInteger()) {
                vm->runtimeError("invalid value (" + val.typeToString() + ") at index " + std::to_string(k) + " in table for 'concat'");
                return false;
            }
            if (k > startIdx) result += sep;
            result += vm->getStringValue(val);
            if (k == endIdx) break;
            k++;
        }
    }

    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(Value::runtimeString(vm->internString(result)));
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_table_pack(VM* vm, int argCount) {
    TableObject* table = vm->createTable(static_cast<size_t>(argCount), 1);
    
    for (int i = 1; i <= argCount; i++) {
        Value v = vm->peek(argCount - i);
        table->set(Value::number(i), v);
    }
    
    table->set("n", Value::number(argCount));
    
    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(Value::table(table));
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_table_unpack(VM* vm, int argCount) {
    if (argCount < 1) {
        vm->runtimeError("bad argument #1 to 'unpack' (value expected)");
        return false;
    }
    
    Value tableVal = vm->peek(argCount - 1);
    int64_t i = (argCount >= 2) ? vm->peek(argCount - 2).asInteger() : 1;
    int64_t j;
    
    if (argCount >= 3 && !vm->peek(argCount - 3).isNil()) {
        j = vm->peek(argCount - 3).asInteger();
    } else {
        int64_t len = 0;
        if (!get_table_length(vm, tableVal, len)) return false;
        j = len;
    }
    
    if (i > j) {
        for (int k = 0; k < argCount; k++) vm->pop();
        vm->currentCoroutine()->lastResultCount = 0;
        return true;
    }
    
    uint64_t n = static_cast<uint64_t>(j) - static_cast<uint64_t>(i);
    if (n >= static_cast<uint64_t>(INT32_MAX) || n >= 1000000) {
        vm->runtimeError("too many results to unpack");
        return false;
    }

    size_t count = static_cast<size_t>(n + 1);
    std::vector<Value> results;
    results.reserve(count);
    for (int64_t k = i; k <= j; k++) {
        results.push_back(table_get(vm, tableVal, vm->makeInteger(k)));
    }
    
    for (int k = 0; k < argCount; k++) vm->pop();
    
    for (const auto& res : results) {
        vm->push(res);
    }
    vm->currentCoroutine()->lastResultCount = results.size();
    return true;
}

static bool table_less(VM* vm, const Value& a, const Value& b, bool& errorFlag) {
    if (a.isInteger() && b.isInteger()) {
        return a.asInteger() < b.asInteger();
    }
    if (a.isNumber() && b.isNumber()) {
        return a.asNumber() < b.asNumber();
    }
    if ((a.isString() || a.isRuntimeString()) && (b.isString() || b.isRuntimeString())) {
        return vm->getStringValue(a) < vm->getStringValue(b);
    }
    size_t prevFrames = vm->currentCoroutine()->frames.size();
    if (!vm->callBinaryMetamethod(a, b, "__lt")) {
        vm->runtimeError("attempt to compare " + a.typeToString() + " and " + b.typeToString());
        errorFlag = true;
        return false;
    }
    if (vm->currentCoroutine()->frames.size() > prevFrames) {
        if (!vm->run(prevFrames)) {
            errorFlag = true;
            return false;
        }
    }
    Value res = vm->pop();
    return res.isTruthy();
}

static bool sort_comp(VM* vm, const Value& compVal, const Value& a, const Value& b, bool& errorFlag) {
    if (compVal.isNil()) {
        return table_less(vm, a, b, errorFlag);
    }

    vm->push(compVal);
    vm->push(a);
    vm->push(b);
    
    size_t prevFrames = vm->currentCoroutine()->frames.size();
    if (!vm->callValue(2, 2)) {
        errorFlag = true;
        return false;
    }
    
    if (vm->currentCoroutine()->frames.size() > prevFrames) {
        if (!vm->run(prevFrames)) {
            errorFlag = true;
            return false;
        }
    }
    
    Value result = vm->pop();
    return result.isTruthy();
}

static bool sort_partition(VM* vm, const Value& compVal, std::vector<Value>& arr, int64_t lo, int64_t up, int64_t& outP, bool& errorFlag) {
    int64_t i = lo;
    int64_t j = up - 1;
    Value P = arr[static_cast<size_t>(up - 1)]; // Pivot
    for (;;) {
        while (true) {
            i++;
            bool cmp = sort_comp(vm, compVal, arr[static_cast<size_t>(i)], P, errorFlag);
            if (errorFlag) return false;
            if (!cmp) break;
            if (i == up - 1) {
                vm->runtimeError("invalid order function for sorting");
                errorFlag = true;
                return false;
            }
        }
        while (true) {
            j--;
            bool cmp = sort_comp(vm, compVal, P, arr[static_cast<size_t>(j)], errorFlag);
            if (errorFlag) return false;
            if (!cmp) break;
            if (j < i) {
                vm->runtimeError("invalid order function for sorting");
                errorFlag = true;
                return false;
            }
        }
        if (j < i) {
            std::swap(arr[static_cast<size_t>(up - 1)], arr[static_cast<size_t>(i)]);
            outP = i;
            return true;
        }
        std::swap(arr[static_cast<size_t>(i)], arr[static_cast<size_t>(j)]);
    }
}

static bool auxsort(VM* vm, const Value& compVal, std::vector<Value>& arr, int64_t lo, int64_t up, unsigned int& rnd, bool& errorFlag) {
    while (lo < up) {
        if (errorFlag) return false;
        if (sort_comp(vm, compVal, arr[static_cast<size_t>(up)], arr[static_cast<size_t>(lo)], errorFlag)) {
            if (errorFlag) return false;
            std::swap(arr[static_cast<size_t>(lo)], arr[static_cast<size_t>(up)]);
        }
        if (errorFlag) return false;
        if (up - lo == 1) return true; // 2 elements

        int64_t p;
        if (up - lo < 100 || rnd == 0) {
            p = (lo + up) / 2;
        } else {
            rnd = rnd * 1103515245 + 12345;
            int64_t r4 = (up - lo) / 4;
            p = (rnd % (r4 * 2)) + (lo + r4);
        }

        if (sort_comp(vm, compVal, arr[static_cast<size_t>(p)], arr[static_cast<size_t>(lo)], errorFlag)) {
            if (errorFlag) return false;
            std::swap(arr[static_cast<size_t>(p)], arr[static_cast<size_t>(lo)]);
        } else {
            if (sort_comp(vm, compVal, arr[static_cast<size_t>(up)], arr[static_cast<size_t>(p)], errorFlag)) {
                if (errorFlag) return false;
                std::swap(arr[static_cast<size_t>(p)], arr[static_cast<size_t>(up)]);
            }
        }
        if (errorFlag) return false;
        if (up - lo == 2) return true; // 3 elements

        std::swap(arr[static_cast<size_t>(p)], arr[static_cast<size_t>(up - 1)]);

        int64_t pivotPos;
        if (!sort_partition(vm, compVal, arr, lo, up, pivotPos, errorFlag)) return false;

        p = pivotPos;
        int64_t n;
        if (p - lo < up - p) {
            if (!auxsort(vm, compVal, arr, lo, p - 1, rnd, errorFlag)) return false;
            n = p - lo;
            lo = p + 1;
        } else {
            if (!auxsort(vm, compVal, arr, p + 1, up, rnd, errorFlag)) return false;
            n = up - p;
            up = p - 1;
        }
        if ((up - lo) / 128 > n) {
            rnd = rnd * 1103515245 + 12345;
        }
    }
    return true;
}

bool native_table_sort(VM* vm, int argCount) {
    if (argCount < 1) {
        vm->runtimeError("table.sort expects at least 1 argument");
        return false;
    }

    Value compVal = (argCount >= 2) ? vm->peek(argCount - 2) : Value::nil();
    Value tableVal = vm->peek(argCount - 1);

    if (!tableVal.isTable()) {
        vm->runtimeError("table.sort expects table as first argument");
        return false;
    }

    int64_t n = 0;
    if (!get_table_length(vm, tableVal, n)) return false;

    if (n > INT32_MAX) {
        vm->runtimeError("array to sort too big");
        return false;
    }

    if (n <= 1) {
        for (int i = 0; i < argCount; i++) vm->pop();
        vm->currentCoroutine()->lastResultCount = 0;
        return true;
    }

    std::vector<Value> elements;
    elements.reserve(static_cast<size_t>(n));
    for (int64_t i = 1; i <= n; i++) {
        elements.push_back(table_get(vm, tableVal, vm->makeInteger(i)));
    }

    bool errorFlag = false;
    unsigned int rnd = 0x12345678;

    struct NonYieldableGuard {
        CoroutineObject* co;
        NonYieldableGuard(CoroutineObject* c) : co(c) { if (co) co->nonYieldableCount++; }
        ~NonYieldableGuard() { if (co) co->nonYieldableCount--; }
    } nyGuard(vm->currentCoroutine());

    if (!auxsort(vm, compVal, elements, 0, n - 1, rnd, errorFlag)) {
        return false;
    }

    for (int64_t i = 1; i <= n; i++) {
        table_set(vm, tableVal, vm->makeInteger(i), elements[static_cast<size_t>(i - 1)]);
    }

    for (int i = 0; i < argCount; i++) vm->pop();
    vm->currentCoroutine()->lastResultCount = 0;
    return true;
}

bool native_table_move(VM* vm, int argCount) {
    if (argCount < 4) {
        vm->runtimeError("table.move expects at least 4 arguments");
        return false;
    }

    Value a1Val = vm->peek(argCount - 1);
    Value fVal = vm->peek(argCount - 2);
    Value eVal = vm->peek(argCount - 3);
    Value tVal = vm->peek(argCount - 4);
    Value a2Val = (argCount >= 5) ? vm->peek(argCount - 5) : a1Val;

    if (!a1Val.isTable()) {
        vm->runtimeError("bad argument #1 to 'move' (table expected)");
        return false;
    }
    if (!a2Val.isTable()) {
        vm->runtimeError("bad argument #5 to 'move' (table expected)");
        return false;
    }
    if (!fVal.isInteger() && !fVal.isNumber()) {
        vm->runtimeError("bad argument #2 to 'move' (number expected)");
        return false;
    }
    if (!eVal.isInteger() && !eVal.isNumber()) {
        vm->runtimeError("bad argument #3 to 'move' (number expected)");
        return false;
    }
    if (!tVal.isInteger() && !tVal.isNumber()) {
        vm->runtimeError("bad argument #4 to 'move' (number expected)");
        return false;
    }

    int64_t f = fVal.asInteger();
    int64_t e = eVal.asInteger();
    int64_t t = tVal.asInteger();

    if (e >= f) {
        if (!(f > 0 || (static_cast<uint64_t>(e) - static_cast<uint64_t>(f) < static_cast<uint64_t>(std::numeric_limits<int64_t>::max())))) {
            vm->runtimeError("too many elements to move");
            return false;
        }
        int64_t n = e - f + 1;
        if (t > std::numeric_limits<int64_t>::max() - n + 1) {
            vm->runtimeError("destination wrap around");
            return false;
        }
        if (t > e || t <= f) {
            // Forward move
            for (int64_t i = 0; i < n; i++) {
                Value val = table_get(vm, a1Val, vm->makeInteger(f + i));
                table_set(vm, a2Val, vm->makeInteger(t + i), val);
            }
        } else {
            // Backward move
            for (int64_t i = n - 1; i >= 0; i--) {
                Value val = table_get(vm, a1Val, vm->makeInteger(f + i));
                table_set(vm, a2Val, vm->makeInteger(t + i), val);
            }
        }
    }

    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(a2Val);
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

} // anonymous namespace

void registerTableLibrary(VM* vm, TableObject* tableTable) {
    vm->addNativeToTable(tableTable, "create", native_table_create);
    vm->addNativeToTable(tableTable, "insert", native_table_insert);
    vm->addNativeToTable(tableTable, "remove", native_table_remove);
    vm->addNativeToTable(tableTable, "concat", native_table_concat);
    vm->addNativeToTable(tableTable, "pack", native_table_pack);
    vm->addNativeToTable(tableTable, "unpack", native_table_unpack);
    vm->addNativeToTable(tableTable, "sort", native_table_sort);
    vm->addNativeToTable(tableTable, "move", native_table_move);
}
