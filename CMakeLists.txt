cmake_minimum_required(VERSION 3.10)
project(LuaVM VERSION 5.5.0 LANGUAGES C CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# RPATH configuration for relocatable installed targets
if(APPLE)
    set(CMAKE_INSTALL_RPATH "@loader_path/../${CMAKE_INSTALL_LIBDIR}")
else()
    set(CMAKE_INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
endif()
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

# Set C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Debug flags
option(DEBUG "Enable general debug output" OFF)
option(DEBUG_LOG_GC "Enable garbage collector logging" OFF)
option(DEBUG_TRACE_EXECUTION "Enable VM execution tracing" OFF)
option(DEBUG_PRINT_CODE "Enable bytecode disassembly" OFF)
option(USE_JIT "Enable JIT compilation" OFF)

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

# Linenoise for REPL
include(FetchContent)
FetchContent_Declare(
    linenoise
    GIT_REPOSITORY https://github.com/antirez/linenoise.git
    GIT_TAG master
)
FetchContent_MakeAvailable(linenoise)

# We need to manually create the target because linenoise doesn't have a CMakeLists.txt
if(NOT TARGET linenoise)
    add_library(linenoise STATIC ${linenoise_SOURCE_DIR}/linenoise.c)
    target_include_directories(linenoise PUBLIC ${linenoise_SOURCE_DIR})
    set_target_properties(linenoise PROPERTIES LINKER_LANGUAGE C)
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
    src/vm/jit_x64.cpp
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
    src/vm/stdlib_utf8.cpp
    src/api/lua_api.cpp
    src/value/socket.cpp
    src/lsp/analysis.cpp
    src/lsp/server.cpp
)

# Core sources for reuse in tests
set(CORE_SOURCES ${SOURCES})
list(REMOVE_ITEM CORE_SOURCES src/main.cpp)

# Create executable
add_executable(lua src/main.cpp ${CORE_SOURCES})

# Target-specific compile options
if(MSVC)
    target_compile_options(lua PRIVATE /W4 /WX)
else()
    target_compile_options(lua PRIVATE -Wall -Wextra -Wpedantic -Werror -Wno-gnu-anonymous-struct -Wno-nested-anon-types)
endif()

if(USE_JIT)
    target_link_libraries(lua PRIVATE asmjit::asmjit)
endif()
target_link_libraries(lua PRIVATE linenoise)

# Include directories
target_include_directories(lua PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/include)

# Set output directory and enable symbol exports for dynamic modules
set_target_properties(lua PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    ENABLE_EXPORTS ON
)

# Create luac executable
add_executable(luac src/luac.cpp ${CORE_SOURCES})

if(MSVC)
    target_compile_options(luac PRIVATE /W4 /WX)
else()
    target_compile_options(luac PRIVATE -Wall -Wextra -Wpedantic -Werror -Wno-gnu-anonymous-struct -Wno-nested-anon-types)
endif()

if(USE_JIT)
    target_link_libraries(luac PRIVATE asmjit::asmjit)
endif()

target_include_directories(luac PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/include)
set_target_properties(luac PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# Create lua-lsp executable
add_executable(lua-lsp src/lsp/main.cpp ${CORE_SOURCES})

if(MSVC)
    target_compile_options(lua-lsp PRIVATE /W4 /WX)
else()
    target_compile_options(lua-lsp PRIVATE -Wall -Wextra -Wpedantic -Werror -Wno-gnu-anonymous-struct -Wno-nested-anon-types)
endif()

if(USE_JIT)
    target_link_libraries(lua-lsp PRIVATE asmjit::asmjit)
endif()

target_include_directories(lua-lsp PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/include)
set_target_properties(lua-lsp PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)


# Build liblua shared library for external C modules and Luarocks
add_library(liblua SHARED ${CORE_SOURCES})
set_target_properties(liblua PROPERTIES
    OUTPUT_NAME lua
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
target_include_directories(liblua PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
if(USE_JIT)
    target_link_libraries(liblua PRIVATE asmjit::asmjit)
endif()

# Dummy shared library for loadlib testing
add_library(dummy_module SHARED tests/dummy_module.cpp)
target_include_directories(dummy_module PRIVATE ${CMAKE_SOURCE_DIR}/src)

# C API Test Executable
add_executable(test_c_api tests/test_c_api.cpp ${CORE_SOURCES})
target_compile_options(test_c_api PRIVATE -Wall -Wextra -Wpedantic -Wno-gnu-anonymous-struct -Wno-nested-anon-types)
target_include_directories(test_c_api PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/include)
if(USE_JIT)
    target_link_libraries(test_c_api PRIVATE asmjit::asmjit)
endif()
set_target_properties(test_c_api PROPERTIES 
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    ENABLE_EXPORTS ON
)

# Modern C++ Embedding API Test Executable
add_executable(test_cpp_api tests/test_cpp_api.cpp ${CORE_SOURCES})
target_compile_options(test_cpp_api PRIVATE -Wall -Wextra -Wpedantic -Wno-gnu-anonymous-struct -Wno-nested-anon-types)
target_include_directories(test_cpp_api PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/include)
if(USE_JIT)
    target_link_libraries(test_cpp_api PRIVATE asmjit::asmjit)
endif()
set_target_properties(test_cpp_api PROPERTIES 
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    ENABLE_EXPORTS ON
)

# x86_64 JIT Test Executable
add_executable(test_jit_x64 tests/test_jit_x64.cpp ${CORE_SOURCES})
target_compile_options(test_jit_x64 PRIVATE -Wall -Wextra -Wpedantic -Wno-gnu-anonymous-struct -Wno-nested-anon-types)
target_include_directories(test_jit_x64 PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/include)
if(USE_JIT)
    target_link_libraries(test_jit_x64 PRIVATE asmjit::asmjit)
endif()
set_target_properties(test_jit_x64 PROPERTIES 
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    ENABLE_EXPORTS ON
)

# Modern C++ Embedding Demo Executable
add_executable(embedding_demo examples/embedding_demo.cpp ${CORE_SOURCES})
target_compile_options(embedding_demo PRIVATE -Wall -Wextra -Wpedantic -Wno-gnu-anonymous-struct -Wno-nested-anon-types)
target_include_directories(embedding_demo PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/include)
if(USE_JIT)
    target_link_libraries(embedding_demo PRIVATE asmjit::asmjit)
endif()
set_target_properties(embedding_demo PROPERTIES 
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    ENABLE_EXPORTS ON
)

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

# Fixture modules for ecosystem compatibility tests
add_library(lfs MODULE tests/fixtures/lfs/lfs.c)
target_include_directories(lfs PRIVATE ${CMAKE_SOURCE_DIR}/src/api)
set_target_properties(lfs PROPERTIES
    PREFIX ""
    OUTPUT_NAME lfs
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tests/fixtures/lfs
)
if(APPLE)
    set_target_properties(lfs PROPERTIES LINK_FLAGS "-undefined dynamic_lookup")
endif()

add_library(cjson MODULE 
    tests/fixtures/cjson/lua_cjson.c 
    tests/fixtures/cjson/strbuf.c 
    tests/fixtures/cjson/fpconv.c
)
target_include_directories(cjson PRIVATE ${CMAKE_SOURCE_DIR}/src/api)
set_target_properties(cjson PROPERTIES
    PREFIX ""
    OUTPUT_NAME cjson
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tests/fixtures/cjson
)
if(APPLE)
    set_target_properties(cjson PROPERTIES LINK_FLAGS "-undefined dynamic_lookup")
endif()

# Custom test target
add_custom_target(test
    COMMAND ./run_all_tests.sh ../build/lua
    COMMAND ../build/test_c_api
    COMMAND ../build/test_cpp_api
    COMMAND ../build/test_jit_x64
    COMMAND ./test_luac.sh ../build/lua ../build/luac
    COMMAND ./test_lsp.sh ../build/lua ../build/lua-lsp
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/tests
    DEPENDS lua luac lua-lsp test_c_api test_cpp_api test_jit_x64 embedding_demo dummy_module lfs cjson
    COMMENT "Running all Lua VM unit tests, API tests, JIT tests, luac tests, and LSP tests"
)

# Custom benchmark target
add_custom_target(benchmark
    COMMAND bash "${CMAKE_SOURCE_DIR}/benchmarks/run.sh" "$<TARGET_FILE:lua>"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/benchmarks
    DEPENDS lua
    COMMENT "Running Lua VM performance benchmark suite"
)

# ==============================================================================
# Installation and Packaging Rules
# ==============================================================================

# Install binaries and shared library
install(TARGETS lua luac lua-lsp liblua
    EXPORT LuaTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Install C API and standard headers
install(FILES
    src/api/lua.h
    src/api/lauxlib.h
    src/api/lualib.h
    src/api/luaconf.h
    include/lua.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Install Modern C++ Embedding API header
install(FILES
    include/lua/lua.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/lua
)

# Configure and install pkg-config file
configure_file(
    ${CMAKE_SOURCE_DIR}/lua.pc.in
    ${CMAKE_BINARY_DIR}/lua.pc
    @ONLY
)
install(FILES
    ${CMAKE_BINARY_DIR}/lua.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)

# CMake Package Configuration (find_package support)
configure_package_config_file(
    ${CMAKE_SOURCE_DIR}/cmake/LuaConfig.cmake.in
    ${CMAKE_BINARY_DIR}/cmake/LuaConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Lua
)
write_basic_package_version_file(
    ${CMAKE_BINARY_DIR}/cmake/LuaConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_BINARY_DIR}/cmake/LuaConfig.cmake
    ${CMAKE_BINARY_DIR}/cmake/LuaConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Lua
)

install(EXPORT LuaTargets
    FILE LuaTargets.cmake
    NAMESPACE Lua::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Lua
)


