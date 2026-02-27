#include "vm/vm.hpp"
#include "compiler/chunk.hpp"
#include "value/table.hpp"
#include "value/string.hpp"
#include <string>
#include <vector>
#include <algorithm>

namespace {

bool native_table_insert(VM* vm, int argCount) {
    if (argCount < 2 || argCount > 3) {
        vm->runtimeError("table.insert expects 2 or 3 arguments");
        return false;
    }

    Value valueVal;
    Value posVal;
    Value tableVal;

    if (argCount == 2) {
        // table.insert(t, value)
        valueVal = vm->peek(0);
        tableVal = vm->peek(1);
        posVal = Value::nil();
    } else {
        // table.insert(t, pos, value)
        valueVal = vm->peek(0);
        posVal = vm->peek(1);
        tableVal = vm->peek(2);
    }

    if (!tableVal.isTable()) {
        vm->runtimeError("table.insert expects table as first argument");
        return false;
    }

    TableObject* table = vm->getTable(tableVal.asTableIndex());

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

        for (int i = n; i >= pos; i--) {
            table->set(Value::number(i + 1), table->get(Value::number(i)));
        }
        table->set(Value::number(pos), valueVal);
    }

    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(Value::nil());
    return true;
}

bool native_table_remove(VM* vm, int argCount) {
    if (argCount < 1 || argCount > 2) {
        vm->runtimeError("table.remove expects 1 or 2 arguments");
        return false;
    }

    Value posVal = (argCount == 2) ? vm->peek(0) : Value::nil();
    Value tableVal = vm->peek(argCount - 1);

    if (!tableVal.isTable()) {
        vm->runtimeError("table.remove expects table as first argument");
        return false;
    }

    TableObject* table = vm->getTable(tableVal.asTableIndex());

    int n = 1;
    while (!table->get(Value::number(n)).isNil()) {
        n++;
    }
    n--;

    int pos = posVal.isNil() ? n : static_cast<int>(posVal.asNumber());

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
    return true;
}

bool native_table_concat(VM* vm, int argCount) {
    if (argCount < 1 || argCount > 2) {
        vm->runtimeError("table.concat expects 1 or 2 arguments");
        return false;
    }

    Value sepVal = (argCount == 2) ? vm->peek(0) : Value::nil();
    Value tableVal = vm->peek(argCount - 1);

    if (!tableVal.isTable()) {
        vm->runtimeError("table.concat expects table as first argument");
        return false;
    }

    TableObject* table = vm->getTable(tableVal.asTableIndex());
    std::string sep = sepVal.isNil() ? "" : vm->getStringValue(sepVal);

    std::string result;
    int i = 1;
    while (true) {
        Value val = table->get(Value::number(i));
        if (val.isNil()) break;

        if (i > 1) result += sep;
        result += vm->getStringValue(val);
        i++;
    }

    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(Value::runtimeString(vm->internString(result)));
    return true;
}

bool native_table_pack(VM* vm, int argCount) {
    size_t tableIdx = vm->createTable();
    TableObject* table = vm->getTable(tableIdx);
    
    for (int i = 1; i <= argCount; i++) {
        Value v = vm->peek(argCount - i);
        table->set(Value::number(i), v);
    }
    
    size_t nIdx = vm->internString("n");
    table->set(Value::runtimeString(nIdx), Value::number(argCount));
    
    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(Value::table(tableIdx));
    return true;
}

bool native_table_unpack(VM* vm, int argCount) {
    if (argCount < 1 || argCount > 3) {
        vm->runtimeError("table.unpack expects 1 to 3 arguments");
        return false;
    }
    
    Value endVal = (argCount >= 3) ? vm->peek(0) : Value::nil();
    Value startVal = (argCount >= 2) ? vm->peek(argCount - 2) : Value::number(1);
    Value tableVal = vm->peek(argCount - 1);
    
    if (!tableVal.isTable()) {
        vm->runtimeError("table.unpack expects table as first argument");
        return false;
    }
    
    TableObject* table = vm->getTable(tableVal.asTableIndex());
    
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
    if (argCount < 1 || argCount > 2) {
        vm->runtimeError("table.sort expects 1 or 2 arguments");
        return false;
    }

    Value compVal = (argCount == 2) ? vm->peek(0) : Value::nil();
    Value tableVal = vm->peek(argCount - 1);

    if (!tableVal.isTable()) {
        vm->runtimeError("table.sort expects table as first argument");
        return false;
    }

    TableObject* table = vm->getTable(tableVal.asTableIndex());

    int n = 0;
    while (!table->get(Value::number(n + 1)).isNil()) {
        n++;
    }

    if (n <= 1) {
        for(int i=0; i<argCount; i++) vm->pop();
        vm->push(Value::nil());
        return true;
    }

    std::vector<Value> elements;
    elements.reserve(n);
    for (int i = 1; i <= n; i++) {
        elements.push_back(table->get(Value::number(i)));
    }

    bool sortError = false;
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
            if (!vm->callValue(2, 1)) {
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
}
