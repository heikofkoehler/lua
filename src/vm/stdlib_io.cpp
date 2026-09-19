#include "vm/vm.hpp"
#include "value/file.hpp"
#include "value/table.hpp"
#include "value/string.hpp"
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <cctype>
#include <vector>
#include <string>
#include <algorithm>
#if !defined(_WIN32)
#include <sys/wait.h>
#endif

namespace {

// Check mode for io.open: standard Lua rules
// Valid: "r", "w", "a", "r+", "w+", "a+", "rb", "wb", "ab", "r+b", "w+b", "a+b"
static bool checkmode(const char* mode) {
    if (*mode == '\0') return false;
    char c = *mode++;
    if (c != 'r' && c != 'w' && c != 'a') return false;
    if (*mode == '+') mode++;
    if (*mode == 'b') mode++;
    return *mode == '\0';
}

// Check mode for io.popen: only "r", "w", "rb", "wb"
static bool checkmodep(const char* mode) {
    return ((mode[0] == 'r' || mode[0] == 'w') &&
            (mode[1] == '\0' || (mode[1] == 'b' && mode[2] == '\0')));
}

static FILE* open_cfile(const std::string& filename, const std::string& mode, DevFullCookie** outCookie = nullptr) {
    if (filename == "/dev/full") {
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
        auto cookie = new DevFullCookie();
        FILE* f = funopen(cookie,
            [](void*, char* buf, int size) -> int { std::memset(buf, 0, size); return size; },
            [](void* c, const char*, int size) -> int {
                auto ck = static_cast<DevFullCookie*>(c);
                if (ck && ck->closing) return size;
                errno = ENOSPC;
                return -1;
            },
            [](void*, fpos_t, int) -> fpos_t { return 0; },
            [](void* c) -> int {
                delete static_cast<DevFullCookie*>(c);
                return 0;
            }
        );
        if (f) {
            std::setvbuf(f, nullptr, _IOFBF, BUFSIZ);
            if (outCookie) *outCookie = cookie;
            return f;
        } else {
            delete cookie;
        }
#endif
    }
    return std::fopen(filename.c_str(), mode.c_str());
}

static bool fileresult(VM* vm, bool stat, const std::string& fname) {
    int en = errno;
    if (stat) {
        vm->push(Value::boolean(true));
        vm->currentCoroutine()->lastResultCount = 1;
        return true;
    } else {
        vm->push(Value::nil());
        std::string msg = fname.empty() ? std::strerror(en) : (fname + ": " + std::strerror(en));
        vm->push(Value::runtimeString(vm->internString(msg)));
        vm->push(Value::integer(en));
        vm->currentCoroutine()->lastResultCount = 3;
        return true;
    }
}

static FileObject* tofile(VM* vm, Value val, int argNum = 1) {
    if (!val.isFile()) {
        std::string typeName = val.isNil() ? "no value" : val.typeToString();
        vm->runtimeError("bad argument #" + std::to_string(argNum) + " (FILE* expected, got " + typeName + ")");
        return nullptr;
    }
    FileObject* f = val.asFileObj();
    if (!f->isOpen()) {
        vm->runtimeError("attempt to use a closed file");
        return nullptr;
    }
    return f;
}

static Value get_io_input(VM* vm) {
    Value v = vm->getRegistry("_IO_input");
    if (v.isFile()) return v;
    Value ioVal = vm->getGlobal("io");
    if (ioVal.isTable()) {
        Value stdinVal = ioVal.asTableObj()->get("stdin");
        if (stdinVal.isFile()) return stdinVal;
    }
    return Value::nil();
}

static Value get_io_output(VM* vm) {
    Value v = vm->getRegistry("_IO_output");
    if (v.isFile()) return v;
    Value ioVal = vm->getGlobal("io");
    if (ioVal.isTable()) {
        Value stdoutVal = ioVal.asTableObj()->get("stdout");
        if (stdoutVal.isFile()) return stdoutVal;
    }
    return Value::nil();
}

static FileObject* getiofile(VM* vm, const std::string& which) {
    Value val = (which == "input") ? get_io_input(vm) : get_io_output(vm);
    if (!val.isFile()) {
        vm->runtimeError("standard " + which + " not found");
        return nullptr;
    }
    FileObject* f = val.asFileObj();
    if (!f->isOpen()) {
        vm->runtimeError("default " + which + " file is closed");
        return nullptr;
    }
    return f;
}

// -------------------------------------------------------------------------
// Reader helpers
// -------------------------------------------------------------------------
#define L_MAXLENNUM 200

struct RN {
    FILE* f;
    int c;
    int n;
    char buff[L_MAXLENNUM + 1];
};

static int nextc(RN* rn) {
    if (rn->n >= L_MAXLENNUM) {
        rn->buff[0] = '\0';
        return 0;
    }
    rn->buff[rn->n++] = static_cast<char>(rn->c);
    rn->c = std::fgetc(rn->f);
    return 1;
}

static int test2(RN* rn, const char* set) {
    if (rn->c == set[0] || rn->c == set[1])
        return nextc(rn);
    return 0;
}

static int readdigits(RN* rn, int hex) {
    int count = 0;
    while ((hex ? std::isxdigit(rn->c) : std::isdigit(rn->c)) && nextc(rn))
        count++;
    return count;
}

static int read_number(VM* vm, FILE* f, Value& result) {
    RN rn;
    int count = 0;
    int hex = 0;
    char decp[2] = { '.', '.' };
    rn.f = f;
    rn.n = 0;
    do {
        rn.c = std::fgetc(rn.f);
    } while (rn.c != EOF && std::isspace(rn.c));
    if (rn.c == EOF) {
        result = Value::nil();
        return 0;
    }
    test2(&rn, "-+");
    if (test2(&rn, "00")) {
        if (test2(&rn, "xX")) hex = 1;
        else count = 1;
    }
    count += readdigits(&rn, hex);
    if (test2(&rn, decp))
        count += readdigits(&rn, hex);
    if (count > 0 && test2(&rn, hex ? "pP" : "eE")) {
        test2(&rn, "-+");
        readdigits(&rn, 0);
    }
    if (rn.c != EOF) std::ungetc(rn.c, rn.f);
    rn.buff[rn.n] = '\0';
    double dnum;
    int64_t inum;
    bool isInt;
    if (VM::stringToNumber(rn.buff, dnum, inum, isInt)) {
        result = isInt ? vm->makeInteger(inum) : Value::number(dnum);
        return 1;
    } else {
        result = Value::nil();
        return 0;
    }
}

static int test_eof(FILE* f) {
    int c = std::fgetc(f);
    if (c != EOF) std::ungetc(c, f);
    return (c != EOF);
}

static bool read_chars(FILE* f, size_t n, std::string& out) {
    out.resize(n);
    size_t nr = std::fread(&out[0], 1, n, f);
    out.resize(nr);
    return nr > 0;
}

static bool read_line(FILE* f, bool chop, std::string& out) {
    out.clear();
    int c;
    while ((c = std::fgetc(f)) != EOF && c != '\n') {
        out += static_cast<char>(c);
    }
    if (!chop && c == '\n') {
        out += '\n';
    }
    return (c == '\n' || !out.empty());
}

static void read_all(FILE* f, std::string& out) {
    out.clear();
    char buf[4096];
    while (size_t nr = std::fread(buf, 1, sizeof(buf), f)) {
        out.append(buf, nr);
    }
}

static bool g_read(VM* vm, FileObject* file, int firstArg, int argCount) {
    FILE* f = file->cfile();
    std::clearerr(f);
    errno = 0;

    int nargs = argCount - firstArg;
    std::vector<Value> results;
    bool success = true;

    if (nargs == 0) {
        std::string s;
        success = read_line(f, true, s);
        if (success) {
            results.push_back(Value::runtimeString(vm->internString(s)));
        }
    } else {
        for (int i = firstArg; i < argCount && success; i++) {
            Value arg = vm->peek(argCount - 1 - i);
            if (arg.isNumber()) {
                size_t l = static_cast<size_t>(arg.asNumber());
                if (l == 0) {
                    if (test_eof(f)) {
                        results.push_back(Value::runtimeString(vm->internString("")));
                    } else {
                        success = false;
                    }
                } else {
                    std::string s;
                    success = read_chars(f, l, s);
                    if (success) results.push_back(Value::runtimeString(vm->internString(s)));
                }
            } else if (arg.isString()) {
                std::string p = vm->getStringValue(arg);
                const char* s = p.c_str();
                if (*s == '*') s++;
                if (std::strcmp(s, "n") == 0) {
                    Value v;
                    success = (read_number(vm, f, v) != 0);
                    if (success) results.push_back(v);
                } else if (std::strcmp(s, "l") == 0 || std::strcmp(s, "line") == 0) {
                    std::string line;
                    success = read_line(f, true, line);
                    if (success) results.push_back(Value::runtimeString(vm->internString(line)));
                } else if (std::strcmp(s, "L") == 0) {
                    std::string line;
                    success = read_line(f, false, line);
                    if (success) results.push_back(Value::runtimeString(vm->internString(line)));
                } else if (std::strcmp(s, "a") == 0 || std::strcmp(s, "all") == 0) {
                    std::string all;
                    read_all(f, all);
                    results.push_back(Value::runtimeString(vm->internString(all)));
                } else {
                    for (int k = 0; k < argCount; k++) vm->pop();
                    vm->runtimeError("bad argument #" + std::to_string(i + 1 - firstArg) + " to 'read' (invalid format)");
                    return false;
                }
            } else {
                for (int k = 0; k < argCount; k++) vm->pop();
                vm->runtimeError("bad argument #" + std::to_string(i + 1 - firstArg) + " to 'read' (invalid format)");
                return false;
            }
        }
    }

    if (std::ferror(f)) {
        for (int k = 0; k < argCount; k++) vm->pop();
        return fileresult(vm, false, "");
    }

    if (!success) {
        results.push_back(Value::nil());
    }

    for (int k = 0; k < argCount; k++) vm->pop();
    for (const auto& v : results) {
        vm->push(v);
    }
    vm->currentCoroutine()->lastResultCount = results.size();
    return true;
}

// -------------------------------------------------------------------------
// Writer helper
// -------------------------------------------------------------------------
static bool g_write(VM* vm, FileObject* file, int startArg, int argCount) {
    FILE* f = file->cfile();
    bool status = true;
    size_t totalWritten = 0;
    errno = 0;

    for (int i = startArg; i < argCount; i++) {
        Value val = vm->peek(argCount - 1 - i);
        if (val.isInteger()) {
            char buf[64];
            int n = std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(val.asInteger()));
            if (n > 0) {
                size_t numbytes = std::fwrite(buf, 1, static_cast<size_t>(n), f);
                totalWritten += numbytes;
                if (numbytes < static_cast<size_t>(n)) {
                    status = false;
                    break;
                }
            } else {
                status = false;
                break;
            }
        } else if (val.isFloat()) {
            std::string s;
            vm->toLString(val, s);
            size_t numbytes = std::fwrite(s.data(), 1, s.size(), f);
            totalWritten += numbytes;
            if (numbytes < s.size()) {
                status = false;
                break;
            }
        } else if (val.isString()) {
            std::string s = vm->getStringValue(val);
            size_t numbytes = std::fwrite(s.data(), 1, s.size(), f);
            totalWritten += numbytes;
            if (numbytes < s.size()) {
                status = false;
                break;
            }
        } else {
            for (int k = 0; k < argCount; k++) vm->pop();
            vm->runtimeError("bad argument #" + std::to_string(i + 1 - startArg) + " to 'write' (string expected, got " + val.typeToString() + ")");
            return false;
        }
    }

