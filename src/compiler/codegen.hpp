#ifndef LUA_CODEGEN_HPP
#define LUA_CODEGEN_HPP

#include "common/common.hpp"
#include "compiler/ast.hpp"
#include "compiler/chunk.hpp"
#include <deque>
#include <memory>

// CodeGenerator: Walks AST and generates bytecode
// Implements visitor pattern to traverse AST nodes

class CodeGenerator : public ASTVisitor {
public:
    CodeGenerator();

    // Generate bytecode from AST
    std::unique_ptr<Chunk> generate(ProgramNode* program);

    // Visitor methods
    void visitLiteral(LiteralNode* node) override;
    void visitStringLiteral(StringLiteralNode* node) override;
    void visitUnary(UnaryNode* node) override;
    void visitBinary(BinaryNode* node) override;
    void visitVariable(VariableExprNode* node) override;
    void visitVararg(VarargExprNode* node) override;
    void visitCall(CallExprNode* node) override;
    void visitMethodCall(MethodCallExprNode* node) override;
    void visitTableConstructor(TableConstructorNode* node) override;
    void visitIndexExpr(IndexExprNode* node) override;
    void visitExprStmt(ExprStmtNode* node) override;
    void visitAssignmentStmt(AssignmentStmtNode* node) override;
    void visitIndexAssignmentStmt(IndexAssignmentStmtNode* node) override;
    void visitLocalDeclStmt(LocalDeclStmtNode* node) override;
    void visitMultipleLocalDeclStmt(MultipleLocalDeclStmtNode* node) override;
    void visitMultipleAssignmentStmt(MultipleAssignmentStmtNode* node) override;
    void visitIfStmt(IfStmtNode* node) override;
    void visitWhileStmt(WhileStmtNode* node) override;
    void visitRepeatStmt(RepeatStmtNode* node) override;
    void visitForStmt(ForStmtNode* node) override;
    void visitForInStmt(ForInStmtNode* node) override;
    void visitFunctionDecl(FunctionDeclNode* node) override;
    void visitFunctionExpr(FunctionExprNode* node) override;
    void visitReturn(ReturnStmtNode* node) override;
    void visitBreak(BreakStmtNode* node) override;
    void visitGoto(GotoStmtNode* node) override;
    void visitLabel(LabelStmtNode* node) override;
    void visitBlock(BlockStmtNode* node) override;
    void visitProgram(ProgramNode* node) override;

private:
    // Local variable tracking
    struct Local {
        std::string name;
        int depth;
        int slot;
        bool isCaptured;  // True if captured by a closure
    };

    // Upvalue tracking
    struct Upvalue {
        uint8_t index;      // Parent slot/upvalue index
        bool isLocal;       // true = local, false = upvalue
        std::string name;   // For debugging
    };

    // Label and Goto tracking for Lua 5.2+
    struct Label {
        size_t offset;
        int localCount;
        int scopeDepth;
    };

    struct Goto {
        std::string name;
        size_t instructionOffset;
        int localCount;
        int scopeDepth;
        int line;
    };

    // Compiler state for nested function compilation
    struct CompilerState {
        std::unique_ptr<Chunk> chunk;
        std::vector<Local> locals;
        std::vector<Upvalue> upvalues;
        int scopeDepth;
        int localCount;
        uint8_t expectedRetCount;
        CompilerState* enclosing;  // Parent compiler (not owned)
        std::unordered_map<std::string, Label> labels;
        std::vector<Goto> unresolvedGotos;
    };

    std::unique_ptr<Chunk> chunk_;
    int currentLine_;
    std::vector<Local> locals_;
    std::vector<Upvalue> upvalues_;
    int scopeDepth_;
    int localCount_;
    std::deque<CompilerState> compilerStack_;
    CompilerState* enclosingCompiler_;  // Parent compiler for upvalue resolution
    
    std::unordered_map<std::string, Label> labels_;
    std::vector<Goto> unresolvedGotos_;

    // Context for expression return values
    uint8_t expectedRetCount_; // 0=all (multires), 1=single (default), >1=specific count
    bool isTailCall_ = false;  // Whether current call should be compiled as tail call

    // Loop context for break statements
    struct LoopContext {
        std::vector<size_t> jumps;
        int localCount;
    };
    std::vector<LoopContext> loopStack_;

    // Bytecode emission
    void emitByte(uint8_t byte);
    void emitBytes(uint8_t byte1, uint8_t byte2);
    void emitOpCode(OpCode op);
    void emitConstant(const Value& value);
    void emitReturn();

    // Jump handling
    size_t emitJump(OpCode op);
    void patchJump(size_t offset);
    void emitLoop(size_t loopStart);

    // Variable handling
    void addLocal(const std::string& name);
    int resolveLocal(const std::string& name);
    int resolveUpvalue(const std::string& name);
    int resolveUpvalueHelper(CompilerState* compiler, const std::string& name);
    int addUpvalue(uint8_t index, bool isLocal);
    void beginScope();
    void endScope();

    // Compiler state management
    void pushCompilerState();
    void popCompilerState();

    // Helper for function compilation (shared by named and anonymous functions)
    void compileFunction(const std::string& name, const std::vector<std::string>& params,
                        const std::vector<std::unique_ptr<StmtNode>>& body, bool hasVarargs);

    // Loop context management for break statements
    void beginLoop();
    void endLoop();
    void addBreakJump(size_t jump);

    // Get current chunk
    Chunk* currentChunk() { return chunk_.get(); }

    // Set current line for error reporting
    void setLine(int line) { currentLine_ = line; }
};

#endif // LUA_CODEGEN_HPP
