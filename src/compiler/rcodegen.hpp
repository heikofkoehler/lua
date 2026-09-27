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
    FunctionObject* compile(ProgramNode* program, const std::string& sourceName = "chunk");

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
    RCodeGen* parent_ = nullptr;

    // Register allocation
    int nextReg_ = 0;  // Next free register
    int maxReg_ = 0;   // Maximum registers used in this frame
    std::vector<int> freeRegs_;  // Freed temporaries
    std::vector<int> reservedCounts_;  // Count of active reservations for temporaries/call args

    // Local variable info
    struct LocalVar {
        std::string name;
        int reg = 0;
        bool isConst = false;
        bool isClose = false;
        size_t startPC = 0;
    };

    // Scope management
    struct Scope {
        std::unordered_map<std::string, LocalVar> locals;
        int baseReg = 0;
        bool hasClose = false;
        int minCloseReg = -1;
    };
    std::vector<Scope> scopes_;

    // Upvalue tracking for closures
    struct UpvalueInfo {
        std::string name;
        int index = 0;  // Index in parent locals (if isLocal) or parent upvalue index
        bool isLocal = false;  // True if from parent's local, false if from parent's upvalue
        bool isConst = false;
    };
    std::vector<UpvalueInfo> upvalues_;

    bool isLocalReg(int reg) const;
    int resolveUpvalue(const std::string& name);
    int addUpvalue(const std::string& name, int index, bool isLocal, bool isConst = false);

    // Break/continue jump patching
    std::vector<std::vector<size_t>> breakJumps_;  // Stack of break lists per loop

    // Labels and gotos
    struct LabelInfo {
        size_t targetPc = 0;
        std::vector<std::string> activeLocals;
        int line = 0;
    };
    std::unordered_map<std::string, LabelInfo> labels_;

    struct GotoInfo {
        std::string name;
        size_t jumpPc = 0;
        std::vector<std::string> activeLocals;
        int line = 0;
    };
    std::vector<GotoInfo> pendingGotos_;

    // Expression result: register holding the value
    int exprReg_ = -1;

    // Generated register bytecode and line numbers
    std::vector<RInstruction> code_;
    std::vector<int> lines_;
    int currentLine_ = 1;

    void setLine(int line) { if (line > 0) currentLine_ = line; }

    // Allocate a temporary register
    int allocReg();
    // Free a temporary register
    void freeReg(int reg);
    // Reserve / unreserve registers from allocation or free
    void reserveReg(int reg);
    void unreserveReg(int reg);
    bool isReservedReg(int reg) const;
    // Reset temporaries back to base register
    void resetTemps(int base);

    // Allocate a local (never freed until scope exit)
    int allocLocal(const std::string& name, bool isConst = false, bool isClose = false);
    void activateLocal(int reg, const std::string& name, bool isConst = false, bool isClose = false);
    // Find local by name (search scopes)
    int findLocal(const std::string& name);
    const LocalVar* findLocalVar(const std::string& name) const;
    // Enter/exit scope
    void pushScope();
    void popScope();
    std::vector<std::string> getActiveLocals() const;

    // Emit helpers
    void emitABC(ROpCode op, int a, int b, int c);
    void emitAB(ROpCode op, int a, int b);
    void emitABx(ROpCode op, int a, int bx);
    void emitGetTabUp(int destReg, int upIdx, int constIdx);
    void emitSetTabUp(int valReg, int upIdx, int constIdx);
    size_t emitJump(ROpCode op, int a);  // Returns PC for patching
    void patchJump(size_t pc, size_t target);

    // Constant table
    int addConstant(const Value& v);

    // Check if expression can produce multiple results
    bool isMultiRes(ExprNode* expr) const;
    // Generate multires expression (call, method call, vararg) into destReg (or temp)
    int genMultiRes(ExprNode* expr, int wantedResults, int destReg = -1);

    // Generate expression into a specific register (or temp if dest < 0)
    // Returns the register holding the result.
    int genExpr(ExprNode* expr, int destReg = -1);
    int genCall(ExprNode* calleeNode, const std::vector<std::unique_ptr<ExprNode>>& args, int wantedResults, int destReg = -1, int methodReg = -1, int selfReg = -1);

    // Generate statement
    void genStmt(StmtNode* stmt);
    void genBlock(const std::vector<std::unique_ptr<StmtNode>>& stmts);

    FunctionObject* compileFunctionBody(const std::string& name,
                                        const std::vector<std::string>& params,
                                        const std::vector<std::unique_ptr<StmtNode>>& body,
                                        bool hasVarargs,
                                        const std::string& varargName,
                                        std::vector<UpvalueInfo>& outUpvalues,
                                        int lineDefined = 0,
                                        int lastLineDefined = 0);
};

#endif
