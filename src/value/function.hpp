#ifndef LUA_FUNCTION_HPP
#define LUA_FUNCTION_HPP

#include "common/common.hpp"
#include "compiler/chunk.hpp"
#include <string>
#include <memory>

// FunctionObject: Represents a compiled Lua function
// Contains the function's bytecode, parameter count, and name

struct LocalVarInfo {
    std::string name;
    size_t startPC;
    size_t endPC;
    int slot;
};

class FunctionObject {
public:
    FunctionObject(const std::string& name, int arity, std::unique_ptr<Chunk> chunk,
                   int upvalueCount = 0, bool hasVarargs = false)
        : name_(name), arity_(arity), chunk_(std::move(chunk)),
          upvalueCount_(upvalueCount), hasVarargs_(hasVarargs) {}

    const std::string& name() const { return name_; }
    int arity() const { return arity_; }
    Chunk* chunk() const { return chunk_.get(); }
    int upvalueCount() const { return upvalueCount_; }
    bool hasVarargs() const { return hasVarargs_; }

    const std::vector<LocalVarInfo>& localVars() const { return localVars_; }
    void addLocalVar(const std::string& name, size_t startPC, size_t endPC, int slot) {
        localVars_.push_back({name, startPC, endPC, slot});
    }

    // Serialization
    void serialize(std::ostream& os) const;
    static std::unique_ptr<FunctionObject> deserialize(std::istream& is);

private:
    std::string name_;
    int arity_;
    std::unique_ptr<Chunk> chunk_;
    int upvalueCount_;  // Number of upvalues this function captures
    bool hasVarargs_;   // Whether this function accepts varargs (...)
    std::vector<LocalVarInfo> localVars_;
};

#endif // LUA_FUNCTION_HPP
