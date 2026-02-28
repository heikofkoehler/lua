#include "vm/vm.hpp"
#include "value/file.hpp"
#include "value/table.hpp"
#include "value/string.hpp"
#include <iostream>

namespace {

bool native_io_open(VM* vm, int argCount) {
    if (argCount < 1 || argCount > 2) {
        vm->runtimeError("io.open expects 1 or 2 arguments");
        return false;
    }
    
    std::string mode = "r";
    if (argCount == 2) {
        Value modeVal = vm->peek(0);
        mode = vm->getStringValue(modeVal);
    }
    Value filenameVal = vm->peek(argCount - 1);
    std::string filename = vm->getStringValue(filenameVal);
    
    FileObject* file = vm->openFile(filename, mode);
    
    for(int i=0; i<argCount; i++) vm->pop();

    if (file->isOpen()) {
        vm->push(Value::file(file));
    } else {
        vm->push(Value::nil());
        vm->push(Value::runtimeString(vm->internString("could not open file")));
    }
    return true;
}

bool native_io_write(VM* vm, int argCount) {
    FileObject* file = nullptr;
    int startArg = 0;
    
    if (argCount > 0) {
        Value firstArg = vm->peek(argCount - 1);
        if (firstArg.isFile()) {
            file = firstArg.asFileObj();
            startArg = 1;
        }
    }
    
    if (!file) {
        for (int i = startArg; i < argCount; i++) {
            Value val = vm->peek(argCount - 1 - i);
            std::cout << vm->getStringValue(val);
        }
    } else {
        for (int i = startArg; i < argCount; i++) {
            Value val = vm->peek(argCount - 1 - i);
            file->write(vm->getStringValue(val));
        }
    }
    
    for(int i=0; i<argCount; i++) vm->pop();
    
    vm->push(Value::boolean(true));
    return true;
}

bool native_io_read(VM* vm, int argCount) {
    FileObject* file = nullptr;
    if (argCount > 0) {
        Value firstArg = vm->peek(argCount - 1);
        if (firstArg.isFile()) {
            file = firstArg.asFileObj();
        }
    }
    
    std::string line;
    bool hasLine = false;
    
    if (!file) {
        if (std::getline(std::cin, line)) {
            hasLine = true;
        }
    } else {
        line = file->readLine();
        if (!(line.empty() && file->isEOF())) {
            hasLine = true;
        }
    }
    
    for(int i=0; i<argCount; i++) vm->pop();

    if (hasLine) {
        vm->push(Value::runtimeString(vm->internString(line)));
    } else {
        vm->push(Value::nil());
    }
    
    return true;
}

bool native_io_close(VM* vm, int argCount) {
    FileObject* file = nullptr;
    if (argCount > 0) {
        Value fileVal = vm->peek(argCount - 1);
        if (fileVal.isFile()) {
            file = fileVal.asFileObj();
        }
    }
    
    for(int i=0; i<argCount; i++) vm->pop();

    if (file) {
        vm->closeFile(file);
    }
    
    vm->push(Value::boolean(true));
    return true;
}

} // anonymous namespace

void registerIOLibrary(VM* vm, TableObject* ioTable) {
    vm->addNativeToTable(ioTable, "open", native_io_open);
    vm->addNativeToTable(ioTable, "write", native_io_write);
    vm->addNativeToTable(ioTable, "read", native_io_read);
    vm->addNativeToTable(ioTable, "close", native_io_close);
    
    // Register as globals too for backward compatibility
    vm->globals()["io_open"] = ioTable->get("open");
    vm->globals()["io_write"] = ioTable->get("write");
    vm->globals()["io_read"] = ioTable->get("read");
    vm->globals()["io_close"] = ioTable->get("close");

    ioTable->set("stderr", Value::nil());
    ioTable->set("stdout", Value::nil());
    ioTable->set("stdin", Value::nil());
}
