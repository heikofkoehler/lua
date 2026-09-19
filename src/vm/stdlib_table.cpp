#include "vm/vm.hpp"
#include "compiler/chunk.hpp"
#include "value/table.hpp"
#include "value/string.hpp"
#include <string>
#include <vector>
#include <algorithm>

namespace {

bool native_table_insert(VM* vm, int argCount) {
    if (argCount < 2) {
        vm->runtimeError("table.insert expects at least 2 arguments");
        return false;
    }

    Value valueVal;
    Value posVal;
    Value tableVal;

    if (argCount == 2) {
        // table.insert(t, value)
        tableVal = vm->peek(argCount - 1);
        valueVal = vm->peek(argCount - 2);
        posVal = Value::nil();
    } else {
        // table.insert(t, pos, value)
        tableVal = vm->peek(argCount - 1);
        posVal = vm->peek(argCount - 2);
        valueVal = vm->peek(argCount - 3);
    }

    if (!tableVal.isTable()) {
        vm->runtimeError("table.insert expects table as first argument");
        return false;
    }

    TableObject* table = tableVal.asTableObj();

    if (posVal.isNil()) {
        int n = 1;
        while (!table->get(Value::number(n)).isNil()) {
            n++;
        }
        table->set(Value::number(n), valueVal);
    } else {
        if (!posVal.isNumber()) {
            vm->runtimeError("table.insert position must be a number");
            return false;
        }
        int pos = static_cast<int>(posVal.asNumber());

        int n = 1;
        while (!table->get(Value::number(n)).isNil()) {
            n++;
        }

        if (pos < 1 || pos > n) {
            vm->runtimeError("bad argument #2 to 'insert' (position out of bounds)");
            return false;
        }

        for (int i = n; i >= pos; i--) {
            table->set(Value::number(i + 1), table->get(Value::number(i)));
        }
        table->set(Value::number(pos), valueVal);
    }

    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(Value::nil());
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_table_remove(VM* vm, int argCount) {
    if (argCount < 1) {
        vm->runtimeError("table.remove expects at least 1 argument");
        return false;
    }

    Value posVal = (argCount >= 2) ? vm->peek(argCount - 2) : Value::nil();
    Value tableVal = vm->peek(argCount - 1);

    if (!tableVal.isTable()) {
        vm->runtimeError("table.remove expects table as first argument");
        return false;
    }

    TableObject* table = tableVal.asTableObj();

    int n = 1;
    while (!table->get(Value::number(n)).isNil()) {
        n++;
    }
    n--;

    int pos = posVal.isNil() ? n : static_cast<int>(posVal.asNumber());

    if (pos > n + 1 || (pos < 1 && n > 0)) {
        vm->runtimeError("bad argument #2 to 'remove' (position out of bounds)");
        return false;
    }

    Value removed = Value::nil();
    if (pos >= 1 && pos <= n) {
        removed = table->get(Value::number(pos));
        for (int i = pos; i < n; i++) {
            table->set(Value::number(i), table->get(Value::number(i + 1)));
        }
        table->set(Value::number(n), Value::nil());
    }

    for (int i = 0; i < argCount; i++) vm->pop();
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
    TableObject* table = tableVal.asTableObj();

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
            startIdx = startVal.isInteger() ? startVal.asInteger() : static_cast<int64_t>(startVal.asNumber());
        }
    }

    int64_t endIdx = static_cast<int64_t>(table->length());
    if (argCount >= 4) {
        Value endVal = vm->peek(argCount - 4);
        if (!endVal.isNil()) {
            if (!endVal.isInteger() && !endVal.isNumber()) {
                vm->runtimeError("bad argument #4 to 'concat' (number expected)");
                return false;
            }
            endIdx = endVal.isInteger() ? endVal.asInteger() : static_cast<int64_t>(endVal.asNumber());
        }
    }

    std::string result;
    if (startIdx <= endIdx) {
        int64_t i = startIdx;
        while (true) {
            Value val = table->get(vm->makeInteger(i));
            if (!val.isString() && !val.isNumber() && !val.isInteger()) {
                vm->runtimeError("invalid value (" + val.typeToString() + ") at index " + std::to_string(i) + " in table for 'concat'");
                return false;
            }
            if (i > startIdx) result += sep;
            result += vm->getStringValue(val);
            if (i == endIdx) break;
            i++;
        }
    }

    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(Value::runtimeString(vm->internString(result)));
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_table_pack(VM* vm, int argCount) {
    TableObject* table = vm->createTable();
    
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
        vm->runtimeError("table.unpack expects at least 1 argument");
        return false;
    }
    
    Value endVal = (argCount >= 3) ? vm->peek(argCount - 3) : Value::nil();
    Value startVal = (argCount >= 2) ? vm->peek(argCount - 2) : Value::number(1);
    Value tableVal = vm->peek(argCount - 1);
    
    if (!tableVal.isTable()) {
        vm->runtimeError("table.unpack expects table as first argument");
        return false;
    }
    
    TableObject* table = tableVal.asTableObj();
    
