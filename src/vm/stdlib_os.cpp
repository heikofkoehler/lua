#include "vm/vm.hpp"
#include <ctime>
#include <chrono>
#include <iostream>
#include <cstdlib>
#include <cstdio>

namespace {

bool native_os_clock(VM* vm, int argCount) {
    auto now = std::chrono::steady_clock::now();
    auto duration = now.time_since_epoch();
    double seconds = std::chrono::duration<double>(duration).count();
    
    for(int i=0; i<argCount; i++) vm->pop();
    vm->push(Value::number(seconds));
    return true;
}

// Helper to convert a Lua value to an integer, verifying it's a whole number.
// On error the function will push a suitable runtime error message and return false.
static bool getIntField(VM* vm, TableObject* tbl, const char* name, bool required, long long& out) {
    Value key = Value::runtimeString(vm->internString(name));
    Value v = tbl->get(key);
    if (v.isNil()) {
        if (required) {
            vm->runtimeError("missing");
            return false;
        }
        out = 0;
        return true;
    }
    if (!v.isNumber()) {
        vm->runtimeError("not an integer");
        return false;
    }
    double d = v.asNumber();
    if (std::floor(d) != d) {
        vm->runtimeError("not an integer");
        return false;
    }
    out = static_cast<long long>(d);
    return true;
}

bool native_os_time(VM* vm, int argCount) {
    if (argCount == 0) {
        vm->push(Value::number(static_cast<double>(std::time(nullptr))));
        return true;
    }

    // Table argument case: build a tm struct from the table fields.
    Value arg = vm->peek(argCount - 1);
    if (!arg.isTable()) {
        vm->runtimeError("bad argument #1 to 'time' (table expected)");
        return false;
    }
    TableObject* tbl = arg.asTableObj();

    long long year, month, day, hour, minv, sec;
    if (!getIntField(vm, tbl, "year", true, year) ||
        !getIntField(vm, tbl, "month", true, month) ||
        !getIntField(vm, tbl, "day", true, day) ||
        !getIntField(vm, tbl, "hour", false, hour) ||
        !getIntField(vm, tbl, "min", false, minv) ||
        !getIntField(vm, tbl, "sec", false, sec)) {
        return false; // error message already pushed
    }

    // Convert to struct tm. tm_year is years since 1900 and is stored in a C int.
    long long tm_year = year - 1900;
    if (tm_year < std::numeric_limits<int>::min() || tm_year > std::numeric_limits<int>::max()) {
        vm->runtimeError("field 'year' is out-of-bound");
        return false;
    }

    struct tm tms = {};
    tms.tm_year = static_cast<int>(tm_year);
    // tm_mon is 0-based
    tms.tm_mon = static_cast<int>(month - 1);
    tms.tm_mday = static_cast<int>(day);
    tms.tm_hour = static_cast<int>(hour);
    tms.tm_min = static_cast<int>(minv);
    tms.tm_sec = static_cast<int>(sec);
    tms.tm_isdst = -1;

    time_t tt = mktime(&tms);
    if (tt == (time_t)-1) {
        vm->runtimeError("cannot be represented");
        return false;
    }

    for(int i=0; i<argCount; i++) vm->pop();
    vm->push(Value::number(static_cast<double>(tt)));
    return true;
}

bool native_os_difftime(VM* vm, int argCount) {
    if (argCount != 2) {
        vm->runtimeError("os.difftime expects 2 arguments");
        return false;
    }
    Value v2 = vm->peek(0);
    Value v1 = vm->peek(1);
    if (!v1.isNumber() || !v2.isNumber()) {
        vm->runtimeError("os.difftime expects number arguments");
        return false;
    }
    double diff = v1.asNumber() - v2.asNumber();
    
    for(int i=0; i<argCount; i++) vm->pop();
    vm->push(Value::number(diff));
    return true;
}

bool native_os_exit(VM* vm, int argCount) {
    int code = 0;
    if (argCount >= 1) {
        Value val = vm->peek(argCount - 1);
        if (val.isBool()) code = val.asBool() ? 0 : 1;
        else if (val.isNumber()) code = static_cast<int>(val.asNumber());
    }
    std::exit(code);
    return true;
}

bool native_os_getenv(VM* vm, int argCount) {
    if (argCount != 1) {
        vm->runtimeError("os.getenv expects 1 argument");
        return false;
    }
    Value var = vm->peek(0);
    const char* val = std::getenv(vm->getStringValue(var).c_str());
    
    for(int i=0; i<argCount; i++) vm->pop();
    
    if (val) {
        vm->push(Value::runtimeString(vm->internString(val)));
    } else {
        vm->push(Value::nil());
    }
    return true;
}

bool native_os_remove(VM* vm, int argCount) {
    if (argCount != 1) {
        vm->runtimeError("os.remove expects 1 argument");
        return false;
    }
    Value filename = vm->peek(0);
    int res = std::remove(vm->getStringValue(filename).c_str());
    
    for(int i=0; i<argCount; i++) vm->pop();
    
    if (res == 0) {
        vm->push(Value::boolean(true));
    } else {
        vm->push(Value::nil());
        vm->push(Value::runtimeString(vm->internString("could not remove file")));
    }
    return true;
}

bool native_os_rename(VM* vm, int argCount) {
    if (argCount != 2) {
        vm->runtimeError("os.rename expects 2 arguments");
        return false;
    }
    Value newname = vm->peek(0);
    Value oldname = vm->peek(1);
    int res = std::rename(vm->getStringValue(oldname).c_str(), vm->getStringValue(newname).c_str());
    
    for(int i=0; i<argCount; i++) vm->pop();
    
    if (res == 0) {
        vm->push(Value::boolean(true));
    } else {
        vm->push(Value::nil());
        vm->push(Value::runtimeString(vm->internString("could not rename file")));
    }
    return true;
}

bool native_os_setlocale(VM* vm, int argCount) {
    std::string locale = "C";
    if (argCount >= 1) {
        Value v = vm->peek(argCount - 1);
        if (v.isString()) locale = vm->getStringValue(v);
    }
    const char* res = std::setlocale(LC_ALL, locale.c_str());
    
    Value result = Value::nil();
    if (res) {
        result = Value::runtimeString(vm->internString(res));
    }
    
    for(int i=0; i<argCount; i++) vm->pop();
    vm->push(result);
    return true;
}

bool native_os_execute(VM* vm, int argCount) {
    if (argCount == 0) {
        int res = std::system(nullptr);
        vm->push(Value::boolean(res != 0));
        return true;
    }
    
    std::string command = vm->getStringValue(vm->peek(argCount - 1));
    int res = std::system(command.c_str());
    
    for (int i = 0; i < argCount; i++) vm->pop();
    
    if (res == -1) {
        vm->push(Value::nil());
        vm->push(Value::runtimeString(vm->internString("system() failed")));
        vm->push(Value::number(-1));
    } else {
        vm->push(Value::boolean(true));
        vm->push(Value::runtimeString(vm->internString("exit")));
        vm->push(Value::number(res));
    }
    return true;
}

bool native_os_tmpname(VM* vm, int argCount) {
    // We'll use a simple implementation for now.
    // In a real VM, we should use a more secure way.
    static int counter = 0;
    std::string tmpName = "/tmp/lua_tmp_" + std::to_string(std::time(nullptr)) + "_" + std::to_string(counter++);
    
    for(int i=0; i<argCount; i++) vm->pop();
    vm->push(Value::runtimeString(vm->internString(tmpName)));
    return true;
}

bool native_os_date(VM* vm, int argCount) {
    std::string format = "%c";
    time_t t = std::time(nullptr);
    
    if (argCount >= 1) {
        Value v = vm->peek(argCount - 1);
        if (v.isString()) format = vm->getStringValue(v);
        if (argCount >= 2) {
            t = static_cast<time_t>(vm->peek(argCount - 2).asNumber());
        }
    }
    
    struct tm* tms;
    if (!format.empty() && format[0] == '!') {
        tms = std::gmtime(&t);
        format = format.substr(1);
    } else {
        tms = std::localtime(&t);
    }
    
    if (format == "*t") {
        TableObject* tbl = vm->createTable();
        tbl->set("year", Value::number(tms->tm_year + 1900));
        tbl->set("month", Value::number(tms->tm_mon + 1));
        tbl->set("day", Value::number(tms->tm_mday));
        tbl->set("hour", Value::number(tms->tm_hour));
        tbl->set("min", Value::number(tms->tm_min));
        tbl->set("sec", Value::number(tms->tm_sec));
        tbl->set("wday", Value::number(tms->tm_wday + 1));
        tbl->set("yday", Value::number(tms->tm_yday + 1));
        tbl->set("isdst", Value::boolean(tms->tm_isdst > 0));
        
        for(int i=0; i<argCount; i++) vm->pop();
        vm->push(Value::table(tbl));
        return true;
    }
    
    char buffer[256];
    if (std::strftime(buffer, sizeof(buffer), format.c_str(), tms)) {
        for(int i=0; i<argCount; i++) vm->pop();
        vm->push(Value::runtimeString(vm->internString(buffer)));
        return true;
    }
    
    for(int i=0; i<argCount; i++) vm->pop();
    vm->push(Value::nil());
    return true;
}

} // anonymous namespace

void registerOSLibrary(VM* vm, TableObject* osTable) {
    vm->addNativeToTable(osTable, "clock", native_os_clock);
    vm->addNativeToTable(osTable, "time", native_os_time);
    vm->addNativeToTable(osTable, "difftime", native_os_difftime);
    vm->addNativeToTable(osTable, "exit", native_os_exit);
    vm->addNativeToTable(osTable, "getenv", native_os_getenv);
    vm->addNativeToTable(osTable, "remove", native_os_remove);
    vm->addNativeToTable(osTable, "rename", native_os_rename);
    vm->addNativeToTable(osTable, "setlocale", native_os_setlocale);
    vm->addNativeToTable(osTable, "execute", native_os_execute);
    vm->addNativeToTable(osTable, "tmpname", native_os_tmpname);
    vm->addNativeToTable(osTable, "date", native_os_date);
}
