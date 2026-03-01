cmake_minimum_required(VERSION 3.10)
project(LuaVM VERSION 1.0 LANGUAGES CXX)

# Set C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Debug flags
option(DEBUG "Enable general debug output" OFF)
option(DEBUG_LOG_GC "Enable garbage collector logging" OFF)
option(DEBUG_TRACE_EXECUTION "Enable VM execution tracing" OFF)
option(DEBUG_PRINT_CODE "Enable bytecode disassembly" OFF)
option(USE_JIT "Enable JIT compilation" ON)

if(USE_JIT)
    add_compile_definitions(USE_JIT)
    include(FetchContent)
    FetchContent_Declare(
        asmjit
        GIT_REPOSITORY https://github.com/asmjit/asmjit.git
        GIT_TAG master
    )
    FetchContent_MakeAvailable(asmjit)
endif()

if(DEBUG)
    add_compile_definitions(DEBUG)
endif()

if(DEBUG_LOG_GC)
    add_compile_definitions(DEBUG_LOG_GC)
endif()

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
    src/value/table.cpp
    src/value/upvalue.cpp
    src/value/file.cpp
    src/value/closure.cpp
    src/value/userdata.cpp
    src/value/coroutine.cpp
    src/compiler/lexer.cpp
    src/compiler/parser.cpp
    src/compiler/chunk.cpp
    src/compiler/codegen.cpp
    src/vm/vm.cpp
    src/vm/run_impl.cpp
    src/vm/jit.cpp
    src/vm/gc_impl.cpp
    src/vm/stdlib_base.cpp
    src/vm/stdlib_string.cpp
    src/vm/stdlib_table.cpp
    src/vm/stdlib_math.cpp
    src/vm/stdlib_os.cpp
    src/vm/stdlib_io.cpp
    src/vm/stdlib_socket.cpp
    src/vm/stdlib_coroutine.cpp
    src/vm/stdlib_debug.cpp
    src/value/socket.cpp
)

# Create executable
add_executable(lua ${SOURCES})

# Target-specific compile options
if(MSVC)
    target_compile_options(lua PRIVATE /W4 /WX)
else()
    target_compile_options(lua PRIVATE -Wall -Wextra -Wpedantic -Werror -Wno-gnu-anonymous-struct -Wno-nested-anon-types)
endif()

if(USE_JIT)
    target_link_libraries(lua PRIVATE asmjit::asmjit)
endif()

# Include directories
target_include_directories(lua PRIVATE ${CMAKE_SOURCE_DIR}/src)

# Set output directory
set_target_properties(lua PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# Dummy shared library for loadlib testing
add_library(dummy_module SHARED tests/dummy_module.cpp)
target_include_directories(dummy_module PRIVATE ${CMAKE_SOURCE_DIR}/src)

if(MSVC)
    target_compile_options(dummy_module PRIVATE /W4 /WX)
else()
    target_compile_options(dummy_module PRIVATE -Wall -Wextra -Wpedantic -Werror -Wno-gnu-anonymous-struct -Wno-nested-anon-types)
endif()

if(APPLE)
    set_target_properties(dummy_module PROPERTIES LINK_FLAGS "-undefined dynamic_lookup")
endif()

# Don't prefix with "lib" on Unix to match standard Lua extension module naming, and output to build dir
set_target_properties(dummy_module PROPERTIES 
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# Custom test target
add_custom_target(test
    COMMAND ./run_all_tests.sh ../build/lua
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/tests
    DEPENDS lua
    COMMENT "Running all Lua VM tests"
)
