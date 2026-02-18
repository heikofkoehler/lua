#ifndef LUA_FUNCTION_HPP
#define LUA_FUNCTION_HPP

#include "common/common.hpp"
#include "compiler/chunk.hpp"
#include <string>
#include <memory>

// FunctionObject: Represents a compiled Lua function
// Contains the function's bytecode, parameter count, and name

class FunctionObject {
public:
    FunctionObject(const std::string& name, int arity, std::unique_ptr<Chunk> chunk)
        : name_(name), arity_(arity), chunk_(std::move(chunk)) {}

    const std::string& name() const { return name_; }
    int arity() const { return arity_; }
    Chunk* chunk() const { return chunk_.get(); }

private:
    std::string name_;
    int arity_;
    std::unique_ptr<Chunk> chunk_;
};

#endif // LUA_FUNCTION_HPP
