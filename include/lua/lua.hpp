#ifndef LUA_MODERN_CPP_HPP
#define LUA_MODERN_CPP_HPP

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

// Include Lua C API headers
#if defined(__has_include)
  #if __has_include("api/lua.h")
    #include "api/lua.h"
    #include "api/lauxlib.h"
    #include "api/lualib.h"
  #elif __has_include("lua.h")
    #include "lua.h"
    #include "lauxlib.h"
    #include "lualib.h"
  #else
    #include "../../src/api/lua.h"
    #include "../../src/api/lauxlib.h"
    #include "../../src/api/lualib.h"
  #endif
#else
  #include "api/lua.h"
  #include "api/lauxlib.h"
  #include "api/lualib.h"
#endif

namespace lua {

// Forward declarations
class Context;
class Table;
class TableProxy;
class GlobalProxy;
class ValueRef;

// ============================================================================
// Exceptions
// ============================================================================

class Error : public std::runtime_error {
public:
    explicit Error(const std::string& msg, int code = LUA_ERRRUN)
        : std::runtime_error(msg), code_(code) {}
    int code() const noexcept { return code_; }
private:
    int code_;
};

class SyntaxError : public Error {
public:
    explicit SyntaxError(const std::string& msg) : Error(msg, LUA_ERRSYNTAX) {}
};

class RuntimeError : public Error {
public:
    explicit RuntimeError(const std::string& msg) : Error(msg, LUA_ERRRUN) {}
};

// ============================================================================
// Type Enum
// ============================================================================

enum class Type {
    None          = LUA_TNONE,
    Nil           = LUA_TNIL,
    Boolean       = LUA_TBOOLEAN,
    LightUserdata = LUA_TLIGHTUSERDATA,
    Number        = LUA_TNUMBER,
    String        = LUA_TSTRING,
    Table         = LUA_TTABLE,
    Function      = LUA_TFUNCTION,
    Userdata      = LUA_TUSERDATA,
    Thread        = LUA_TTHREAD
};

inline const char* type_name(Type t) {
    switch (t) {
        case Type::None:          return "none";
        case Type::Nil:           return "nil";
        case Type::Boolean:       return "boolean";
        case Type::LightUserdata: return "lightuserdata";
        case Type::Number:        return "number";
        case Type::String:        return "string";
        case Type::Table:         return "table";
        case Type::Function:      return "function";
        case Type::Userdata:      return "userdata";
        case Type::Thread:        return "thread";
        default:                  return "unknown";
    }
}

// ============================================================================
// Template Metaprogramming Utilities
// ============================================================================

namespace detail {

template<typename T>
struct is_tuple : std::false_type {};

template<typename... Ts>
struct is_tuple<std::tuple<Ts...>> : std::true_type {};

template<typename T>
inline constexpr bool is_tuple_v = is_tuple<std::decay_t<T>>::value;

template<typename T>
struct tuple_or_single_size {
    static constexpr size_t value = 1;
};

template<typename... Ts>
struct tuple_or_single_size<std::tuple<Ts...>> {
    static constexpr size_t value = sizeof...(Ts);
};

template<typename T>
inline constexpr size_t tuple_or_single_size_v = tuple_or_single_size<std::decay_t<T>>::value;

// Inspect callable types
template<typename T>
struct function_traits : function_traits<decltype(&std::decay_t<T>::operator())> {};

// Free function pointer
template<typename R, typename... Args>
struct function_traits<R(*)(Args...)> {
    using return_type = R;
    using args_tuple = std::tuple<std::decay_t<Args>...>;
    static constexpr size_t arity = sizeof...(Args);
};

// Function reference
template<typename R, typename... Args>
struct function_traits<R(&)(Args...)> : function_traits<R(*)(Args...)> {};

// Const member function pointer (standard lambdas)
template<typename C, typename R, typename... Args>
struct function_traits<R(C::*)(Args...) const> {
    using return_type = R;
    using args_tuple = std::tuple<std::decay_t<Args>...>;
    static constexpr size_t arity = sizeof...(Args);
};

// Non-const member function pointer (mutable lambdas)
template<typename C, typename R, typename... Args>
struct function_traits<R(C::*)(Args...)> {
    using return_type = R;
    using args_tuple = std::tuple<std::decay_t<Args>...>;
    static constexpr size_t arity = sizeof...(Args);
};

// Check if a type is callable
template<typename T, typename = void>
struct is_callable : std::false_type {};

template<typename R, typename... Args>
struct is_callable<R(*)(Args...)> : std::true_type {};

template<typename R, typename... Args>
struct is_callable<R(&)(Args...)> : std::true_type {};

template<typename T>
struct is_callable<T, std::void_t<decltype(&std::decay_t<T>::operator())>> : std::true_type {};

template<typename T>
inline constexpr bool is_callable_v = is_callable<std::decay_t<T>>::value;

// Forward declaration for callable registration
template<typename F>
void push_callable(lua_State* L, F&& func);

} // namespace detail

// ============================================================================
// ValueRef: RAII Registry Reference
// ============================================================================

class ValueRef {
public:
    ValueRef() : L_(nullptr), ref_(LUA_NOREF) {}
    
