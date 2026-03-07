#include "vm/vm.hpp"
#include "value/file.hpp"
#include "value/table.hpp"
#include "value/string.hpp"
#include <iostream>
#include <sstream>

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
        Value ioTableVal = vm->getGlobal("io");
        if (ioTableVal.isTable()) {
            Value out = ioTableVal.asTableObj()->get("stdout");
            if (out.isFile()) {
                file = out.asFileObj();
            }
        }
    }

    if (file) {
        for (int i = startArg; i < argCount; i++) {
            Value val = vm->peek(argCount - 1 - i);
            file->write(vm->getStringValue(val));
        }
    } else {
        // Fallback to std::cout if io.stdout is not available
        for (int i = startArg; i < argCount; i++) {
            Value val = vm->peek(argCount - 1 - i);
            std::cout << vm->getStringValue(val);
        }
    }
    
    for(int i=0; i<argCount; i++) vm->pop();
    
    vm->push(Value::boolean(true));
    return true;
}

static bool read_item(VM* vm, FileObject* file, std::string fmt, Value& res) {
    if (fmt == "a" || fmt == "*a") {
        std::string result = file ? file->readAll() : "";
        if (!file) {
            std::stringstream buffer;
            buffer << std::cin.rdbuf();
            result = buffer.str();
        }
        res = Value::runtimeString(vm->internString(result));
        return true;
    } else if (fmt == "l" || fmt == "*l" || fmt == "L" || fmt == "*L") {
        bool withNewline = (fmt == "L" || fmt == "*L");
        std::string result;
        if (!file) {
            if (!std::getline(std::cin, result)) return false;
            if (withNewline) result += "\n";
        } else {
            result = file->readLine();
            if (result.empty() && file->isEOF()) return false;
            if (!withNewline && !result.empty() && result.back() == '\n') result.pop_back();
        }
        res = Value::runtimeString(vm->internString(result));
        return true;
    } else if (fmt == "n" || fmt == "*n") {
        double d;
        if (file) {
            // Very basic numeric read for now
            std::string s = file->readAll(); // This is wrong, should peek/read only number
            // Re-implementing correctly requires a smarter FileObject
            return false; 
        } else {
            if (!(std::cin >> d)) return false;
            res = Value::number(d);
            return true;
        }
    } else if (std::isdigit(fmt[0])) {
        size_t len = std::stoul(fmt);
        std::string result = file ? file->read(len) : "";
        if (result.empty() && (file ? file->isEOF() : std::cin.eof())) return false;
        res = Value::runtimeString(vm->internString(result));
        return true;
    }
    return false;
}