    int i = static_cast<int>(startVal.asNumber());
    int j;
    
    if (endVal.isNil()) {
        j = 0;
        while (!table->get(Value::number(j + 1)).isNil()) {
            j++;
        }
    } else {
        j = static_cast<int>(endVal.asNumber());
    }
    
    int64_t n = static_cast<int64_t>(j) - static_cast<int64_t>(i) + 1;
    if (n > 0 && vm->currentCoroutine()->stack.size() + static_cast<size_t>(n) > VM::STACK_LIMIT) {
        vm->runtimeError("too many results to unpack");
        return false;
    }

    std::vector<Value> results;
    for (int k = i; k <= j; k++) {
        results.push_back(table->get(Value::number(k)));
    }
    
    for (int k = 0; k < argCount; k++) vm->pop();
    
    for (const auto& res : results) {
        vm->push(res);
    }
    vm->currentCoroutine()->lastResultCount = results.size();
    
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

    TableObject* table = tableVal.asTableObj();

    int n = 0;
    while (!table->get(Value::number(n + 1)).isNil()) {
        n++;
    }

    if (n <= 1) {
        for(int i=0; i<argCount; i++) vm->pop();
        vm->push(Value::nil());
        vm->currentCoroutine()->lastResultCount = 1;
        return true;
    }

    std::vector<Value> elements;
    elements.reserve(n);
    for (int i = 1; i <= n; i++) {
        elements.push_back(table->get(Value::number(i)));
    }

    bool sortError = false;
    struct NonYieldableGuard {
        CoroutineObject* co;
        NonYieldableGuard(CoroutineObject* c) : co(c) { if (co) co->nonYieldableCount++; }
        ~NonYieldableGuard() { if (co) co->nonYieldableCount--; }
    } nyGuard(vm->currentCoroutine());

    std::sort(elements.begin(), elements.end(), [&](const Value& a, const Value& b) {
        if (sortError) return false;

        if (compVal.isNil()) {
            if (a.isNumber() && b.isNumber()) {
                return a.asNumber() < b.asNumber();
            } else if (a.isString() && b.isString()) {
                return vm->getStringValue(a) < vm->getStringValue(b);
            } else {
                vm->runtimeError("attempt to compare uncomparable types in table.sort");
                sortError = true;
                return false;
            }
        } else {
            vm->push(compVal);
            vm->push(a);
            vm->push(b);
            
            size_t prevFrames = vm->currentCoroutine()->frames.size();
            if (!vm->callValue(2, 2)) { // Expect 1 result (1+1=2)
                sortError = true;
                return false;
            }
            
            if (vm->currentCoroutine()->frames.size() > prevFrames) {
                if (!vm->run(prevFrames)) {
                    sortError = true;
                    return false;
                }
            }
            
            Value result = vm->pop();
            return result.isTruthy();
        }
    });

    if (sortError) return false;

    for (int i = 1; i <= n; i++) {
        table->set(Value::number(i), elements[i - 1]);
    }

    for(int i=0; i<argCount; i++) vm->pop();
    vm->push(Value::nil());
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_table_move(VM* vm, int argCount) {
    if (argCount < 4) {
        vm->runtimeError("table.move expects at least 4 arguments");
        return false;
    }
    
    Value a1Val = vm->peek(argCount - 1);
    int f = static_cast<int>(vm->peek(argCount - 2).asNumber());
    int e = static_cast<int>(vm->peek(argCount - 3).asNumber());
    int t = static_cast<int>(vm->peek(argCount - 4).asNumber());
    Value a2Val = (argCount >= 5) ? vm->peek(argCount - 5) : a1Val;
    
    if (!a1Val.isTable() || !a2Val.isTable()) {
        vm->runtimeError("table.move expects table arguments");
        return false;
    }
    
    TableObject* a1 = a1Val.asTableObj();
    TableObject* a2 = a2Val.asTableObj();
    
    if (e >= f) {
        int n = e - f + 1;
        if (t <= f || t > e) {
            // Forward move
            for (int i = 0; i < n; i++) {
                a2->set(Value::number(t + i), a1->get(Value::number(f + i)));
            }
        } else {
            // Backward move (overlap case)
            for (int i = n - 1; i >= 0; i--) {
                a2->set(Value::number(t + i), a1->get(Value::number(f + i)));
            }
        }
    }
    
    for(int i=0; i<argCount; i++) vm->pop();
    vm->push(a2Val);
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

} // anonymous namespace

void registerTableLibrary(VM* vm, TableObject* tableTable) {
    vm->addNativeToTable(tableTable, "insert", native_table_insert);
    vm->addNativeToTable(tableTable, "remove", native_table_remove);
    vm->addNativeToTable(tableTable, "concat", native_table_concat);
    vm->addNativeToTable(tableTable, "pack", native_table_pack);
    vm->addNativeToTable(tableTable, "unpack", native_table_unpack);
    vm->addNativeToTable(tableTable, "sort", native_table_sort);
    vm->addNativeToTable(tableTable, "move", native_table_move);
}
