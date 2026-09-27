#ifndef LUA_RCODEGEN_HPP
#define LUA_RCODEGEN_HPP

#include "compiler/ast.hpp"
#include "compiler/chunk.hpp"
#include "value/function.hpp"
#include "vm/rinstruction.hpp"
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>

// Direct register code generator: AST -> ROP_* bytecode.
// Replaces the stack codegen + translator pipeline.
class RCodeGen : public ASTVisitor {
public:
    explicit RCodeGen();
    ~RCodeGen() override = default;

    // Compile a program AST into a FunctionObject with register bytecode.
    // Returns nullptr on error.
    FunctionObject* compile(ProgramNode* program);

    // Compile a function body (for nested functions).
    FunctionObject* compileFunction(FunctionExprNode* func);
    FunctionObject* compileFunction(FunctionDeclNode* func);

    // ASTVisitor implementations
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
    void visitFunctionExpr(FunctionExprNode* node) override;
    void visitGroupExpr(GroupExprNode* node) override;

    void visitExprStmt(ExprStmtNode* node) override;
    void visitAssignmentStmt(AssignmentStmtNode* node) override;
    void visitIndexAssignmentStmt(IndexAssignmentStmtNode* node) override;
    void visitLocalDeclStmt(LocalDeclStmtNode* node) override;
    void visitMultipleLocalDeclStmt(MultipleLocalDeclStmtNode* node) override;
    void visitMultipleAssignmentStmt(MultipleAssignmentStmtNode* node) override;
    void visitGlobalDeclStmt(GlobalDeclStmtNode* node) override;
    void visitMultipleGlobalDeclStmt(MultipleGlobalDeclStmtNode* node) override;
    void visitIfStmt(IfStmtNode* node) override;
    void visitWhileStmt(WhileStmtNode* node) override;
    void visitRepeatStmt(RepeatStmtNode* node) override;
    void visitForStmt(ForStmtNode* node) override;
    void visitForInStmt(ForInStmtNode* node) override;
    void visitFunctionDecl(FunctionDeclNode* node) override;
    void visitReturn(ReturnStmtNode* node) override;
    void visitBreak(BreakStmtNode* node) override;
    void visitGoto(GotoStmtNode* node) override;
    void visitLabel(LabelStmtNode* node) override;
    void visitBlock(BlockStmtNode* node) override;
    void visitProgram(ProgramNode* node) override;

private:
    // Current function being compiled
    FunctionObject* func_ = nullptr;

    // Register allocation
    int nextReg_ = 0;  // Next free register
    std::vector<int> freeRegs_;  // Freed temporaries

    // Local variable scope: name -> register
    struct Scope {
        std::unordered_map<std::string, int> locals;
    };
    std::vector<Scope> scopes_;

    // Upvalue tracking for closures
    struct UpvalueInfo {
        std::string name;
        int index;  // Index in parent, or -1 if from grandparent
        bool isLocal;  // True if from parent's local, false if from parent's upvalue
    };
    std::vector<UpvalueInfo> upvalues_;

    // Break/continue jump patching
    std::vector<std::vector<size_t>> breakJumps_;  // Stack of break lists per loop

    // Expression result: register holding the value
    int exprReg_ = -1;

    // Generated register bytecode
    std::vector<RInstruction> code_;

    // Allocate a temporary register
    int allocReg();
    // Free a temporary register
    void freeReg(int reg);
    // Allocate a local (never freed until scope exit)
    int allocLocal(const std::string& name);
    // Find local by name (search scopes)
    int findLocal(const std::string& name);
    // Enter/exit scope
    void pushScope();
    void popScope();

    // Emit helpers
    void emitABC(ROpCode op, int a, int b, int c);
    void emitAB(ROpCode op, int a, int b);
    void emitABx(ROpCode op, int a, int bx);
    size_t emitJump(ROpCode op, int a);  // Returns PC for patching
    void patchJump(size_t pc, size_t target);

    // Constant table
    int addConstant(const Value& v);

    // Generate expression into a specific register (or temp if dest < 0)
    // Returns the register holding the result.
    int genExpr(ExprNode* expr, int destReg = -1);

    // Generate statement
    void genStmt(StmtNode* stmt);
    void genBlock(const std::vector<std::unique_ptr<StmtNode>>& stmts);
};

#endif
