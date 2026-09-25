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

class VM;
typedef int64_t (*JITFunc)(VM* vm);

class FunctionObject {
public:
    FunctionObject(const std::string& name, int arity, std::unique_ptr<Chunk> chunk,
                   int upvalueCount = 0, bool hasVarargs = false)
        : name_(name), arity_(arity), chunk_(std::move(chunk)),
          upvalueCount_(upvalueCount), hasVarargs_(hasVarargs),
          hotness_(0), jitCode_(nullptr) {}

    ~FunctionObject() {}

    const std::string& name() const { return name_; }
    int arity() const { return arity_; }
    Chunk* chunk() const { return chunk_.get(); }
    int upvalueCount() const { return upvalueCount_; }
    bool hasVarargs() const { return hasVarargs_; }
    int lineDefined() const { return lineDefined_; }
    int lastLineDefined() const { return lastLineDefined_; }
    void setLines(int first, int last) { lineDefined_ = first; lastLineDefined_ = last; }

    // JIT related
    int incrementHotness() { return ++hotness_; }
    void resetHotness(int value = 0) { hotness_ = value; }
    void setJITCode(JITFunc code) { jitCode_ = code; }
    JITFunc getJITCode() const { return jitCode_; }

    const std::vector<LocalVarInfo>& localVars() const { return localVars_; }
    void addLocalVar(const std::string& name, size_t startPC, size_t endPC, int slot) {
        localVars_.push_back({name, startPC, endPC, slot});
    }

    const std::vector<std::string>& upvalueNames() const { return upvalueNames_; }
    void addUpvalueName(const std::string& name) { upvalueNames_.push_back(name); }
    const std::string& getUpvalueName(size_t index) const {
        static const std::string empty;
        if (index < upvalueNames_.size()) return upvalueNames_[index];
        return empty;
    }

    bool hasNamedVarargs() const { return hasNamedVarargs_; }
    int namedVarargSlot() const { return namedVarargSlot_; }
    bool isVarargOptimized() const { return isVarargOptimized_; }
    void setNamedVarargs(int slot, bool optimized) {
        hasNamedVarargs_ = true;
        namedVarargSlot_ = slot;
        isVarargOptimized_ = optimized;
    }

    void disassemble() const;

    // Serialization
    void serialize(std::ostream& os, const std::string& parentSource = "", bool strip = false) const;
    static std::unique_ptr<FunctionObject> deserialize(std::istream& is, const std::string& parentSource = "");

private:
    std::string name_;
    int arity_;
    std::unique_ptr<Chunk> chunk_;
    int upvalueCount_;  // Number of upvalues this function captures
    bool hasVarargs_;   // Whether this function accepts varargs (...)
    bool hasNamedVarargs_ = false;
    int namedVarargSlot_ = -1;
    bool isVarargOptimized_ = false;
    int lineDefined_ = 0;
    int lastLineDefined_ = 0;
    std::vector<LocalVarInfo> localVars_;
    std::vector<std::string> upvalueNames_;

    // JIT related
    int hotness_;
    JITFunc jitCode_;
};

#endif // LUA_FUNCTION_HPP