    ValueRef(lua_State* L, int idx) : L_(L) {
        lua_pushvalue(L, idx);
        ref_ = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    
    ~ValueRef() {
        reset();
    }

    ValueRef(const ValueRef& other) : L_(other.L_) {
        if (other.isValid()) {
            other.push();
            ref_ = luaL_ref(L_, LUA_REGISTRYINDEX);
        } else {
            ref_ = LUA_NOREF;
        }
    }

    ValueRef& operator=(const ValueRef& other) {
        if (this != &other) {
            reset();
            L_ = other.L_;
            if (other.isValid()) {
                other.push();
                ref_ = luaL_ref(L_, LUA_REGISTRYINDEX);
            }
        }
        return *this;
    }

    ValueRef(ValueRef&& other) noexcept : L_(other.L_), ref_(other.ref_) {
        other.L_ = nullptr;
        other.ref_ = LUA_NOREF;
    }

    ValueRef& operator=(ValueRef&& other) noexcept {
        if (this != &other) {
            reset();
            L_ = other.L_;
            ref_ = other.ref_;
            other.L_ = nullptr;
            other.ref_ = LUA_NOREF;
        }
        return *this;
    }

    void reset() {
        if (L_ && ref_ != LUA_NOREF && ref_ != LUA_REFNIL) {
            luaL_unref(L_, LUA_REGISTRYINDEX, ref_);
            ref_ = LUA_NOREF;
        }
    }

    bool isValid() const noexcept {
        return L_ != nullptr && ref_ != LUA_NOREF && ref_ != LUA_REFNIL;
    }

    void push() const {
        if (isValid()) {
            lua_rawgeti(L_, LUA_REGISTRYINDEX, ref_);
        } else if (L_) {
            lua_pushnil(L_);
        }
    }

    int ref() const noexcept { return ref_; }
    lua_State* state() const noexcept { return L_; }

private:
    lua_State* L_;
    int ref_;
};

// ============================================================================
// Stack: Type Marshaling between C++ and Lua Stack
// ============================================================================

struct Stack {
    static Type type(lua_State* L, int idx) {
        return static_cast<Type>(lua_type(L, idx));
    }

    static bool is_nil(lua_State* L, int idx) {
        return lua_isnil(L, idx);
    }

    // ------------------------------------------------------------------------
    // PUSH OVERLOADS
    // ------------------------------------------------------------------------

    static void push(lua_State* L, std::nullptr_t) {
        lua_pushnil(L);
    }

    static void push(lua_State* L, bool val) {
        lua_pushboolean(L, val ? 1 : 0);
    }

    template<typename T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>, int> = 0>
    static void push(lua_State* L, T val) {
        lua_pushinteger(L, static_cast<lua_Integer>(val));
    }

    template<typename T, std::enable_if_t<std::is_floating_point_v<T>, int> = 0>
    static void push(lua_State* L, T val) {
        lua_pushnumber(L, static_cast<lua_Number>(val));
    }

