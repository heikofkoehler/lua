#ifndef LUA_CODEGEN_HPP
#define LUA_CODEGEN_HPP

#include "common/common.hpp"
#include "compiler/ast.hpp"
#include "compiler/chunk.hpp"
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
    void visitCall(CallExprNode* node) override;
    void visitTableConstructor(TableConstructorNode* node) override;
    void visitIndexExpr(IndexExprNode* node) override;
    void visitPrintStmt(PrintStmtNode* node) override;
    void visitExprStmt(ExprStmtNode* node) override;
    void visitAssignmentStmt(AssignmentStmtNode* node) override;
    void visitIndexAssignmentStmt(IndexAssignmentStmtNode* node) override;
    void visitLocalDeclStmt(LocalDeclStmtNode* node) override;
    void visitIfStmt(IfStmtNode* node) override;
    void visitWhileStmt(WhileStmtNode* node) override;
    void visitRepeatStmt(RepeatStmtNode* node) override;
    void visitForStmt(ForStmtNode* node) override;
    void visitForInStmt(ForInStmtNode* node) override;
    void visitFunctionDecl(FunctionDeclNode* node) override;
    void visitReturn(ReturnStmtNode* node) override;
    void visitBreak(BreakStmtNode* node) override;
    void visitProgram(ProgramNode* node) override;

private:
    // Local variable tracking
    struct Local {
        std::string name;
        int depth;
        int slot;
    };

    // Compiler state for nested function compilation
    struct CompilerState {
        std::unique_ptr<Chunk> chunk;
        std::vector<Local> locals;
        int scopeDepth;
        int localCount;
    };

    std::unique_ptr<Chunk> chunk_;
    int currentLine_;
    std::vector<Local> locals_;
    int scopeDepth_;
    int localCount_;
    std::vector<CompilerState> compilerStack_;

    // Loop context for break statements
    std::vector<std::vector<size_t>> breakJumps_;  // Stack of break jump lists

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
    void beginScope();
    void endScope();

    // Compiler state management
    void pushCompilerState();
    void popCompilerState();

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