bool native_io_read(VM* vm, int argCount) {
    FileObject* file = nullptr;
    int argStart = 0;
    if (argCount > 0) {
        Value firstArg = vm->peek(argCount - 1);
        if (firstArg.isFile()) {
            file = firstArg.asFileObj();
            argStart = 1;
        }
    }
    
    if (!file) {
        Value stdinVal = vm->getGlobal("io");
        if (stdinVal.isTable()) {
            Value in = stdinVal.asTableObj()->get("stdin");
            if (in.isFile()) file = in.asFileObj();
        }
    }

    int formatCount = argCount - argStart;
    if (formatCount == 0) formatCount = 1; // Default "l"

    int results = 0;
    for (int i = 0; i < formatCount; i++) {
        std::string fmt = "l";
        if (argCount > argStart) {
            // Adjust peek index by 'results' because we are pushing results onto the stack inside the loop
            Value v = vm->peek(argCount - 1 - (argStart + i) + results);
            if (v.isString()) fmt = vm->getStringValue(v);
            else if (v.isNumber()) fmt = std::to_string((int)v.asNumber());
        }
        
        Value res;
        if (read_item(vm, file, fmt, res)) {
            vm->push(res);
            results++;
        } else {
            vm->push(Value::nil());
            results++;
            break;
        }
    }
    
    // Cleanup args
    std::vector<Value> res_vals;
    for(int i=0; i<results; i++) res_vals.push_back(vm->pop());
    std::reverse(res_vals.begin(), res_vals.end());
    
    for(int i=0; i<argCount; i++) vm->pop();
    for(const auto& v : res_vals) vm->push(v);
    
    vm->currentCoroutine()->lastResultCount = results;
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

bool native_io_seek(VM* vm, int argCount) {
    if (argCount < 1) {
        vm->runtimeError("file:seek expects at least 1 argument");
        return false;
    }
    
    Value fileVal = vm->peek(argCount - 1);
    if (!fileVal.isFile()) {
        vm->runtimeError("file:seek expects a file object");
        return false;
    }
    FileObject* file = fileVal.asFileObj();
    
    std::string whence = "cur";
    int64_t offset = 0;
    
    if (argCount >= 2) {
        Value whenceVal = vm->peek(argCount - 2);
        if (whenceVal.isString()) whence = vm->getStringValue(whenceVal);
    }
    
    if (argCount >= 3) {
        Value offsetVal = vm->peek(argCount - 3);
        if (offsetVal.isNumber()) offset = static_cast<int64_t>(offsetVal.asNumber());
    }
    
    for(int i=0; i<argCount; i++) vm->pop();
    
    int64_t newPosition = 0;
    if (file->seek(whence, offset, newPosition)) {
        vm->push(Value::number(static_cast<double>(newPosition)));
    } else {
        vm->push(Value::nil());
        vm->push(Value::runtimeString(vm->internString("seek failed")));
    }
    return true;
}

bool native_io_flush(VM* vm, int argCount) {
    FileObject* file = nullptr;
    if (argCount > 0) {
        Value fileVal = vm->peek(argCount - 1);
        if (fileVal.isFile()) {
            file = fileVal.asFileObj();
        }
    }
    
    for(int i=0; i<argCount; i++) vm->pop();

    if (file && file->flush()) {
        vm->push(Value::boolean(true));
    } else {
        vm->push(Value::nil());
        vm->push(Value::runtimeString(vm->internString("flush failed")));
    }
    return true;
}

bool native_io_setvbuf(VM* vm, int argCount) {
    for(int i=0; i<argCount; i++) vm->pop();
    // Dummy implementation for compatibility
    vm->push(Value::boolean(true));
    return true;
}

bool native_io_type(VM* vm, int argCount) {
    if (argCount != 1) {
        vm->runtimeError("io.type expects 1 argument");
        return false;
    }
    Value val = vm->peek(0);
    vm->pop();
    if (val.isFile()) {
        if (val.asFileObj()->isOpen()) {
            vm->push(Value::runtimeString(vm->internString("file")));
        } else {
            vm->push(Value::runtimeString(vm->internString("closed file")));
        }
    } else {
        vm->push(Value::nil());
    }
    return true;
}

bool native_io_tmpfile(VM* vm, int argCount) {
    for(int i=0; i<argCount; i++) vm->pop();
    
    // Simple tmpfile implementation
    static int counter = 0;
    std::string tmpName = "/tmp/lua_tmpfile_" + std::to_string(std::time(nullptr)) + "_" + std::to_string(counter++);
    FileObject* file = vm->openFile(tmpName, "w+");
    if (file && file->isOpen()) {
        vm->push(Value::file(file));
    } else {
        vm->push(Value::nil());
    }
    return true;
}

bool native_io_input(VM* vm, int argCount) {
    Value ioTableVal = vm->getGlobal("io");
    TableObject* ioTable = ioTableVal.isTable() ? ioTableVal.asTableObj() : nullptr;
    
    if (argCount == 0) {
        if (ioTable) {
            vm->push(ioTable->get("stdin"));
        } else {
            vm->push(Value::nil());
        }
        return true;
    }
    
    Value arg = vm->peek(argCount - 1);
    for(int i=0; i<argCount; i++) vm->pop();
    
    if (arg.isString()) {
        FileObject* file = vm->openFile(vm->getStringValue(arg), "r");
        if (!file->isOpen()) {
            vm->runtimeError("io.input: could not open file");
            return false;
        }
        if (ioTable) ioTable->set("stdin", Value::file(file));
        vm->push(Value::file(file));
    } else if (arg.isFile()) {
        if (ioTable) ioTable->set("stdin", arg);
        vm->push(arg);
    } else {
        vm->runtimeError("io.input expects a file or string");
        return false;
    }
    return true;
}

bool native_io_output(VM* vm, int argCount) {
    Value ioTableVal = vm->getGlobal("io");
    TableObject* ioTable = ioTableVal.isTable() ? ioTableVal.asTableObj() : nullptr;
    
    if (argCount == 0) {
        if (ioTable) {
            vm->push(ioTable->get("stdout"));
        } else {
            vm->push(Value::nil());
        }
        return true;
    }
    
    Value arg = vm->peek(argCount - 1);
    for(int i=0; i<argCount; i++) vm->pop();
    
    if (arg.isString()) {
        FileObject* file = vm->openFile(vm->getStringValue(arg), "w");
        if (!file->isOpen()) {
            vm->runtimeError("io.output: could not open file");
            return false;
        }
        if (ioTable) ioTable->set("stdout", Value::file(file));
        vm->push(Value::file(file));
    } else if (arg.isFile()) {
        if (ioTable) ioTable->set("stdout", arg);
        vm->push(arg);
    } else {
        vm->runtimeError("io.output expects a file or string");
        return false;
    }
    return true;
}

bool native_io_lines(VM* vm, int argCount) {
    std::string filename;
    bool toClose = false;
    
    if (argCount == 0) {
        Value ioTableVal = vm->getGlobal("io");
        if (ioTableVal.isTable()) {
            Value in = ioTableVal.asTableObj()->get("stdin");
            vm->push(in);
        } else {
            vm->push(Value::nil());
        }
    } else {
        Value arg = vm->peek(argCount - 1);
        if (arg.isString()) {
            filename = vm->getStringValue(arg);
            toClose = true;
        } else {
            vm->runtimeError("io.lines expects a string filename");
            return false;
        }
    }
    
    for(int i=0; i<argCount; i++) vm->pop();

    const char* script = 
        "local file, toClose = ...\n"
        "if type(file) == 'string' then\n"
        "   file = io.open(file, 'r')\n"
        "   if not file then error('cannot open file') end\n"
        "end\n"
        "return function()\n"
        "   local line = file:read('l')\n"
        "   if not line and toClose then file:close() end\n"
        "   return line\n"
        "end\n";

    FunctionObject* func = vm->compileSource(script, "io.lines");
    if (!func) return false;
    ClosureObject* closure = vm->createClosure(func);
    vm->setupRootUpvalues(closure);
    
    vm->push(Value::closure(closure));
    if (toClose) {
        vm->push(Value::runtimeString(vm->internString(filename)));
    } else {
        Value ioTableVal = vm->getGlobal("io");
        vm->push(ioTableVal.asTableObj()->get("stdin"));
    }
    vm->push(Value::boolean(toClose));
    
    size_t baseFrames = vm->currentCoroutine()->frames.size();
    if (vm->callValue(2, 2)) { // Request 1 result (2 in callValue terms)
        if (vm->currentCoroutine()->frames.size() > baseFrames) {
            if (!vm->run(baseFrames)) return false;
        }
        vm->currentCoroutine()->lastResultCount = 1;
        return true;
    }
    return false;
}

bool native_io_popen(VM* vm, int argCount) {
    if (argCount < 1 || argCount > 2) {
        vm->runtimeError("io.popen expects 1 or 2 arguments");
        return false;
    }
    
    std::string mode = "r";
    if (argCount == 2) {
        Value modeVal = vm->peek(0);
        mode = vm->getStringValue(modeVal);
    }
    Value commandVal = vm->peek(argCount - 1);
    std::string command = vm->getStringValue(commandVal);
    
    FileObject* file = vm->popen(command, mode);
    
    for(int i=0; i<argCount; i++) vm->pop();

    if (file && file->isOpen()) {
        vm->push(Value::file(file));
    } else {
        vm->push(Value::nil());
        vm->push(Value::runtimeString(vm->internString("could not open pipe")));
        vm->currentCoroutine()->lastResultCount = 2;
    }
    return true;
}

bool native_file_lines(VM* vm, int argCount) {
    if (argCount < 1) {
        vm->runtimeError("file:lines expected a file");
        return false;
    }
    Value fileVal = vm->peek(argCount - 1);
    if (!fileVal.isFile()) {
        vm->runtimeError("file:lines expected a file");
        return false;
    }
    
    for(int i=0; i<argCount; i++) vm->pop();

    const char* script = 
        "local file = ...\n"
        "return function()\n"
        "   return file:read('l')\n"
        "end\n";

    FunctionObject* func = vm->compileSource(script, "file:lines");
    if (!func) return false;
    ClosureObject* closure = vm->createClosure(func);
    vm->setupRootUpvalues(closure);
    
    vm->push(Value::closure(closure));
    vm->push(fileVal);
    
    size_t baseFrames = vm->currentCoroutine()->frames.size();
    if (vm->callValue(1, 2)) {
        if (vm->currentCoroutine()->frames.size() > baseFrames) {
            if (!vm->run(baseFrames)) return false;
        }
        vm->currentCoroutine()->lastResultCount = 1;
        return true;
    }
    return false;
}

} // anonymous namespace