    static void push(lua_State* L, const char* str) {
        if (str) {
            lua_pushstring(L, str);
        } else {
            lua_pushnil(L);
        }
    }

    static void push(lua_State* L, const std::string& str) {
        lua_pushlstring(L, str.data(), str.size());
    }

    static void push(lua_State* L, std::string_view str) {
        lua_pushlstring(L, str.data(), str.size());
    }

    static void push(lua_State* L, lua_CFunction fn) {
        lua_pushcfunction(L, fn);
    }

    static void push(lua_State* L, const ValueRef& ref) {
        (void)L;
        ref.push();
    }

    static void push(lua_State* L, const Table& t);

    template<typename T>
    static void push(lua_State* L, const std::optional<T>& opt) {
        if (opt.has_value()) {
            push(L, *opt);
        } else {
            lua_pushnil(L);
        }
    }

    template<typename T>
    static void push(lua_State* L, const std::vector<T>& vec) {
        lua_createtable(L, static_cast<int>(vec.size()), 0);
        for (size_t i = 0; i < vec.size(); ++i) {
            push(L, vec[i]);
            lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
        }
    }

    template<typename K, typename V>
    static void push(lua_State* L, const std::map<K, V>& m) {
        lua_createtable(L, 0, static_cast<int>(m.size()));
        for (const auto& [k, v] : m) {
            push(L, k);
            push(L, v);
            lua_settable(L, -3);
        }
    }

    template<typename K, typename V>
    static void push(lua_State* L, const std::unordered_map<K, V>& m) {
        lua_createtable(L, 0, static_cast<int>(m.size()));
        for (const auto& [k, v] : m) {
            push(L, k);
            push(L, v);
            lua_settable(L, -3);
        }
    }

    template<typename K, typename V>
    static void push(lua_State* L, const std::pair<K, V>& p) {
        lua_createtable(L, 2, 0);
        push(L, p.first);
        lua_rawseti(L, -2, 1);
        push(L, p.second);
        lua_rawseti(L, -2, 2);
    }

    template<typename... Ts, size_t... Is>
    static int push_tuple_impl(lua_State* L, const std::tuple<Ts...>& tup, std::index_sequence<Is...>) {
        (push(L, std::get<Is>(tup)), ...);
        return static_cast<int>(sizeof...(Ts));
    }

    template<typename... Ts>
    static int push_tuple(lua_State* L, const std::tuple<Ts...>& tup) {
        return push_tuple_impl(L, tup, std::index_sequence_for<Ts...>{});
    }

    // ------------------------------------------------------------------------
    // GET OVERLOADS (Declared here, defined after Table is complete)
    // ------------------------------------------------------------------------

    template<typename T>
    static T get(lua_State* L, int idx);

    template<typename T>
    static T get_custom(lua_State* L, int idx);

    template<typename Tuple, size_t... Is>
    static Tuple get_tuple_impl(lua_State* L, int start_idx, std::index_sequence<Is...>);

    template<typename Tuple>
    static Tuple get_tuple(lua_State* L, int start_idx);

private:
    template<typename T>
    struct is_optional : std::false_type {};
    template<typename T>
    struct is_optional<std::optional<T>> : std::true_type {};
    template<typename T>
    static constexpr bool is_optional_v = is_optional<T>::value;

    template<typename T>
    struct is_vector : std::false_type {};
    template<typename T, typename A>
    struct is_vector<std::vector<T, A>> : std::true_type {};
    template<typename T>
    static constexpr bool is_vector_v = is_vector<T>::value;

    template<typename T>
    struct is_map : std::false_type {};
    template<typename K, typename V, typename C, typename A>
    struct is_map<std::map<K, V, C, A>> : std::true_type {};
    template<typename K, typename V, typename H, typename P, typename A>
    struct is_map<std::unordered_map<K, V, H, P, A>> : std::true_type {};
    template<typename T>
    static constexpr bool is_map_v = is_map<T>::value;
};

// ============================================================================
// Table Proxy & Table Objects
// ============================================================================

class TableProxy {
public:
    TableProxy(lua_State* L, ValueRef tableRef, std::string key)
        : L_(L), tableRef_(std::move(tableRef)), key_(std::move(key)), isIntKey_(false), intKey_(0) {}

