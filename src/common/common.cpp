#include "common/common.hpp"

namespace Log {
    void error(const std::string& message, int line) {
        if (line >= 0) {
            std::cerr << "[line " << line << "] Error: " << message << std::endl;
        } else {
            std::cerr << "Error: " << message << std::endl;
        }
    }

    void warning(const std::string& message) {
        std::cerr << "Warning: " << message << std::endl;
    }

    void info(const std::string& message) {
        std::cout << "Info: " << message << std::endl;
    }
}
