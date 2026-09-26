#include "lua/lua.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <optional>
#include <tuple>
#include <cstdlib>

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond " at " __FILE__ ":" << __LINE__ << std::endl; \
        std::abort(); \
    } \
} while (0)

void test_basic_lifecycle() {
    std::cout << "Testing basic lifecycle and RAII..." << std::endl;
    {
        lua::Context ctx;
        TEST_ASSERT(ctx.isValid());
        TEST_ASSERT(ctx.state() != nullptr);
        TEST_ASSERT(ctx.native_handle() != nullptr);
    }
    // Move semantics
    {
        lua::Context ctx1;
        ctx1.set("foo", 123);
        lua::Context ctx2 = std::move(ctx1);
        TEST_ASSERT(ctx2.isValid());
        TEST_ASSERT(!ctx1.isValid());
        TEST_ASSERT(ctx2.get<int>("foo") == 123);
    }
    std::cout << "  Passed!" << std::endl;
}

void test_execution_and_eval() {
    std::cout << "Testing run and eval..." << std::endl;
    lua::Context ctx;

    ctx.run("a = 10; b = 20; c = a + b");
    TEST_ASSERT(ctx.get<int>("c") == 30);

    // eval with explicit return
    int res1 = ctx.eval<int>("return a * b");
    TEST_ASSERT(res1 == 200);

    // eval with implicit return
    int res2 = ctx.eval<int>("a + b + 5");
    TEST_ASSERT(res2 == 35);

    double dbl = ctx.eval<double>("return math.sqrt(16.0)");
    TEST_ASSERT(dbl == 4.0);

    std::string str = ctx.eval<std::string>("return 'hello ' .. 'world'");
    TEST_ASSERT(str == "hello world");

    bool b = ctx.eval<bool>("return 10 > 5");
    TEST_ASSERT(b == true);

    // void eval
    ctx.eval<void>("x = 999");
    TEST_ASSERT(ctx.get<int>("x") == 999);

    std::cout << "  Passed!" << std::endl;
}

void test_variables_and_proxy() {
    std::cout << "Testing variables and proxy syntax..." << std::endl;
    lua::Context ctx;

    // Direct get/set
    ctx.set("num", 42);
    TEST_ASSERT(ctx.get<int>("num") == 42);
    TEST_ASSERT(ctx.get<double>("num") == 42.0);

    ctx.set("greeting", std::string("hi there"));
    TEST_ASSERT(ctx.get<std::string>("greeting") == "hi there");

    // Proxy assignment and implicit conversion
    ctx["val"] = 100;
    int val = ctx["val"];
    TEST_ASSERT(val == 100);

    ctx["name"] = "Alice";
    std::string name = ctx["name"];
    TEST_ASSERT(name == "Alice");

    // Explicit as<T>()
    TEST_ASSERT(ctx["val"].as<int>() == 100);
    TEST_ASSERT(ctx["name"].as<std::string>() == "Alice");

    std::cout << "  Passed!" << std::endl;
}

static int free_add(int a, int b) {
    return a + b;
}

void test_function_bindings() {
    std::cout << "Testing function bindings..." << std::endl;
    lua::Context ctx;

    // 1. Stateless lambda
    ctx.bind("add", [](double a, double b) {
        return a + b;
    });
    double res = ctx.eval<double>("return add(10, 25)");
    TEST_ASSERT(res == 35.0);

    // 2. Capturing lambda (stateful)
    int factor = 5;
    ctx.bind("multiply_factor", [factor](int x) {
        return x * factor;
    });
    TEST_ASSERT(ctx.eval<int>("return multiply_factor(6)") == 30);

    // 3. Free function
    ctx.bind("free_add", free_add);
    TEST_ASSERT(ctx.eval<int>("return free_add(15, 27)") == 42);

    // 4. Void returning lambda
    int side_effect = 0;
    ctx.bind("set_side_effect", [&side_effect](int v) {
        side_effect = v;
    });
    ctx.run("set_side_effect(777)");
    TEST_ASSERT(side_effect == 777);

    // 5. Zero argument lambda
    ctx.bind("get_magic", []() {
        return 4242;
    });
    TEST_ASSERT(ctx.eval<int>("return get_magic()") == 4242);

    // 6. Multiple return values via std::tuple
    ctx.bind("divmod", [](int a, int b) {
        return std::make_tuple(a / b, a % b);
    });
    ctx.run("q, r = divmod(19, 4)");
    TEST_ASSERT(ctx.get<int>("q") == 4);
    TEST_ASSERT(ctx.get<int>("r") == 3);

    // 7. Proxy assignment of callable
    ctx["square"] = [](int x) { return x * x; };
    TEST_ASSERT(ctx.eval<int>("return square(9)") == 81);

    // 8. Direct call via proxy operator()
    int sq = ctx["square"](7);
    TEST_ASSERT(sq == 49);

    std::cout << "  Passed!" << std::endl;
}