    TableProxy(lua_State* L, ValueRef tableRef, int64_t key)
        : L_(L), tableRef_(std::move(tableRef)), isIntKey_(true), intKey_(key) {}

    template<typename T>
    operator T() const {
        return as<T>();
    }

    template<typename T>
    T as() const {
        tableRef_.push();
        if (isIntKey_) {
            lua_geti(L_, -1, static_cast<lua_Integer>(intKey_));
        } else {
            lua_getfield(L_, -1, key_.c_str());
        }
        T val = Stack::get<T>(L_, -1);
        lua_pop(L_, 2);
        return val;
    }

    template<typename V>
    TableProxy& operator=(V&& val);

private:
    lua_State* L_;
    ValueRef tableRef_;
    std::string key_;
    bool isIntKey_;
    int64_t intKey_;
};

class Table {
public:
    Table() = default;
    explicit Table(ValueRef ref) : ref_(std::move(ref)) {}
    Table(lua_State* L, int idx) : ref_(L, idx) {}

    lua_State* state() const { return ref_.state(); }

    template<typename T>
    T get(const std::string& key) const {
        lua_State* L = ref_.state();
        ref_.push();
        lua_getfield(L, -1, key.c_str());
        T val = Stack::get<T>(L, -1);
        lua_pop(L, 2);
        return val;
    }

    template<typename T>
    T get(int64_t index) const {
        lua_State* L = ref_.state();
        ref_.push();
        lua_geti(L, -1, static_cast<lua_Integer>(index));
        T val = Stack::get<T>(L, -1);
        lua_pop(L, 2);
        return val;
    }

    template<typename V>
    void set(const std::string& key, V&& val);

    template<typename V>
    void set(int64_t index, V&& val);

    size_t size() const {
        lua_State* L = ref_.state();
        ref_.push();
        size_t len = lua_rawlen(L, -1);
        lua_pop(L, 1);
        return len;
    }

    bool has(const std::string& key) const {
        lua_State* L = ref_.state();
        ref_.push();
        lua_getfield(L, -1, key.c_str());
        bool exists = !lua_isnil(L, -1);
        lua_pop(L, 2);
        return exists;
    }

    TableProxy operator[](const std::string& key) {
        return TableProxy(ref_.state(), ref_, key);
    }

    TableProxy operator[](int64_t index) {
        return TableProxy(ref_.state(), ref_, index);
    }

