#include "value/value.hpp"
#include "value/function.hpp"
#include <sstream>
#include <iomanip>

std::string Value::toString() const {
    std::ostringstream oss;
    print(oss);
    return oss.str();
}

void Value::print(std::ostream& os) const {
    switch (type()) {
        case Type::NIL:
            os << "nil";
            break;

        case Type::BOOL:
            os << (asBool() ? "true" : "false");
            break;

        case Type::NUMBER: {
            double num = asNumber();
            // Check if it's an integer value
            if (std::floor(num) == num && !std::isinf(num) && !std::isnan(num)) {
                os << static_cast<int64_t>(num);
            } else {
                os << std::setprecision(14) << num;
            }
            break;
        }

        case Type::FUNCTION: {
            size_t funcIndex = asFunctionIndex();
            os << "<function:" << funcIndex << ">";
            break;
        }
    }
}
