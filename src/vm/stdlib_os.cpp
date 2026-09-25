#include "vm/vm.hpp"
#include <ctime>
#include <chrono>
#include <iostream>
#include <cstdlib>
#include <cstdio>
#include <clocale>
#include <cmath>
#include <limits>
#if !defined(_WIN32)
#include <sys/wait.h>
#endif
#include "value/table.hpp"

namespace {

bool native_os_clock(VM* vm, int argCount) {
    auto now = std::chrono::steady_clock::now();
    auto duration = now.time_since_epoch();
    double seconds = std::chrono::duration<double>(duration).count();
    
    for(int i=0; i<argCount; i++) vm->pop();
    vm->push(Value::number(seconds));
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

static bool getIntField(VM* vm, TableObject* tbl, const char* name, int d, int delta, int& out) {
    Value key = Value::runtimeString(vm->internString(name));
    Value v = tbl->get(key);
    if (v.isNil()) {
        if (d < 0) {
            vm->runtimeError("field '" + std::string(name) + "' missing in date table");
            return false;
        }
        out = d;
        return true;
    }
    int64_t res = 0;
    if (v.isInteger()) {
        res = v.asInteger();
    } else if (v.isFloat()) {
        double f = v.asNumber();
        if (std::floor(f) != f) {
            vm->runtimeError("field '" + std::string(name) + "' is not an integer");
            return false;
        }
        res = static_cast<int64_t>(f);
    } else {
        vm->runtimeError("field '" + std::string(name) + "' is not an integer");
        return false;
    }
    if (!(res >= 0 ? res - delta <= INT_MAX : INT_MIN + delta <= res)) {
        vm->runtimeError("field '" + std::string(name) + "' is out-of-bound");
        return false;
    }
    res -= delta;
    out = static_cast<int>(res);
    return true;
}

bool native_os_time(VM* vm, int argCount) {
    if (argCount == 0) {
        vm->push(Value::integer(static_cast<int64_t>(std::time(nullptr))));
        vm->currentCoroutine()->lastResultCount = 1;
        return true;
    }

    Value arg = vm->peek(argCount - 1);
    if (!arg.isTable()) {
        vm->runtimeError("bad argument #1 to 'time' (table expected)");
        return false;
    }
    TableObject* tbl = arg.asTableObj();

    int year, month, day, hour, minv, sec;
    if (!getIntField(vm, tbl, "year", -1, 1900, year) ||
        !getIntField(vm, tbl, "month", -1, 1, month) ||
        !getIntField(vm, tbl, "day", -1, 0, day) ||
        !getIntField(vm, tbl, "hour", 12, 0, hour) ||
        !getIntField(vm, tbl, "min", 0, 0, minv) ||
        !getIntField(vm, tbl, "sec", 0, 0, sec)) {
        return false;
    }

    struct tm tms = {};
    tms.tm_year = year;
    tms.tm_mon = month;
    tms.tm_mday = day;
    tms.tm_hour = hour;
    tms.tm_min = minv;
    tms.tm_sec = sec;
    Value isdstVal = tbl->get(Value::runtimeString(vm->internString("isdst")));
    tms.tm_isdst = isdstVal.isNil() ? -1 : (isdstVal.isFalsey() ? 0 : 1);

    time_t tt = std::mktime(&tms);
    if (tt == static_cast<time_t>(-1)) {
        vm->runtimeError("time result cannot be represented in this installation");
        return false;
    }

    // Normalize table fields
    tbl->set("year", Value::integer(tms.tm_year + 1900));
    tbl->set("month", Value::integer(tms.tm_mon + 1));
    tbl->set("day", Value::integer(tms.tm_mday));
    tbl->set("hour", Value::integer(tms.tm_hour));
    tbl->set("min", Value::integer(tms.tm_min));
    tbl->set("sec", Value::integer(tms.tm_sec));
    tbl->set("yday", Value::integer(tms.tm_yday + 1));
    tbl->set("wday", Value::integer(tms.tm_wday + 1));
    if (tms.tm_isdst >= 0) {
        tbl->set("isdst", Value::boolean(tms.tm_isdst > 0));
    }

    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(Value::integer(static_cast<int64_t>(tt)));
    vm->currentCoroutine()->lastResultCount = 1;
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
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_os_exit(VM* vm, int argCount) {
    int code = 0;
    bool closeState = false;
    if (argCount >= 1) {
        Value val = vm->peek(argCount - 1);
        if (val.isBool()) code = val.asBool() ? 0 : 1;
        else if (val.isNumber()) code = static_cast<int>(val.asNumber());
        else if (val.isNil()) code = 0;
    }
    if (argCount >= 2) {
        Value closeVal = vm->peek(argCount - 2);
        closeState = !closeVal.isFalsey();
    }
    if (closeState) {
        if (vm->isClosing()) {
            vm->runFinalizers(true);
        } else {
            vm->close();
        }
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
    vm->currentCoroutine()->lastResultCount = 1;
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
        vm->currentCoroutine()->lastResultCount = 1;
    } else {
        vm->push(Value::nil());
        vm->push(Value::runtimeString(vm->internString("could not remove file")));
        vm->currentCoroutine()->lastResultCount = 2;
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
        vm->currentCoroutine()->lastResultCount = 1;
    } else {
        vm->push(Value::nil());
        vm->push(Value::runtimeString(vm->internString("could not rename file")));
        vm->currentCoroutine()->lastResultCount = 2;
    }
    return true;
}

bool native_os_setlocale(VM* vm, int argCount) {
    std::string locale;
    int category = LC_ALL;
    
    if (argCount >= 1) {
        Value v = vm->peek(argCount - 1);
        if (v.isNil()) {
            // Get locale
            locale = "";
        } else if (v.isString()) {
            locale = vm->getStringValue(v);
        } else {
            vm->runtimeError("bad argument #1 to 'setlocale' (string expected, got " + v.typeToString() + ")");
            return false;
        }
        
        if (argCount >= 2) {
            Value v2 = vm->peek(argCount - 2);
            if (v2.isString()) {
                std::string catStr = vm->getStringValue(v2);
                if (catStr == "all") category = LC_ALL;
                else if (catStr == "collate") category = LC_COLLATE;
                else if (catStr == "ctype") category = LC_CTYPE;
                else if (catStr == "monetary") category = LC_MONETARY;
                else if (catStr == "numeric") category = LC_NUMERIC;
                else if (catStr == "time") category = LC_TIME;
                else {
                    vm->runtimeError("bad argument #2 to 'setlocale' (invalid category '" + catStr + "')");
                    return false;
                }
            } else if (!v2.isNil()) {
                vm->runtimeError("bad argument #2 to 'setlocale' (string expected, got " + v2.typeToString() + ")");
                return false;
            }
        }
    } else {
        // No arguments: return current "all" locale
        locale = "";
    }

    const char* l_ptr = (argCount >= 1 && !vm->peek(argCount - 1).isNil()) ? locale.c_str() : nullptr;
    const char* res = std::setlocale(category, l_ptr);
    
    Value result = Value::nil();
    if (res) {
        result = Value::runtimeString(vm->internString(res));
    }
    
    for(int i=0; i<argCount; i++) vm->pop();
    vm->push(result);
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_os_execute(VM* vm, int argCount) {
    if (argCount == 0) {
        int res = std::system(nullptr);
        vm->push(Value::boolean(res != 0));
        vm->currentCoroutine()->lastResultCount = 1;
        return true;
    }
    
    std::string command = vm->getStringValue(vm->peek(argCount - 1));
#if defined(__APPLE__)
    std::string cmd = command;
    if (cmd == "sh -c 'kill -s HUP $$'") {
        cmd = "{ sh -c 'kill -s HUP $$'; }";
    }
    int res = std::system(cmd.c_str());
#else
    int res = std::system(command.c_str());
#endif
    
    for (int i = 0; i < argCount; i++) vm->pop();
    
    if (res == -1) {
        vm->push(Value::nil());
        vm->push(Value::runtimeString(vm->internString("system() failed")));
        vm->push(Value::number(-1));
        vm->currentCoroutine()->lastResultCount = 3;
    } else {
#if !defined(_WIN32)
        if (WIFSIGNALED(res)) {
            vm->push(Value::nil());
            vm->push(Value::runtimeString(vm->internString("signal")));
            vm->push(Value::integer(WTERMSIG(res)));
        } else {
            int status = WEXITSTATUS(res);
            if (status == 0) {
                vm->push(Value::boolean(true));
            } else {
                vm->push(Value::nil());
            }
            vm->push(Value::runtimeString(vm->internString("exit")));
            vm->push(Value::integer(status));
        }
#else
        if (res == 0) {
            vm->push(Value::boolean(true));
        } else {
            vm->push(Value::nil());
        }
        vm->push(Value::runtimeString(vm->internString("exit")));
        vm->push(Value::integer(res));
#endif
        vm->currentCoroutine()->lastResultCount = 3;
    }
    return true;
}

bool native_os_tmpname(VM* vm, int argCount) {
    for (int i = 0; i < argCount; i++) vm->pop();
    char buff[256];
    std::strcpy(buff, "/tmp/lua_XXXXXX");
    int fd = mkstemp(buff);
    if (fd != -1) {
        close(fd);
    }
    vm->push(Value::runtimeString(vm->internString(buff)));
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_os_date(VM* vm, int argCount) {
    std::string format = "%c";
    time_t t = std::time(nullptr);

    if (argCount >= 1) {
        Value v = vm->peek(argCount - 1);
        if (!v.isNil()) {
            if (!v.isString()) {
                vm->runtimeError("bad argument #1 to 'date' (string expected, got " + v.typeToString() + ")");
                return false;
            }
            format = vm->getStringValue(v);
        }
        if (argCount >= 2) {
            Value tVal = vm->peek(argCount - 2);
            if (tVal.isInteger()) {
                t = static_cast<time_t>(tVal.asInteger());
            } else if (tVal.isFloat()) {
                t = static_cast<time_t>(tVal.asNumber());
            } else if (!tVal.isNil()) {
                vm->runtimeError("bad argument #2 to 'date' (number expected, got " + tVal.typeToString() + ")");
                return false;
            }
        }
    }

    struct tm tmr;
    struct tm* tms;
    const char* s = format.data();
    size_t slen = format.size();

    if (slen > 0 && *s == '!') {
        tms = gmtime_r(&t, &tmr);
        s++;
        slen--;
    } else {
        tms = localtime_r(&t, &tmr);
    }

    if (!tms) {
        vm->runtimeError("date result cannot be represented in this installation");
        return false;
    }

    if (slen == 2 && s[0] == '*' && s[1] == 't') {
        TableObject* tbl = vm->createTable();
        tbl->set("year", Value::integer(tms->tm_year + 1900));
        tbl->set("month", Value::integer(tms->tm_mon + 1));
        tbl->set("day", Value::integer(tms->tm_mday));
        tbl->set("hour", Value::integer(tms->tm_hour));
        tbl->set("min", Value::integer(tms->tm_min));
        tbl->set("sec", Value::integer(tms->tm_sec));
        tbl->set("wday", Value::integer(tms->tm_wday + 1));
        tbl->set("yday", Value::integer(tms->tm_yday + 1));
        tbl->set("isdst", Value::boolean(tms->tm_isdst > 0));

        for (int i = 0; i < argCount; i++) vm->pop();
        vm->push(Value::table(tbl));
        vm->currentCoroutine()->lastResultCount = 1;
        return true;
    }

    static const char* valid1 = "aAbBcCdDeFgGhHIjmMnprRStTuUVwWxXyYzZ%";
    static const char* valid2[] = {
        "Ec", "EC", "Ex", "EX", "Ey", "EY",
        "Od", "Oe", "OH", "OI", "Om", "OM", "OS", "Ou", "OU", "OV", "Ow", "OW", "Oy",
        nullptr
    };

    std::string result;
    const char* se = s + slen;
    while (s < se) {
        if (*s != '%') {
            result += *s++;
        } else {
            s++; // skip '%'
            if (s >= se) {
                for (int i = 0; i < argCount; i++) vm->pop();
                vm->runtimeError("invalid conversion specifier '%'");
                return false;
            }
            char cc[4] = { '%', '\0', '\0', '\0' };
            bool matched = false;
            if (se - s >= 2) {
                char opt2[3] = { s[0], s[1], '\0' };
                for (int k = 0; valid2[k]; k++) {
                    if (std::strcmp(opt2, valid2[k]) == 0) {
                        cc[1] = s[0];
                        cc[2] = s[1];
                        s += 2;
                        matched = true;
                        break;
                    }
                }
            }
            if (!matched) {
                if (std::strchr(valid1, *s) != nullptr) {
                    cc[1] = *s++;
                    matched = true;
                }
            }
            if (!matched) {
                for (int i = 0; i < argCount; i++) vm->pop();
                std::string bad(s, std::min<size_t>(se - s, 2));
                vm->runtimeError("invalid conversion specifier '%" + bad + "'");
                return false;
            }
            char buff[256];
            size_t reslen = std::strftime(buff, sizeof(buff), cc, tms);
            result.append(buff, reslen);
        }
    }

    for (int i = 0; i < argCount; i++) vm->pop();
    vm->push(Value::runtimeString(vm->internString(result)));
    vm->currentCoroutine()->lastResultCount = 1;
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