    void push() const { ref_.push(); }
    const ValueRef& valueRef() const { return ref_; }

private:
    ValueRef ref_;
};

inline void Stack::push(lua_State* L, const Table& t) {
    (void)L;
    t.push();
}

// ----------------------------------------------------------------------------
// Stack::get Implementation (Table is now complete)
// ----------------------------------------------------------------------------

template<typename T>
T Stack::get(lua_State* L, int idx) {
    using Raw = std::decay_t<T>;
    if constexpr (std::is_same_v<Raw, bool>) {
        return lua_toboolean(L, idx) != 0;
    } else if constexpr (std::is_integral_v<Raw>) {
        int isnum = 0;
        lua_Integer val = lua_tointegerx(L, idx, &isnum);
        if (!isnum && !lua_isnumber(L, idx)) {
            throw RuntimeError("expected integer at stack index " + std::to_string(idx) +
                               ", got " + lua_typename(L, lua_type(L, idx)));
        }
        return static_cast<Raw>(val);
    } else if constexpr (std::is_floating_point_v<Raw>) {
        int isnum = 0;
        lua_Number val = lua_tonumberx(L, idx, &isnum);
        if (!isnum) {
            throw RuntimeError("expected number at stack index " + std::to_string(idx) +
                               ", got " + lua_typename(L, lua_type(L, idx)));
        }
        return static_cast<Raw>(val);
    } else if constexpr (std::is_same_v<Raw, std::string>) {
        size_t len = 0;
        const char* s = lua_tolstring(L, idx, &len);
        if (!s) {
            throw RuntimeError("expected string at stack index " + std::to_string(idx) +
                               ", got " + lua_typename(L, lua_type(L, idx)));
        }
        return std::string(s, len);
    } else if constexpr (std::is_same_v<Raw, std::string_view>) {
        size_t len = 0;
        const char* s = lua_tolstring(L, idx, &len);
        if (!s) {
            throw RuntimeError("expected string at stack index " + std::to_string(idx) +
                               ", got " + lua_typename(L, lua_type(L, idx)));
        }
        return std::string_view(s, len);
    } else if constexpr (std::is_same_v<Raw, ValueRef>) {
        return ValueRef(L, idx);
    } else if constexpr (std::is_same_v<Raw, Table>) {
        return Table(L, idx);
    } else if constexpr (detail::is_tuple_v<Raw>) {
        return get_tuple<Raw>(L, idx);
    } else {
        return get_custom<Raw>(L, idx);
    }
}

template<typename T>
T Stack::get_custom(lua_State* L, int idx) {
    using Raw = std::decay_t<T>;
    // Optional
    if constexpr (is_optional_v<Raw>) {
        if (lua_isnil(L, idx) || lua_isnone(L, idx)) {
            return std::nullopt;
        }
        return get<typename Raw::value_type>(L, idx);
    }
    // Vector
    else if constexpr (is_vector_v<Raw>) {
        int abs_idx = lua_absindex(L, idx);
        if (!lua_istable(L, abs_idx)) {
            throw RuntimeError("expected table for std::vector conversion, got " +
                               std::string(lua_typename(L, lua_type(L, abs_idx))));
        }
        size_t len = lua_rawlen(L, abs_idx);
        Raw result;
        result.reserve(len);
        using Element = typename Raw::value_type;
        for (size_t i = 1; i <= len; ++i) {
            lua_rawgeti(L, abs_idx, static_cast<lua_Integer>(i));
            result.push_back(get<Element>(L, -1));
            lua_pop(L, 1);
        }
        return result;
    }
    // Map
    else if constexpr (is_map_v<Raw>) {
        int abs_idx = lua_absindex(L, idx);
        if (!lua_istable(L, abs_idx)) {
            throw RuntimeError("expected table for std::map conversion, got " +
                               std::string(lua_typename(L, lua_type(L, abs_idx))));
        }
        Raw result;
        using KeyType = typename Raw::key_type;
        using MappedType = typename Raw::mapped_type;
        lua_pushnil(L);
        while (lua_next(L, abs_idx) != 0) {
            KeyType k = get<KeyType>(L, -2);
            MappedType v = get<MappedType>(L, -1);
            result.emplace(std::move(k), std::move(v));
            lua_pop(L, 1); // pop value, keep key for next
        }
        return result;
    } else {
        static_assert(sizeof(Raw) == 0, "Unsupported type for lua::Stack::get");
    }
}

template<typename Tuple, size_t... Is>
Tuple Stack::get_tuple_impl(lua_State* L, int start_idx, std::index_sequence<Is...>) {
    return Tuple(get<std::tuple_element_t<Is, Tuple>>(L, start_idx + static_cast<int>(Is))...);
}

template<typename Tuple>
Tuple Stack::get_tuple(lua_State* L, int start_idx) {
    return get_tuple_impl<Tuple>(L, start_idx, std::make_index_sequence<std::tuple_size_v<Tuple>>{});
}

// ============================================================================
// Callable Dispatcher
// ============================================================================

namespace detail {

template<typename F, size_t... Is>
int invoke_callable_helper(lua_State* L, F& func, std::index_sequence<Is...>) {
    using traits = function_traits<F>;
    using return_type = typename traits::return_type;
    using args_tuple = typename traits::args_tuple;

    int top = lua_gettop(L);
    constexpr size_t num_args = sizeof...(Is);
    if (top < static_cast<int>(num_args)) {
        return luaL_error(L, "expected at least %d argument(s), got %d", (int)num_args, top);
    }

    if constexpr (std::is_void_v<return_type>) {
        func(Stack::get<std::tuple_element_t<Is, args_tuple>>(L, static_cast<int>(Is + 1))...);
        return 0;
    } else if constexpr (is_tuple_v<return_type>) {
        return_type res = func(Stack::get<std::tuple_element_t<Is, args_tuple>>(L, static_cast<int>(Is + 1))...);
        return Stack::push_tuple(L, res);
    } else {
        return_type res = func(Stack::get<std::tuple_element_t<Is, args_tuple>>(L, static_cast<int>(Is + 1))...);
        Stack::push(L, res);
        return 1;
    }
}

template<typename F>
void push_callable(lua_State* L, F&& func) {
    using DecayedF = std::decay_t<F>;
    void* mem = lua_newuserdatauv(L, sizeof(DecayedF), 0);
    new (mem) DecayedF(std::forward<F>(func));

    // Metatable with __gc finalizer for safe memory destruction
    const char* mt_name = typeid(DecayedF).name();
    if (luaL_newmetatable(L, mt_name)) {
        lua_pushcfunction(L, [](lua_State* L) -> int {
            void* p = lua_touserdata(L, 1);
            if (p) {
                static_cast<DecayedF*>(p)->~DecayedF();
            }
            return 0;
        });
        lua_setfield(L, -2, "__gc");
    }
    lua_setmetatable(L, -2);

    // C closure with userdata as upvalue 1
    lua_pushcclosure(L, [](lua_State* L) -> int {
        void* p = lua_touserdata(L, lua_upvalueindex(1));
        if (!p) {
            return luaL_error(L, "internal error: missing callable userdata");
        }
        DecayedF* f = static_cast<DecayedF*>(p);
        try {
            using traits = function_traits<DecayedF>;
            return invoke_callable_helper(L, *f, std::make_index_sequence<traits::arity>{});
        } catch (const std::exception& e) {
            return luaL_error(L, "%s", e.what());
        } catch (...) {
            return luaL_error(L, "unknown C++ exception in callback");
        }
    }, 1);
}

} // namespace detail

template<typename V>
TableProxy& TableProxy::operator=(V&& val) {
    tableRef_.push();
    if constexpr (detail::is_callable_v<V>) {
        detail::push_callable(L_, std::forward<V>(val));
    } else {
        Stack::push(L_, std::forward<V>(val));
    }
    if (isIntKey_) {
        lua_seti(L_, -2, static_cast<lua_Integer>(intKey_));
    } else {
        lua_setfield(L_, -2, key_.c_str());
    }
    lua_pop(L_, 1);
    return *this;
}

template<typename V>
void Table::set(const std::string& key, V&& val) {
    lua_State* L = ref_.state();
    ref_.push();
    if constexpr (detail::is_callable_v<V>) {
        detail::push_callable(L, std::forward<V>(val));
    } else {
        Stack::push(L, std::forward<V>(val));
    }
    lua_setfield(L, -2, key.c_str());
    lua_pop(L, 1);
}

template<typename V>
void Table::set(int64_t index, V&& val) {
    lua_State* L = ref_.state();
    ref_.push();
    if constexpr (detail::is_callable_v<V>) {
        detail::push_callable(L, std::forward<V>(val));
    } else {
        Stack::push(L, std::forward<V>(val));
    }
    lua_seti(L, -2, static_cast<lua_Integer>(index));
    lua_pop(L, 1);
}

// ============================================================================
// CallResult Proxy for Global Calling
// ============================================================================

class CallResult {
public:
    explicit CallResult(lua_State* L) : L_(L), popped_(false) {}
    ~CallResult() {
        if (!popped_ && L_) {
            lua_pop(L_, 1);
        }
    }

