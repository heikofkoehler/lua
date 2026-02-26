#include "vm/vm.hpp"
#include "compiler/chunk.hpp"
#include "value/table.hpp"
#include "value/string.hpp"
#include <string>

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
        // table.insert(t, value) - append to end
        valueVal = vm->pop();
        tableVal = vm->pop();
        posVal = Value::nil();
    } else {
        // table.insert(t, pos, value) - insert at position
        valueVal = vm->pop();
        posVal = vm->pop();
        tableVal = vm->pop();
    }

    if (!tableVal.isTable()) {
        vm->runtimeError("table.insert expects table as first argument");
        return false;
    }

    TableObject* table = vm->getTable(tableVal.asTableIndex());

    if (posVal.isNil()) {
        // Append: find next numeric index
        int n = 1;
        while (!table->get(Value::number(n)).isNil()) {
            n++;
        }
        table->set(Value::number(n), valueVal);
    } else {
        // Insert at position: shift elements
        if (!posVal.isNumber()) {
            vm->runtimeError("table.insert position must be a number");
            return false;
        }
        int pos = static_cast<int>(posVal.asNumber());

        // Find table length
        int n = 1;
        while (!table->get(Value::number(n)).isNil()) {
            n++;
        }

        // Shift elements from pos to end
        for (int i = n; i >= pos; i--) {
            table->set(Value::number(i + 1), table->get(Value::number(i)));
        }

        // Insert new value
        table->set(Value::number(pos), valueVal);
    }

    vm->push(Value::nil());  // table.insert returns nothing
    return true;
}

bool native_table_remove(VM* vm, int argCount) {
    if (argCount < 1 || argCount > 2) {
        vm->runtimeError("table.remove expects 1 or 2 arguments");
        return false;
    }

    Value posVal = (argCount == 2) ? vm->pop() : Value::nil();
    Value tableVal = vm->pop();

    if (!tableVal.isTable()) {
        vm->runtimeError("table.remove expects table as first argument");
        return false;
    }

    TableObject* table = vm->getTable(tableVal.asTableIndex());

    // Find table length
    int n = 1;
    while (!table->get(Value::number(n)).isNil()) {
        n++;
    }
    n--;  // Length is one less than first nil index

    int pos = posVal.isNil() ? n : static_cast<int>(posVal.asNumber());

    if (pos < 1 || pos > n) {
        vm->push(Value::nil());
        return true;
    }

    Value removed = table->get(Value::number(pos));

    // Shift elements down
    for (int i = pos; i < n; i++) {
        table->set(Value::number(i), table->get(Value::number(i + 1)));
    }

    // Remove last element
    table->set(Value::number(n), Value::nil());

    vm->push(removed);
    return true;
}

bool native_table_concat(VM* vm, int argCount) {
    if (argCount < 1 || argCount > 2) {
        vm->runtimeError("table.concat expects 1 or 2 arguments");
        return false;
    }

    Value sepVal;
    if (argCount == 2) {
        sepVal = vm->pop();
    } else {
        // Use empty string from chunk's string pool
        Chunk* chunk = const_cast<Chunk*>(vm->rootChunk());
        size_t emptyIdx = chunk->addString("");
        sepVal = Value::string(emptyIdx);
    }
    Value tableVal = vm->pop();

    if (!tableVal.isTable()) {
        vm->runtimeError("table.concat expects table as first argument");
        return false;
    }

    TableObject* table = vm->getTable(tableVal.asTableIndex());

    StringObject* sepStr = nullptr;
    if (sepVal.isRuntimeString()) {
        sepStr = vm->getString(sepVal.asStringIndex());
    } else if (sepVal.isString()) {
        sepStr = vm->rootChunk()->getString(sepVal.asStringIndex());
    }

    std::string result;
    int i = 1;
    while (true) {
        Value val = table->get(Value::number(i));
        if (val.isNil()) break;

        if (i > 1 && sepStr) {
            result.append(sepStr->chars(), sepStr->length());
        }

        // Convert value to string
        if (val.isRuntimeString()) {
            StringObject* str = vm->getString(val.asStringIndex());
            result.append(str->chars(), str->length());
        } else if (val.isString()) {
            StringObject* str = vm->rootChunk()->getString(val.asStringIndex());
            result.append(str->chars(), str->length());
        } else if (val.isNumber()) {
            result.append(std::to_string(val.asNumber()));
        } else {
            result.append(val.toString());
        }

        i++;
    }

    vm->push(Value::runtimeString(vm->internString(result)));
    return true;
}

