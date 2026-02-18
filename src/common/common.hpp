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
            return "[line " + std::to_string(line) + "] Error: " + message;
        }
        return "Error: " + message;
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
            return "[line " + std::to_string(line) + "] Compile Error: " + message;
        }
        return "Compile Error: " + message;
    }
};

// Logging utilities
namespace Log {
    void error(const std::string& message, int line = -1);
    void warning(const std::string& message);
    void info(const std::string& message);
}

#endif // LUA_COMMON_HPP
