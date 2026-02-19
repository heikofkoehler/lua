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
    if (sepVal.isString()) {
        sepStr = vm->rootChunk()->getString(sepVal.asStringIndex());
    } else if (sepVal.isRuntimeString()) {
        sepStr = vm->getString(sepVal.asStringIndex());
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
        if (val.isString()) {
            StringObject* str = vm->rootChunk()->getString(val.asStringIndex());
            result.append(str->chars(), str->length());
        } else if (val.isRuntimeString()) {
            StringObject* str = vm->getString(val.asStringIndex());
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

} // anonymous namespace

void registerTableLibrary(VM* vm, TableObject* tableTable) {
    vm->addNativeToTable(tableTable, "insert", native_table_insert);
    vm->addNativeToTable(tableTable, "remove", native_table_remove);
    vm->addNativeToTable(tableTable, "concat", native_table_concat);
}