    template<typename T>
    operator T() && {
        popped_ = true;
        T val = Stack::get<T>(L_, -1);
        lua_pop(L_, 1);
        return val;
    }

    template<typename T>
    T as() && {
        popped_ = true;
        T val = Stack::get<T>(L_, -1);
        lua_pop(L_, 1);
        return val;
    }

private:
    lua_State* L_;
    bool popped_;
};

// ============================================================================
// Global Proxy
// ============================================================================

class GlobalProxy {
public:
    GlobalProxy(lua_State* L, std::string name) : L_(L), name_(std::move(name)) {}

    template<typename T>
    operator T() const {
        return as<T>();
    }

    template<typename T>
    T as() const {
        lua_getglobal(L_, name_.c_str());
        T val = Stack::get<T>(L_, -1);
        lua_pop(L_, 1);
        return val;
    }

    template<typename T>
    GlobalProxy& operator=(T&& val) {
        if constexpr (detail::is_callable_v<T>) {
            detail::push_callable(L_, std::forward<T>(val));
            lua_setglobal(L_, name_.c_str());
        } else {
            Stack::push(L_, std::forward<T>(val));
            lua_setglobal(L_, name_.c_str());
        }
        return *this;
    }

    template<typename... Args>
    CallResult operator()(Args&&... args) const {
        lua_getglobal(L_, name_.c_str());
        if (!lua_isfunction(L_, -1)) {
            lua_pop(L_, 1);
            throw RuntimeError("attempt to call non-function global '" + name_ + "'");
        }
        (Stack::push(L_, std::forward<Args>(args)), ...);
        constexpr int num_args = sizeof...(Args);
        int status = lua_pcall(L_, num_args, 1, 0);
        if (status != LUA_OK) {
            const char* err = lua_tostring(L_, -1);
            std::string msg = err ? err : "unknown error";
            lua_pop(L_, 1);
            throw RuntimeError(msg);
        }
        return CallResult(L_);
    }

private:
    lua_State* L_;
    std::string name_;
};

// ============================================================================
// Context: Modern RAII Lua State Wrapper
// ============================================================================

class Context {
public:
    Context() : Context(true) {}

