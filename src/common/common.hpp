#ifndef LUA_COMMON_HPP
#define LUA_COMMON_HPP

#include <cstdint>
#include <cstddef>
#include <iostream>
#include <string>
#include <stdexcept>

// Debug flags (set via CMake options)
#ifdef DEBUG_TRACE_EXECUTION
    #define TRACE_EXECUTION
#endif

#ifdef DEBUG_PRINT_CODE
    #define PRINT_CODE
#endif

// Utility macros
#define UNUSED(x) (void)(x)

// Error reporting
class RuntimeError : public std::runtime_error {
public:
    explicit RuntimeError(const std::string& message, int line = -1)
        : std::runtime_error(formatMessage(message, line)), line_(line) {}

    int line() const { return line_; }

private:
    int line_;

    static std::string formatMessage(const std::string& message, int line) {
        if (line >= 0) {
            return ":" + std::to_string(line) + ": " + message;
        }
        return message;
    }
};

class CompileError : public std::runtime_error {
public:
    explicit CompileError(const std::string& message, int line = -1)
        : std::runtime_error(formatMessage(message, line)), line_(line) {}

    int line() const { return line_; }

private:
    int line_;

    static std::string formatMessage(const std::string& message, int line) {
        if (line >= 0) {
            return ":" + std::to_string(line) + ": " + message;
        }
        return message;
    }
};

// Logging utilities
namespace Log {
    void error(const std::string& message, int line = -1);
    void warning(const std::string& message);
    void info(const std::string& message);
}

inline std::string formatChunkId(const std::string& source) {
    if (source.empty()) return "[string \"\"]";
    constexpr size_t LUA_IDSIZE = 60;
    constexpr size_t IDSIZE = LUA_IDSIZE - 1; // 59 max length
    size_t srclen = source.length();
    const char* src = source.c_str();

    if (*src == '=') {
        if (srclen - 1 <= IDSIZE) {
            return source.substr(1);
        } else {
            return source.substr(1, IDSIZE);
        }
    } else if (*src == '@') {
        if (srclen - 1 <= IDSIZE) {
            return source.substr(1);
        } else {
            return "..." + source.substr(srclen - (IDSIZE - 3));
        }
    } else {
        const char* nl = std::strchr(src, '\n');
        size_t len = nl ? static_cast<size_t>(nl - src) : srclen;
        if (len <= 48 && nl == nullptr) {
            return "[string \"" + source.substr(0, len) + "\"]";
        } else {
            if (len > 45) len = 45;
            return "[string \"" + source.substr(0, len) + "...\"]";
        }
    }
}

#endif // LUA_COMMON_HPP
