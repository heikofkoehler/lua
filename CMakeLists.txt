cmake_minimum_required(VERSION 3.10)
project(LuaVM VERSION 1.0 LANGUAGES CXX)

# Set C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable warnings
if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# Debug flags
option(DEBUG_TRACE_EXECUTION "Enable VM execution tracing" OFF)
option(DEBUG_PRINT_CODE "Enable bytecode disassembly" OFF)

if(DEBUG_TRACE_EXECUTION)
    add_compile_definitions(DEBUG_TRACE_EXECUTION)
endif()

if(DEBUG_PRINT_CODE)
    add_compile_definitions(DEBUG_PRINT_CODE)
endif()

# Source files
set(SOURCES
    src/main.cpp
    src/common/common.cpp
    src/value/value.cpp
    src/value/file.cpp
    src/compiler/lexer.cpp
    src/compiler/parser.cpp
    src/compiler/chunk.cpp
    src/compiler/codegen.cpp
    src/vm/vm.cpp
    src/vm/gc.cpp
    src/vm/gc_impl.cpp
    src/vm/stdlib_base.cpp
    src/vm/stdlib_string.cpp
    src/vm/stdlib_table.cpp
    src/vm/stdlib_math.cpp
    src/vm/stdlib_socket.cpp
    src/vm/stdlib_coroutine.cpp
    src/value/socket.cpp
)

# Create executable
add_executable(lua ${SOURCES})

# Include directories
target_include_directories(lua PRIVATE ${CMAKE_SOURCE_DIR}/src)

# Set output directory
set_target_properties(lua PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# Custom test target
add_custom_target(test
    COMMAND ./run_all_tests.sh ../build/lua
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/tests
    DEPENDS lua
    COMMENT "Running all Lua VM tests"
)