bool native_table_pack(VM* vm, int argCount) {
    size_t tableIdx = vm->createTable();
    TableObject* table = vm->getTable(tableIdx);
    
    // Arguments are on stack in order: [..., arg1, arg2, ..., argN]
    // Peek and set them into the table
    for (int i = 1; i <= argCount; i++) {
        Value v = vm->peek(argCount - i);
        table->set(Value::number(i), v);
    }
    
    // Set field "n"
    size_t nIdx = vm->internString("n");
    table->set(Value::runtimeString(nIdx), Value::number(argCount));
    
    // Pop all arguments
    for (int i = 0; i < argCount; i++) vm->pop();
    
    vm->push(Value::table(tableIdx));
    return true;
}

bool native_table_unpack(VM* vm, int argCount) {
    if (argCount < 1 || argCount > 3) {
        vm->runtimeError("table.unpack expects 1 to 3 arguments");
        return false;
    }
    
    Value endVal = (argCount >= 3) ? vm->pop() : Value::nil();
    Value startVal = (argCount >= 2) ? vm->pop() : Value::number(1);
    Value tableVal = vm->pop();
    
    if (!tableVal.isTable()) {
        vm->runtimeError("table.unpack expects table as first argument");
        return false;
    }
    
    TableObject* table = vm->getTable(tableVal.asTableIndex());
    
    int i = static_cast<int>(startVal.asNumber());
    int j;
    
    if (endVal.isNil()) {
        // Find length of table (numeric sequence)
        j = 0;
        while (!table->get(Value::number(j + 1)).isNil()) {
            j++;
        }
    } else {
        j = static_cast<int>(endVal.asNumber());
    }
    
    // Push values onto stack
    int count = 0;
    for (int k = i; k <= j; k++) {
        vm->push(table->get(Value::number(k)));
        count++;
    }
    vm->currentCoroutine()->lastResultCount = count;
    
    return true;
}

bool native_table_sort(VM* vm, int argCount) {
    if (argCount < 1 || argCount > 2) {
        vm->runtimeError("table.sort expects 1 or 2 arguments");
        return false;
    }

    Value compVal = (argCount == 2) ? vm->pop() : Value::nil();
    Value tableVal = vm->pop();

    if (!tableVal.isTable()) {
        vm->runtimeError("table.sort expects table as first argument");
        return false;
    }

    TableObject* table = vm->getTable(tableVal.asTableIndex());

    // Find length of table
    int n = 0;
    while (!table->get(Value::number(n + 1)).isNil()) {
        n++;
    }

    if (n <= 1) {
        vm->push(Value::nil());
        return true; // Nothing to sort
    }

    // Extract elements into a vector
    std::vector<Value> elements;
    elements.reserve(n);
    for (int i = 1; i <= n; i++) {
        elements.push_back(table->get(Value::number(i)));
    }

    // Sort the vector
    bool sortError = false;
    std::sort(elements.begin(), elements.end(), [&](const Value& a, const Value& b) {
        if (sortError) return false;

        if (compVal.isNil()) {
            // Default comparison: a < b
            if (a.isNumber() && b.isNumber()) {
                return a.asNumber() < b.asNumber();
            } else if ((a.isString() || a.isRuntimeString()) && (b.isString() || b.isRuntimeString())) {
                return vm->getStringValue(a) < vm->getStringValue(b);
            } else {
                vm->runtimeError("attempt to compare uncomparable types in table.sort");
                sortError = true;
                return false;
            }
        } else {
            // Call custom comparison function
            vm->push(compVal);
            vm->push(a);
            vm->push(b);
            
            // Need to execute the function
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

    // Put elements back into table
    for (int i = 1; i <= n; i++) {
        table->set(Value::number(i), elements[i - 1]);
    }

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