    for (int k = 0; k < argCount; k++) vm->pop();

    if (status) {
        vm->push(Value::file(file));
        vm->currentCoroutine()->lastResultCount = 1;
        return true;
    } else {
        int en = errno;
        vm->push(Value::nil());
        vm->push(Value::runtimeString(vm->internString(std::strerror(en))));
        vm->push(Value::integer(en));
        vm->push(vm->makeInteger(static_cast<int64_t>(totalWritten)));
        vm->currentCoroutine()->lastResultCount = 4;
        return true;
    }
}

// -------------------------------------------------------------------------
// native_io_open
// -------------------------------------------------------------------------
bool native_io_open(VM* vm, int argCount) {
    if (argCount < 1 || argCount > 2) {
        vm->runtimeError("io.open expects 1 or 2 arguments");
        return false;
    }

    Value filenameVal = vm->peek(argCount - 1);
    if (!filenameVal.isString()) {
        vm->runtimeError("bad argument #1 to 'open' (string expected, got " + filenameVal.typeToString() + ")");
        return false;
    }
    std::string filename = vm->getStringValue(filenameVal);

    std::string mode = "r";
    if (argCount == 2) {
        Value modeVal = vm->peek(0);
        if (!modeVal.isString()) {
            vm->runtimeError("bad argument #2 to 'open' (string expected, got " + modeVal.typeToString() + ")");
            return false;
        }
        mode = vm->getStringValue(modeVal);
    }

    if (!checkmode(mode.c_str())) {
        for (int i = 0; i < argCount; i++) vm->pop();
        vm->runtimeError("bad argument #2 to 'open' (invalid mode)");
        return false;
    }

    for (int i = 0; i < argCount; i++) vm->pop();

    errno = 0;
    DevFullCookie* cookie = nullptr;
    FILE* f = open_cfile(filename, mode, &cookie);
    if (f == nullptr) {
        return fileresult(vm, false, filename);
    }

    FileObject* file = vm->createFile(f, mode);
    if (cookie) file->setDevFullCookie(cookie);
    vm->push(Value::file(file));
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

// -------------------------------------------------------------------------
// native_io_popen
// -------------------------------------------------------------------------
bool native_io_popen(VM* vm, int argCount) {
    if (argCount < 1 || argCount > 2) {
        vm->runtimeError("io.popen expects 1 or 2 arguments");
        return false;
    }

    Value cmdVal = vm->peek(argCount - 1);
    if (!cmdVal.isString()) {
        vm->runtimeError("bad argument #1 to 'popen' (string expected, got " + cmdVal.typeToString() + ")");
        return false;
    }
    std::string command = vm->getStringValue(cmdVal);

    std::string mode = "r";
    if (argCount == 2) {
        Value modeVal = vm->peek(0);
        if (!modeVal.isString()) {
            vm->runtimeError("bad argument #2 to 'popen' (string expected, got " + modeVal.typeToString() + ")");
            return false;
        }
        mode = vm->getStringValue(modeVal);
    }

    if (!checkmodep(mode.c_str())) {
        for (int i = 0; i < argCount; i++) vm->pop();
        vm->runtimeError("bad argument #2 to 'popen' (invalid mode)");
        return false;
    }

    for (int i = 0; i < argCount; i++) vm->pop();

    errno = 0;
    FileObject* file = vm->popen(command, mode);
    if (!file || !file->isOpen()) {
        return fileresult(vm, false, command);
    }

    vm->push(Value::file(file));
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

// -------------------------------------------------------------------------
// native_io_tmpfile
// -------------------------------------------------------------------------
bool native_io_tmpfile(VM* vm, int argCount) {
    for (int i = 0; i < argCount; i++) vm->pop();
    errno = 0;
    FILE* f = std::tmpfile();
    if (!f) {
        return fileresult(vm, false, "");
    }
    FileObject* file = vm->createFile(f, "wb+");
    vm->push(Value::file(file));
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

// -------------------------------------------------------------------------
// native_file_close and native_io_close
// -------------------------------------------------------------------------
bool native_file_close(VM* vm, int argCount) {
    if (argCount < 1) {
        vm->runtimeError("bad argument #1 to 'close' (FILE* expected, got no value)");
        return false;
    }
    Value val = vm->peek(argCount - 1);
    if (!val.isFile()) {
        vm->runtimeError("bad argument #1 to 'close' (FILE* expected, got " + val.typeToString() + ")");
        return false;
    }
    FileObject* file = val.asFileObj();
    if (!file->isOpen()) {
        vm->runtimeError("attempt to use a closed file");
        return false;
    }

    for (int i = 0; i < argCount; i++) vm->pop();

    if (file->isStandard()) {
        vm->push(Value::nil());
        vm->push(Value::runtimeString(vm->internString("cannot close standard file")));
        vm->currentCoroutine()->lastResultCount = 2;
        return true;
    }

    bool isPipe = file->isPipe();
    errno = 0;
    int res = file->close();
    if (isPipe) {
        if (res == -1) {
            vm->push(Value::nil());
            vm->push(Value::runtimeString(vm->internString(std::strerror(errno))));
            vm->push(Value::integer(errno));
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
    } else {
        if (res == 0) {
            vm->push(Value::boolean(true));
            vm->currentCoroutine()->lastResultCount = 1;
        } else {
            return fileresult(vm, false, "");
        }
    }
    return true;
}

bool native_file_gc(VM* vm, int argCount) {
    if (argCount < 1) return true;
    Value val = vm->peek(argCount - 1);
    for (int i = 0; i < argCount; i++) vm->pop();
    if (val.isFile()) {
        FileObject* file = val.asFileObj();
        if (file->isOpen() && !file->isStandard()) {
            file->close();
        }
    }
    return true;
}

bool native_io_close(VM* vm, int argCount) {
    if (argCount == 0) {
        FileObject* f = getiofile(vm, "output");
        if (!f) return false;
        vm->push(Value::file(f));
        return native_file_close(vm, 1);
    }
    return native_file_close(vm, argCount);
}

// -------------------------------------------------------------------------
// native_file_tostring
// -------------------------------------------------------------------------
bool native_file_tostring(VM* vm, int argCount) {
    if (argCount < 1) {
        vm->runtimeError("bad argument #1 to 'tostring' (FILE* expected)");
        return false;
    }
    Value val = vm->peek(argCount - 1);
    for (int i = 0; i < argCount; i++) vm->pop();
    if (!val.isFile()) {
        vm->push(Value::runtimeString(vm->internString("not a file")));
        vm->currentCoroutine()->lastResultCount = 1;
        return true;
    }
    FileObject* f = val.asFileObj();
    if (!f->isOpen()) {
        vm->push(Value::runtimeString(vm->internString("file (closed)")));
    } else {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "file (%p)", static_cast<void*>(f->cfile()));
        vm->push(Value::runtimeString(vm->internString(buf)));
    }
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

// -------------------------------------------------------------------------
// native_file_write and native_io_write
// -------------------------------------------------------------------------
bool native_file_write(VM* vm, int argCount) {
    if (argCount < 1) {
        vm->runtimeError("bad argument #1 to 'write' (FILE* expected, got no value)");
        return false;
    }
    FileObject* f = tofile(vm, vm->peek(argCount - 1), 1);
    if (!f) return false;
    return g_write(vm, f, 1, argCount);
}

bool native_io_write(VM* vm, int argCount) {
    FileObject* f = getiofile(vm, "output");
    if (!f) return false;
    return g_write(vm, f, 0, argCount);
}

// -------------------------------------------------------------------------
// native_file_read and native_io_read
// -------------------------------------------------------------------------
bool native_file_read(VM* vm, int argCount) {
    if (argCount < 1) {
        vm->runtimeError("bad argument #1 to 'read' (FILE* expected, got no value)");
        return false;
    }
    FileObject* f = tofile(vm, vm->peek(argCount - 1), 1);
    if (!f) return false;
    return g_read(vm, f, 1, argCount);
}

bool native_io_read(VM* vm, int argCount) {
    FileObject* f = getiofile(vm, "input");
    if (!f) return false;
    return g_read(vm, f, 0, argCount);
}

// -------------------------------------------------------------------------
// native_file_seek and native_io_flush
// -------------------------------------------------------------------------
bool native_file_seek(VM* vm, int argCount) {
    if (argCount < 1) {
        vm->runtimeError("bad argument #1 to 'seek' (FILE* expected, got no value)");
        return false;
    }
    FileObject* f = tofile(vm, vm->peek(argCount - 1), 1);
    if (!f) return false;

    std::string whence = "cur";
    int64_t offset = 0;

    if (argCount >= 2) {
        Value wVal = vm->peek(argCount - 2);
        if (wVal.isString()) whence = vm->getStringValue(wVal);
    }
    if (argCount >= 3) {
        Value offVal = vm->peek(argCount - 3);
        if (offVal.isInteger()) offset = offVal.asInteger();
        else if (offVal.isFloat()) offset = static_cast<int64_t>(offVal.asNumber());
    }

    for (int i = 0; i < argCount; i++) vm->pop();

    errno = 0;
    int64_t newPos = 0;
    if (f->seek(whence, offset, newPos)) {
        vm->push(vm->makeInteger(newPos));
        vm->currentCoroutine()->lastResultCount = 1;
        return true;
    } else {
        return fileresult(vm, false, "");
    }
}

bool native_file_flush(VM* vm, int argCount) {
    if (argCount < 1) {
        vm->runtimeError("bad argument #1 to 'flush' (FILE* expected, got no value)");
        return false;
    }
    FileObject* f = tofile(vm, vm->peek(argCount - 1), 1);
    if (!f) return false;
    for (int i = 0; i < argCount; i++) vm->pop();
    errno = 0;
    return fileresult(vm, f->flush(), "");
}

bool native_io_flush(VM* vm, int argCount) {
    for (int i = 0; i < argCount; i++) vm->pop();
    FileObject* f = getiofile(vm, "output");
    if (!f) return false;
    errno = 0;
    return fileresult(vm, f->flush(), "");
}

bool native_file_setvbuf(VM* vm, int argCount) {
    if (argCount < 2) {
        vm->runtimeError("file:setvbuf expects at least 2 arguments");
        return false;
    }
    FileObject* f = tofile(vm, vm->peek(argCount - 1), 1);
    if (!f) return false;

    Value modeVal = vm->peek(argCount - 2);
    if (!modeVal.isString()) {
        vm->runtimeError("bad argument #2 to 'setvbuf' (string expected, got " + modeVal.typeToString() + ")");
        return false;
    }
    std::string mode = vm->getStringValue(modeVal);
    size_t size = 0;
    if (argCount >= 3) {
        Value sizeVal = vm->peek(argCount - 3);
        if (sizeVal.isInteger()) size = static_cast<size_t>(sizeVal.asInteger());
        else if (sizeVal.isFloat()) size = static_cast<size_t>(sizeVal.asNumber());
    }

    for (int i = 0; i < argCount; i++) vm->pop();
    errno = 0;
    return fileresult(vm, f->setvbuf(mode, size), "");
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
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

// -------------------------------------------------------------------------
// native_io_input and native_io_output
// -------------------------------------------------------------------------
bool native_io_input(VM* vm, int argCount) {
    if (argCount == 0) {
        vm->push(get_io_input(vm));
        vm->currentCoroutine()->lastResultCount = 1;
        return true;
    }

    Value arg = vm->peek(argCount - 1);
    for (int i = 0; i < argCount; i++) vm->pop();

    if (arg.isString()) {
        std::string filename = vm->getStringValue(arg);
        errno = 0;
        DevFullCookie* cookie = nullptr;
        FILE* f = open_cfile(filename, "r", &cookie);
        if (!f) {
            vm->runtimeError("cannot open file '" + filename + "' (" + std::strerror(errno) + ")");
            return false;
        }
        FileObject* file = vm->createFile(f, "r");
        if (cookie) file->setDevFullCookie(cookie);
        vm->setRegistry("_IO_input", Value::file(file));
        vm->push(Value::file(file));
    } else if (arg.isFile()) {
        tofile(vm, arg, 1);
        vm->setRegistry("_IO_input", arg);
        vm->push(arg);
    } else {
        vm->runtimeError("bad argument #1 to 'input' (FILE* or string expected, got " + arg.typeToString() + ")");
        return false;
    }
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

bool native_io_output(VM* vm, int argCount) {
    if (argCount == 0) {
        vm->push(get_io_output(vm));
        vm->currentCoroutine()->lastResultCount = 1;
        return true;
    }

    Value arg = vm->peek(argCount - 1);
    for (int i = 0; i < argCount; i++) vm->pop();

    if (arg.isString()) {
        std::string filename = vm->getStringValue(arg);
        errno = 0;
        DevFullCookie* cookie = nullptr;
        FILE* f = open_cfile(filename, "w", &cookie);
        if (!f) {
            vm->runtimeError("cannot open file '" + filename + "' (" + std::strerror(errno) + ")");
            return false;
        }
        FileObject* file = vm->createFile(f, "w");
        if (cookie) file->setDevFullCookie(cookie);
        vm->setRegistry("_IO_output", Value::file(file));
        vm->push(Value::file(file));
    } else if (arg.isFile()) {
        tofile(vm, arg, 1);
        vm->setRegistry("_IO_output", arg);
        vm->push(arg);
    } else {
        vm->runtimeError("bad argument #1 to 'output' (FILE* or string expected, got " + arg.typeToString() + ")");
        return false;
    }
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

// -------------------------------------------------------------------------
// lines iteration
// -------------------------------------------------------------------------
const char* LINES_HELPER = 
    "local file, toClose, argCount = ...\n"
    "local formats = { select(4, ...) }\n"
    "return function()\n"
    "    if io.type(file) == 'closed file' then\n"
    "        error('file is already closed')\n"
    "    end\n"
    "    local res\n"
    "    if argCount == 0 then\n"
    "        res = { file:read() }\n"
    "    else\n"
    "        res = { file:read(table.unpack(formats, 1, argCount)) }\n"
    "    end\n"
    "    if res[1] == nil then\n"
    "        if res[2] ~= nil then\n"
    "            error(res[2])\n"
    "        end\n"
    "        if toClose then file:close() end\n"
    "        return nil\n"
    "    end\n"
    "    return table.unpack(res)\n"
    "end\n";

bool native_io_lines(VM* vm, int argCount) {
    Value fileVal = Value::nil();
    bool toClose = false;
    int firstFmt = 0;

    if (argCount == 0 || vm->peek(argCount - 1).isNil()) {
        FileObject* f = getiofile(vm, "input");
        if (!f) return false;
        fileVal = Value::file(f);
        toClose = false;
        firstFmt = (argCount > 0) ? 1 : 0;
    } else {
        Value arg0 = vm->peek(argCount - 1);
        if (arg0.isString()) {
            std::string fname = vm->getStringValue(arg0);
            errno = 0;
            DevFullCookie* cookie = nullptr;
            FILE* f = open_cfile(fname, "r", &cookie);
            if (!f) {
                for (int i = 0; i < argCount; i++) vm->pop();
                vm->runtimeError("cannot open file '" + fname + "' (" + std::strerror(errno) + ")");
                return false;
            }
            FileObject* file = vm->createFile(f, "r");
            if (cookie) file->setDevFullCookie(cookie);
            fileVal = Value::file(file);
            toClose = true;
            firstFmt = 1;
        } else {
            for (int i = 0; i < argCount; i++) vm->pop();
            vm->runtimeError("bad argument #1 to 'lines' (filename expected)");
            return false;
        }
    }

    int nformats = argCount - firstFmt;
    if (nformats > 250) {
        for (int i = 0; i < argCount; i++) vm->pop();
        vm->runtimeError("too many arguments");
        return false;
    }

    std::vector<Value> formats;
    for (int i = firstFmt; i < argCount; i++) {
        formats.push_back(vm->peek(argCount - 1 - i));
    }
    for (int i = 0; i < argCount; i++) vm->pop();

    FunctionObject* func = vm->compileSource(LINES_HELPER, "io.lines");
    if (!func) return false;
    ClosureObject* closure = vm->createClosure(func);
    vm->setupRootUpvalues(closure);

    vm->push(Value::closure(closure));
    vm->push(fileVal);
    vm->push(Value::boolean(toClose));
    vm->push(Value::integer(nformats));
    for (const auto& fmt : formats) {
        vm->push(fmt);
    }

    size_t baseFrames = vm->currentCoroutine()->frames.size();
    if (vm->callValue(3 + nformats, 2)) {
        if (vm->currentCoroutine()->frames.size() > baseFrames) {
            if (!vm->run(baseFrames)) return false;
        }
        Value iterFunc = vm->pop();
        vm->push(iterFunc);
        if (toClose) {
            vm->push(Value::nil()); // state
            vm->push(Value::nil()); // control
            vm->push(fileVal);      // to-be-closed
            vm->currentCoroutine()->lastResultCount = 4;
        } else {
            vm->currentCoroutine()->lastResultCount = 1;
        }
        return true;
    }
    return false;
}

bool native_file_lines(VM* vm, int argCount) {
    if (argCount < 1) {
        vm->runtimeError("bad argument #1 to 'lines' (FILE* expected, got no value)");
        return false;
    }
    Value fileVal = vm->peek(argCount - 1);
    FileObject* f = tofile(vm, fileVal, 1);
    if (!f) return false;

    int nformats = argCount - 1;
    if (nformats > 250) {
        for (int i = 0; i < argCount; i++) vm->pop();
        vm->runtimeError("too many arguments");
        return false;
    }

    std::vector<Value> formats;
    for (int i = 1; i < argCount; i++) {
        formats.push_back(vm->peek(argCount - 1 - i));
    }
    for (int i = 0; i < argCount; i++) vm->pop();

    FunctionObject* func = vm->compileSource(LINES_HELPER, "file:lines");
    if (!func) return false;
    ClosureObject* closure = vm->createClosure(func);
    vm->setupRootUpvalues(closure);

    vm->push(Value::closure(closure));
    vm->push(fileVal);
    vm->push(Value::boolean(false)); // do not close on exit
    vm->push(Value::integer(nformats));
    for (const auto& fmt : formats) {
        vm->push(fmt);
    }

    size_t baseFrames = vm->currentCoroutine()->frames.size();
    if (vm->callValue(3 + nformats, 2)) {
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
    
    // Create FILE* metatable
    TableObject* fileMeta = vm->createTable();
    fileMeta->set("__name", Value::runtimeString(vm->internString("FILE*")));
    fileMeta->set("__index", Value::table(fileMeta));
    
    vm->addNativeToTable(fileMeta, "read", native_file_read);
    vm->addNativeToTable(fileMeta, "write", native_file_write);
    vm->addNativeToTable(fileMeta, "close", native_file_close);
    vm->addNativeToTable(fileMeta, "seek", native_file_seek);
    vm->addNativeToTable(fileMeta, "flush", native_file_flush);
    vm->addNativeToTable(fileMeta, "setvbuf", native_file_setvbuf);
    vm->addNativeToTable(fileMeta, "lines", native_file_lines);
    vm->addNativeToTable(fileMeta, "__tostring", native_file_tostring);
    vm->addNativeToTable(fileMeta, "__close", native_file_gc);
    vm->addNativeToTable(fileMeta, "__gc", native_file_gc);

    vm->setTypeMetatable(Value::Type::FILE, Value::table(fileMeta));

    FileObject* stdinObj = vm->createFile(stdin, "r");
    FileObject* stdoutObj = vm->createFile(stdout, "w");
    FileObject* stderrObj = vm->createFile(stderr, "w");
    ioTable->set("stdin", Value::file(stdinObj));
    ioTable->set("stdout", Value::file(stdoutObj));
    ioTable->set("stderr", Value::file(stderrObj));
    vm->setRegistry("_IO_input", Value::file(stdinObj));
    vm->setRegistry("_IO_output", Value::file(stdoutObj));
}
