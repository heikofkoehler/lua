#include "lua/lua.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <tuple>
#include <cmath>

int main() {
    std::cout << "=========================================================\n";
    std::cout << "  LuaVM Modern C++17 Embedding API Demonstration\n";
    std::cout << "=========================================================\n\n";

    // 1. Initialize Lua Context (RAII manages lua_State lifecycle)
    lua::Context ctx;
    std::cout << "[1] Initialized Lua context with standard libraries.\n";

    // 2. Direct script execution
    ctx.run(R"lua(
        print("  [Lua] Hello from inside Lua!")
        global_message = "Greetings from Lua environment"
    )lua");

    // 3. Variable access via typed get/set and proxy syntax
    ctx.set("app_version", 2.0);
    ctx["author"] = "LuaVM Team";

    std::cout << "\n[2] Variables passed between C++ and Lua:\n";
    std::cout << "  author:      " << ctx.get<std::string>("author") << "\n";
    std::cout << "  app_version: " << ctx.get<double>("app_version") << "\n";
    std::cout << "  message:     " << ctx["global_message"].as<std::string>() << "\n";

    // 4. Expression evaluation with automatic type deduction
    double hypotenuse = ctx.eval<double>("math.sqrt(3.0^2 + 4.0^2)");
    std::cout << "\n[3] Expression evaluation:\n";
    std::cout << "  hypotenuse of 3 and 4 = " << hypotenuse << "\n";

    // 5. Binding C++ Lambdas & Free Functions
    std::cout << "\n[4] Binding C++ lambdas:\n";

    // Stateless lambda
    ctx.bind("add", [](double a, double b) {
        return a + b;
    });

    // Stateful capturing lambda
    double tax_rate = 0.0825;
    ctx.bind("calculate_total", [tax_rate](double subtotal) {
        return subtotal * (1.0 + tax_rate);
    });

    // Multiple return values via std::tuple
    ctx.bind("divide_and_remainder", [](int dividend, int divisor) {
        if (divisor == 0) {
            throw std::runtime_error("division by zero in divide_and_remainder");
        }
        return std::make_tuple(dividend / divisor, dividend % divisor);
    });

    ctx.run(R"lua(
        local sum = add(10.5, 24.5)
        print(string.format("  [Lua] add(10.5, 24.5) = %.1f", sum))

        local total = calculate_total(100.0)
        print(string.format("  [Lua] calculate_total(100.0) = %.2f", total))

        local q, r = divide_and_remainder(29, 6)
        print(string.format("  [Lua] 29 / 6 = %d remainder %d", q, r))
    )lua");

    // 6. Calling Lua functions from C++
    std::cout << "\n[5] Calling Lua functions directly from C++:\n";
    ctx.run(R"lua(
        function compute_stats(values)
            local sum = 0
            for _, v in ipairs(values) do
                sum = sum + v
            end
            local avg = sum / #values
            return sum, avg
        end
    )lua");

    std::vector<int> numbers = {10, 20, 30, 40, 50};
    auto [sum, avg] = ctx.call<std::tuple<int, double>>("compute_stats", numbers);
    std::cout << "  Array sum: " << sum << ", average: " << avg << "\n";

    // 7. Table manipulation via lua::Table
    std::cout << "\n[6] Structured Table operations:\n";
    lua::Table config = ctx.create_table();
    config["host"] = "127.0.0.1";
    config["port"] = 8080;
    config["ssl_enabled"] = true;

    ctx.set("server_config", config);
    ctx.run(R"lua(
        print(string.format("  [Lua] Server listening on %s:%d (SSL: %s)",
            server_config.host, server_config.port, tostring(server_config.ssl_enabled)))
    )lua");

    // 8. Exception safety & translation
    std::cout << "\n[7] Exception handling:\n";
    ctx.bind("risky_operation", [](int value) {
        if (value < 0) {
            throw std::runtime_error("negative value not allowed!");
        }
        return value * 2;
    });

    ctx.run(R"lua(
        local ok, err = pcall(risky_operation, -5)
        if not ok then
            print("  [Lua] Safely caught C++ exception: " .. tostring(err))
        end
    )lua");

    std::cout << "\n=========================================================\n";
    std::cout << "  Embedding demonstration completed successfully!\n";
    std::cout << "=========================================================\n";
    return 0;
}