    explicit Context(bool open_libs) : L_(luaL_newstate()), owns_(true) {
        if (!L_) {
            throw Error("failed to allocate Lua state");
        }
        if (open_libs) {
            luaL_openlibs(L_);
        }
    }

    explicit Context(lua_State* L, bool take_ownership = false)
        : L_(L), owns_(take_ownership) {}

    ~Context() {
        close();
    }

    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    Context(Context&& other) noexcept : L_(other.L_), owns_(other.owns_) {
        other.L_ = nullptr;
        other.owns_ = false;
    }

    Context& operator=(Context&& other) noexcept {
        if (this != &other) {
            close();
            L_ = other.L_;
            owns_ = other.owns_;
            other.L_ = nullptr;
            other.owns_ = false;
        }
        return *this;
    }

    void close() {
        if (L_ && owns_) {
            lua_close(L_);
            L_ = nullptr;
            owns_ = false;
        }
    }

    lua_State* state() const noexcept { return L_; }
    lua_State* native_handle() const noexcept { return L_; }
    bool isValid() const noexcept { return L_ != nullptr; }

    void openLibs() {
        if (L_) luaL_openlibs(L_);
    }

    void execute(const std::string& code, const std::string& name = "chunk") {
        run(code, name);
    }

    void loadFile(const std::string& filename) {
        run_file(filename);
    }

    void run(const std::string& code, const std::string& name = "chunk") {
        int status = luaL_loadbuffer(L_, code.data(), code.size(), name.c_str());
        if (status != LUA_OK) {
            const char* err = lua_tostring(L_, -1);
            std::string msg = err ? err : "syntax error";
            lua_pop(L_, 1);
            throw SyntaxError(msg);
        }
        status = lua_pcall(L_, 0, LUA_MULTRET, 0);
        if (status != LUA_OK) {
            const char* err = lua_tostring(L_, -1);
            std::string msg = err ? err : "runtime error";
            lua_pop(L_, 1);
            throw RuntimeError(msg);
        }
    }

    void run_file(const std::string& filename) {
        int status = luaL_loadfile(L_, filename.c_str());
        if (status != LUA_OK) {
            const char* err = lua_tostring(L_, -1);
            std::string msg = err ? err : "file load error";
            lua_pop(L_, 1);
            throw (status == LUA_ERRSYNTAX ? SyntaxError(msg) : Error(msg, status));
        }
        status = lua_pcall(L_, 0, LUA_MULTRET, 0);
        if (status != LUA_OK) {
            const char* err = lua_tostring(L_, -1);
            std::string msg = err ? err : "runtime error in file";
            lua_pop(L_, 1);
            throw RuntimeError(msg);
        }
    }

