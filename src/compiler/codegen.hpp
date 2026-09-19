#ifndef LUA_CODEGEN_HPP
#define LUA_CODEGEN_HPP

#include "common/common.hpp"
#include "compiler/ast.hpp"
#include "compiler/chunk.hpp"
#include "value/function.hpp"
#include <deque>
#include <memory>
#include <unordered_set>
#include <unordered_map>

// CodeGenerator: Walks AST and generates bytecode
// Implements visitor pattern to traverse AST nodes

class CodeGenerator : public ASTVisitor {
public:
    CodeGenerator();

    // Generate bytecode for a program
    std::unique_ptr<FunctionObject> generate(ProgramNode* program, const std::string& name = "script");


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
    void visitGlobalDeclStmt(GlobalDeclStmtNode* node) override;
    void visitMultipleGlobalDeclStmt(MultipleGlobalDeclStmtNode* node) override;
    void visitIfStmt(IfStmtNode* node) override;
    void visitWhileStmt(WhileStmtNode* node) override;
    void visitRepeatStmt(RepeatStmtNode* node) override;
    void visitForStmt(ForStmtNode* node) override;
    void visitForInStmt(ForInStmtNode* node) override;
    void visitFunctionDecl(FunctionDeclNode* node) override;
    void visitFunctionExpr(FunctionExprNode* node) override;
    void visitGroupExpr(GroupExprNode* node) override;
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
        bool isConstant;
        bool isClose;
        size_t startPC;   // Instruction offset where local enters scope
        uint32_t seq = 0; // Declaration sequence
    };

    // Upvalue tracking
    struct Upvalue {
        uint8_t index = 0;      // Parent slot/upvalue index
        bool isLocal = false;       // true = local, false = upvalue
        bool isConstant = false;    // true if captured variable is constant
        std::string name = "";   // For debugging
    };

    struct ActiveVar {
        std::string name;
        bool isGlobal;
        int depth;
    };

    // Label and Goto tracking for Lua 5.2+
    struct Label {
        std::string name;
        size_t offset;
        int localCount;
        int activeVarCount;
        std::vector<ActiveVar> activeVars;
        int blockDepth;
    };

    struct Goto {
        std::string name;
        size_t instructionOffset;
        int localCount;
        int activeVarCount;
        std::vector<ActiveVar> activeVars;
        int blockDepth;
        int line;
    };

    struct BlockScope {
        size_t firstLabel;
        size_t firstGoto;
        size_t firstActiveVar;
        int entryLocalCount;
        std::vector<ActiveVar> entryActiveVars;
    };

    enum class GlobalMode {
        ALL,
        NONE,
        CONST_ALL
    };

    struct DeclaredGlobal {
        std::string name;
        bool isConstant;
        int scopeDepth;
        uint32_t seq = 0;
    };

    // Compiler state for nested function compilation
    struct CompilerState {
        std::unique_ptr<Chunk> chunk;
        std::vector<Local> locals;
        std::vector<Upvalue> upvalues;
        std::vector<LocalVarInfo> finishedLocals;
        int scopeDepth;
        int localCount;
        uint8_t expectedRetCount;
        CompilerState* enclosing;  // Parent compiler (not owned)
        std::vector<Label> visibleLabels;
        std::vector<Goto> pendingGotos;
        std::vector<ActiveVar> activeVars;
        std::vector<BlockScope> blockScopes;
        std::vector<std::pair<Goto, Label>> gotosNeedingStubs;
        std::string varargName;
        int namedVarargSlot = -1;
        bool isVarargOptimized = false;
        GlobalMode globalMode = GlobalMode::ALL;
        std::vector<DeclaredGlobal> declaredGlobals;
        std::unordered_set<std::string> shadowedLocals;
        bool envDeclaredGlobal = false;
        uint32_t varSequence = 0;
    };

    std::unique_ptr<Chunk> chunk_;
    int currentLine_;
    std::vector<Local> locals_;
    std::vector<Upvalue> upvalues_;
    std::vector<LocalVarInfo> finishedLocals_;
    int scopeDepth_;
    int localCount_;
    std::deque<CompilerState> compilerStack_;
    CompilerState* enclosingCompiler_;  // Parent compiler for upvalue resolution
    
    std::vector<Label> visibleLabels_;
    std::vector<Goto> pendingGotos_;
    std::vector<ActiveVar> activeVars_;
    std::vector<BlockScope> blockScopes_;
    std::vector<std::pair<Goto, Label>> gotosNeedingStubs_;

    std::string currentVarargName_;
    int namedVarargSlot_ = -1;
    bool isVarargOptimized_ = false;
    GlobalMode globalMode_ = GlobalMode::ALL;
    std::vector<DeclaredGlobal> declaredGlobals_;
    std::unordered_set<std::string> shadowedLocals_;
    bool envDeclaredGlobal_ = false;
    uint32_t varSequence_ = 0;

    // Context for expression return values
    uint8_t expectedRetCount_; // 0=all (multires), 1=single (default), >1=specific count
    bool isTailCall_ = false;  // Whether current call should be compiled as tail call
    std::string expectedName_ = ""; // For naming anonymous functions assigned to variables

    // Loop context for break statements
    struct LoopContext {
        std::vector<size_t> jumps;
        int localCount;
    };
    std::vector<LoopContext> loopStack_;

    // Bytecode emission
    void emitByte(uint8_t byte);
    void emitBytes(uint8_t byte1, uint8_t byte2);
    void emitBytes(uint8_t byte1, uint8_t byte2, uint8_t byte3, uint8_t byte4);
    void emitOpCode(OpCode op);
    void emitConstant(const Value& value);
    void emitGetTabUp(uint8_t upvalue, size_t nameIndex);
    void emitSetTabUp(uint8_t upvalue, size_t nameIndex);
    void emitReturn();

    // Jump handling
    size_t emitJump(OpCode op);
    void patchJump(size_t offset);
    void emitLoop(size_t loopStart);
    void compileBlock(const std::vector<std::unique_ptr<StmtNode>>& stmts, bool hasEndScope = true);
    void resolveBlockGotos(size_t firstGoto, size_t firstLabel);
    void checkGotoScoping(const Goto& g, const Label& lbl);
    void emitGotoStubs();

    // Variable handling
    void addLocal(const std::string& name, bool isConstant = false, bool isClose = false);
    int resolveLocal(const std::string& name);
    void checkConstantAssign(const std::string& name, int line);
    bool isDeclaredGlobal(const std::string& name) const;
    bool isDeclaredGlobalConst(const std::string& name) const;
    int resolveUpvalue(const std::string& name);
    int resolveUpvalueHelper(CompilerState* compiler, const std::string& name);
    int addUpvalue(const std::string& name, uint8_t index, bool isLocal, bool isConstant);
    void beginScope();
    void endScope();

    // Compiler state management
    void pushCompilerState();
    void popCompilerState();

    // Helper for function compilation (shared by named and anonymous functions)
    void compileFunction(const std::string& name, const std::vector<std::string>& params,
                        const std::vector<std::unique_ptr<StmtNode>>& body, bool hasVarargs,
                        const std::string& varargName = "", int lineDefined = 0, int lastLineDefined = 0);

    // Loop context management for break statements
    void beginLoop();
    void endLoop();
    void addBreakJump(size_t jump);

    // Constant helpers
    size_t internString(const std::string& str);

    // Get current chunk
    Chunk* currentChunk() { return chunk_.get(); }

    // Set current line for error reporting
    void setLine(int line) { currentLine_ = line; }
};

#endif // LUA_CODEGEN_HPP