void registerIOLibrary(VM* vm, TableObject* ioTable) {
    vm->addNativeToTable(ioTable, "open", native_io_open);
    vm->addNativeToTable(ioTable, "popen", native_io_popen);
    vm->addNativeToTable(ioTable, "write", native_io_write);
    vm->addNativeToTable(ioTable, "read", native_io_read);
    vm->addNativeToTable(ioTable, "close", native_io_close);
    vm->addNativeToTable(ioTable, "flush", native_io_flush);
    vm->addNativeToTable(ioTable, "type", native_io_type);
    vm->addNativeToTable(ioTable, "tmpfile", native_io_tmpfile);
    vm->addNativeToTable(ioTable, "input", native_io_input);
    vm->addNativeToTable(ioTable, "output", native_io_output);
    vm->addNativeToTable(ioTable, "lines", native_io_lines);
    
    // Create FILE metatable to support methods like file:read()
    TableObject* fileMeta = vm->createTable();
    TableObject* fileMethods = vm->createTable();
    vm->addNativeToTable(fileMethods, "read", native_io_read);
    vm->addNativeToTable(fileMethods, "write", native_io_write);
    vm->addNativeToTable(fileMethods, "close", native_io_close);
    vm->addNativeToTable(fileMethods, "seek", native_io_seek);
    vm->addNativeToTable(fileMethods, "flush", native_io_flush);
    vm->addNativeToTable(fileMethods, "setvbuf", native_io_setvbuf);
    vm->addNativeToTable(fileMethods, "lines", native_file_lines);
    
    fileMeta->set("__index", Value::table(fileMethods));
    vm->setTypeMetatable(Value::Type::FILE, Value::table(fileMeta));

    ioTable->set("stdin", Value::file(vm->createFile(stdin, "r")));
    ioTable->set("stdout", Value::file(vm->createFile(stdout, "w")));
    ioTable->set("stderr", Value::file(vm->createFile(stderr, "w")));
}