    template<typename T = void>
    T eval(const std::string& expr) {
        std::string code = "return " + expr;
        int status = luaL_loadbuffer(L_, code.data(), code.size(), "eval");
        if (status != LUA_OK) {
            lua_pop(L_, 1);
            status = luaL_loadbuffer(L_, expr.data(), expr.size(), "eval");
            if (status != LUA_OK) {
                const char* err = lua_tostring(L_, -1);
                std::string msg = err ? err : "syntax error in eval";
                lua_pop(L_, 1);
                throw SyntaxError(msg);
            }
        }

        int pre_top = lua_gettop(L_) - 1; // before chunk was pushed
        status = lua_pcall(L_, 0, LUA_MULTRET, 0);
        if (status != LUA_OK) {
            const char* err = lua_tostring(L_, -1);
            std::string msg = err ? err : "runtime error in eval";
            lua_pop(L_, 1);
            throw RuntimeError(msg);
        }

        if constexpr (std::is_void_v<T>) {
            lua_settop(L_, pre_top);
            return;
        } else if constexpr (detail::is_tuple_v<T>) {
            T result = Stack::get_tuple<T>(L_, pre_top + 1);
            lua_settop(L_, pre_top);
            return result;
        } else {
            int post_top = lua_gettop(L_);
            if (post_top <= pre_top) {
                throw RuntimeError("eval expected a return value, but nothing was returned");
            }
            T result = Stack::get<T>(L_, -1);
            lua_settop(L_, pre_top);
            return result;
        }
    }

    template<typename T>
    T get(const std::string& name) const {
        lua_getglobal(L_, name.c_str());
        T val = Stack::get<T>(L_, -1);
        lua_pop(L_, 1);
        return val;
    }

    template<typename T>
    void set(const std::string& name, T&& value) {
        if constexpr (detail::is_callable_v<T>) {
            bind(name, std::forward<T>(value));
        } else {
            Stack::push(L_, std::forward<T>(value));
            lua_setglobal(L_, name.c_str());
        }
    }

    template<typename F>
    void bind(const std::string& name, F&& func) {
        detail::push_callable(L_, std::forward<F>(func));
        lua_setglobal(L_, name.c_str());
    }

    template<typename Ret = void, typename... Args>
    Ret call(const std::string& name, Args&&... args) {
        lua_getglobal(L_, name.c_str());
        if (!lua_isfunction(L_, -1)) {
            lua_pop(L_, 1);
            throw RuntimeError("attempt to call non-function global '" + name + "'");
        }
        int pre_top = lua_gettop(L_) - 1;
        (Stack::push(L_, std::forward<Args>(args)), ...);
        constexpr int num_args = sizeof...(Args);
        constexpr int num_ret = std::is_void_v<Ret> ? 0 : static_cast<int>(detail::tuple_or_single_size_v<Ret>);
        int status = lua_pcall(L_, num_args, num_ret, 0);
        if (status != LUA_OK) {
            const char* err = lua_tostring(L_, -1);
            std::string msg = err ? err : "runtime error in function call";
            lua_pop(L_, 1);
            throw RuntimeError(msg);
        }

        if constexpr (std::is_void_v<Ret>) {
            lua_settop(L_, pre_top);
            return;
        } else if constexpr (detail::is_tuple_v<Ret>) {
            Ret result = Stack::get_tuple<Ret>(L_, pre_top + 1);
            lua_settop(L_, pre_top);
            return result;
        } else {
            Ret result = Stack::get<Ret>(L_, -1);
            lua_settop(L_, pre_top);
            return result;
        }
    }

    Table create_table(int narr = 0, int nrec = 0) {
        lua_createtable(L_, narr, nrec);
        Table t(L_, -1);
        lua_pop(L_, 1);
        return t;
    }

    GlobalProxy operator[](const std::string& name) {
        return GlobalProxy(L_, name);
    }

private:
    lua_State* L_;
    bool owns_;
};

using State = Context;

} // namespace lua

#endif // LUA_MODERN_CPP_HPP