void test_calling_lua_from_cpp() {
    std::cout << "Testing calling Lua functions from C++..." << std::endl;
    lua::Context ctx;

    ctx.run(R"lua(
        function concat_prefix(prefix, str)
            return prefix .. ": " .. str
        end

        function stats(a, b, c)
            local sum = a + b + c
            local avg = sum / 3.0
            return sum, avg
        end
    )lua");

    // Call single return
    std::string s = ctx.call<std::string>("concat_prefix", "Result", "Success");
    TEST_ASSERT(s == "Result: Success");

    // Call multiple returns via tuple
    auto [sum, avg] = ctx.call<std::tuple<int, double>>("stats", 10, 20, 30);
    TEST_ASSERT(sum == 60);
    TEST_ASSERT(avg == 20.0);

    // Call via proxy
    std::string s2 = ctx["concat_prefix"]("Score", "100");
    TEST_ASSERT(s2 == "Score: 100");

    std::cout << "  Passed!" << std::endl;
}

void test_containers_and_tables() {
    std::cout << "Testing containers and tables..." << std::endl;
    lua::Context ctx;

    // std::vector
    std::vector<int> nums = {10, 20, 30, 40, 50};
    ctx.set("numbers", nums);
    ctx.run("table.insert(numbers, 60)");
    auto resVec = ctx.get<std::vector<int>>("numbers");
    TEST_ASSERT(resVec.size() == 6);
    TEST_ASSERT(resVec[0] == 10);
    TEST_ASSERT(resVec[5] == 60);

    // std::map
    std::map<std::string, int> ages = {{"Alice", 30}, {"Bob", 25}};
    ctx.set("ages", ages);
    ctx.run("ages['Charlie'] = 35");
    auto resMap = ctx.get<std::map<std::string, int>>("ages");
    TEST_ASSERT(resMap.size() == 3);
    TEST_ASSERT(resMap["Alice"] == 30);
    TEST_ASSERT(resMap["Charlie"] == 35);

    // std::optional
    std::optional<int> optSome = 42;
    std::optional<int> optNone = std::nullopt;
    ctx.set("has_val", optSome);
    ctx.set("no_val", optNone);
    TEST_ASSERT(ctx.eval<bool>("return has_val == 42"));
    TEST_ASSERT(ctx.eval<bool>("return no_val == nil"));
    auto readSome = ctx.get<std::optional<int>>("has_val");
    auto readNone = ctx.get<std::optional<int>>("no_val");
    TEST_ASSERT(readSome.has_value() && *readSome == 42);
    TEST_ASSERT(!readNone.has_value());

    // Table wrapper object
    lua::Table tbl = ctx.create_table();
    tbl.set("author", "Lua Team");
    tbl.set("version", 5.5);
    tbl.set(1, "item one");
    TEST_ASSERT(tbl.get<std::string>("author") == "Lua Team");
    TEST_ASSERT(tbl.get<double>("version") == 5.5);
    TEST_ASSERT(tbl.get<std::string>(1) == "item one");
    TEST_ASSERT(tbl.has("author"));
    TEST_ASSERT(!tbl.has("nonexistent"));

    // TableProxy
    tbl["desc"] = "Modern C++ binding";
    std::string desc = tbl["desc"];
    TEST_ASSERT(desc == "Modern C++ binding");

    tbl[2] = "item two";
    std::string item2 = tbl[2];
    TEST_ASSERT(item2 == "item two");

    // Setting a Table as a global
    ctx.set("config_table", tbl);
    TEST_ASSERT(ctx.eval<std::string>("return config_table.desc") == "Modern C++ binding");

    std::cout << "  Passed!" << std::endl;
}

void test_error_handling() {
    std::cout << "Testing error handling..." << std::endl;
    lua::Context ctx;

    // Syntax error
    bool caughtSyntax = false;
    try {
        ctx.run("this is completely invalid lua code !@#$");
    } catch (const lua::SyntaxError& e) {
        caughtSyntax = true;
        TEST_ASSERT(e.code() == LUA_ERRSYNTAX);
    }
    TEST_ASSERT(caughtSyntax);

    // Runtime error
    bool caughtRuntime = false;
    try {
        ctx.run("local x = nil; x.foo = 123");
    } catch (const lua::RuntimeError& e) {
        caughtRuntime = true;
        TEST_ASSERT(e.code() == LUA_ERRRUN);
    }
    TEST_ASSERT(caughtRuntime);

    // C++ exception translated to Lua error
    ctx.bind("throw_error", []() {
        throw std::runtime_error("custom C++ exception triggered");
    });
    bool caughtFromCpp = false;
    try {
        ctx.run("throw_error()");
    } catch (const lua::RuntimeError& e) {
        caughtFromCpp = true;
        std::string msg = e.what();
        TEST_ASSERT(msg.find("custom C++ exception triggered") != std::string::npos);
    }
    TEST_ASSERT(caughtFromCpp);

    // Lua pcall catching C++ exception
    ctx.run(R"lua(
        local ok, err = pcall(throw_error)
        assert(not ok)
        assert(string.find(tostring(err), "custom C++ exception triggered", 1, true) ~= nil)
    )lua");

    std::cout << "  Passed!" << std::endl;
}

int main() {
    std::cout << "=== Running Modern C++ Embedding API Tests ===" << std::endl;
    test_basic_lifecycle();
    test_execution_and_eval();
    test_variables_and_proxy();
    test_function_bindings();
    test_calling_lua_from_cpp();
    test_containers_and_tables();
    test_error_handling();
    std::cout << "\nAll Modern C++ Embedding API tests passed successfully!" << std::endl;
    return 0;
}
