#include "compiler/rcodegen.hpp"
#include "value/string.hpp"

RCodeGen::RCodeGen() {
    pushScope();
}

FunctionObject* RCodeGen::compile(ProgramNode* program) {
    // Create main function
    func_ = new FunctionObject();
    func_->setNumParams(0);
    func_->setIsVararg(true);  // Main chunk is vararg

    nextReg_ = 0;
    // TODO: implement

    program->accept(*this);

    // Ensure return at end
    emitAB(ROpCode::ROP_RETURN, 0, 1);  // return 0 values

    return func_;
}

int RCodeGen::allocReg() {
    if (!freeRegs_.empty()) {
        int r = freeRegs_.back();
        freeRegs_.pop_back();
        return r;
    }
    return nextReg_++;
}

void RCodeGen::freeReg(int reg) {
    // Only free temporaries, not locals (locals are managed by scope)
    freeRegs_.push_back(reg);
}

int RCodeGen::allocLocal(const std::string& name) {
    int reg = nextReg_++;
    scopes_.back().locals[name] = reg;
    return reg;
}

int RCodeGen::findLocal(const std::string& name) {
    for (int i = (int)scopes_.size() - 1; i >= 0; --i) {
        auto it = scopes_[i].locals.find(name);
        if (it != scopes_[i].locals.end()) return it->second;
    }
    return -1;
}

void RCodeGen::pushScope() {
    scopes_.push_back(Scope());
}

void RCodeGen::popScope() {
    scopes_.pop_back();
}

void RCodeGen::emitABC(ROpCode op, int a, int b, int c) {
    // TODO: implement using RInstruction
}

void RCodeGen::emitAB(ROpCode op, int a, int b) {
    // TODO: implement
}

void RCodeGen::emitABx(ROpCode op, int a, int bx) {
    // TODO: implement
}

size_t RCodeGen::emitJump(ROpCode op, int a) {
    // TODO: implement, return PC
    return 0;
}

void RCodeGen::patchJump(size_t pc, size_t target) {
    // TODO: implement
}

int RCodeGen::addConstant(const Value& v) {
    // TODO: implement constant table
    return 0;
}

int RCodeGen::genExpr(ExprNode* expr, int destReg) {
    expr->accept(*this);
    int src = exprReg_;
    if (destReg >= 0 && destReg != src) {
        emitAB(ROpCode::ROP_MOVE, destReg, src);
        freeReg(src);
        return destReg;
    }
    return src;
}

void RCodeGen::genStmt(StmtNode* stmt) {
    if (stmt) stmt->accept(*this);
}

void RCodeGen::genBlock(const std::vector<std::unique_ptr<StmtNode>>& stmts) {
    pushScope();
    for (const auto& s : stmts) genStmt(s.get());
    popScope();
}

// === Expression visitors ===

void RCodeGen::visitLiteral(LiteralNode* node) {
    int reg = allocReg();
    const Value& v = node->value();
    if (v.isNil()) {
        emitAB(ROpCode::ROP_LOADNIL, reg, 1);
    } else if (v.isBoolean()) {
        emitAB(ROpCode::ROP_LOADBOOL, reg, v.boolean() ? 1 : 0);
    } else if (v.isInteger()) {
        // TODO: use LOADK or LOADI
        int c = addConstant(v);
        emitABx(ROpCode::ROP_LOADK, reg, c);
    } else if (v.isNumber()) {
        int c = addConstant(v);
        emitABx(ROpCode::ROP_LOADK, reg, c);
    }
    exprReg_ = reg;
}

void RCodeGen::visitStringLiteral(StringLiteralNode* node) {
    int reg = allocReg();
    // TODO: intern string, add constant
    exprReg_ = reg;
}

void RCodeGen::visitUnary(UnaryNode* node) {
    int operand = genExpr(node->operand());
    int reg = allocReg();
    // TODO: map TokenType to ROP_UNM, ROP_NOT, ROP_LEN, ROP_BNOT
    exprReg_ = reg;
    freeReg(operand);
}

void RCodeGen::visitBinary(BinaryNode* node) {
    int left = genExpr(node->left());
    int right = genExpr(node->right());
    int reg = allocReg();
    // TODO: map TokenType to ROP_ADD, ROP_SUB, etc.
    exprReg_ = reg;
    freeReg(left);
    freeReg(right);
}

void RCodeGen::visitVariable(VariableExprNode* node) {
    int reg = allocReg();
    int local = findLocal(node->name());
    if (local >= 0) {
        emitAB(ROpCode::ROP_MOVE, reg, local);
    } else {
        // TODO: upvalue or global
    }
    exprReg_ = reg;
}

void RCodeGen::visitVararg(VarargExprNode* node) {
    // TODO: implement
    exprReg_ = allocReg();
}

void RCodeGen::visitCall(CallExprNode* node) {
    // TODO: implement
    exprReg_ = allocReg();
}

void RCodeGen::visitMethodCall(MethodCallExprNode* node) {
    // TODO: implement
    exprReg_ = allocReg();
}

void RCodeGen::visitTableConstructor(TableConstructorNode* node) {
    // TODO: implement
    exprReg_ = allocReg();
}

void RCodeGen::visitIndexExpr(IndexExprNode* node) {
    // TODO: implement
    exprReg_ = allocReg();
}

void RCodeGen::visitFunctionExpr(FunctionExprNode* node) {
    // TODO: implement closure
    exprReg_ = allocReg();
}

void RCodeGen::visitGroupExpr(GroupExprNode* node) {
    node->expr()->accept(*this);
    // exprReg_ already set
}

// === Statement visitors ===

void RCodeGen::visitExprStmt(ExprStmtNode* node) {
    int r = genExpr(node->expr());
    freeReg(r);
}

void RCodeGen::visitAssignmentStmt(AssignmentStmtNode* node) {
    // TODO: implement
}

void RCodeGen::visitIndexAssignmentStmt(IndexAssignmentStmtNode* node) {
    // TODO: implement
}

void RCodeGen::visitLocalDeclStmt(LocalDeclStmtNode* node) {
    // TODO: implement
}

void RCodeGen::visitMultipleLocalDeclStmt(MultipleLocalDeclStmtNode* node) {
    // TODO: implement
}

void RCodeGen::visitMultipleAssignmentStmt(MultipleAssignmentStmtNode* node) {
    // TODO: implement
}

void RCodeGen::visitGlobalDeclStmt(GlobalDeclStmtNode* node) {
    // TODO: implement
}

void RCodeGen::visitMultipleGlobalDeclStmt(MultipleGlobalDeclStmtNode* node) {
    // TODO: implement
}

void RCodeGen::visitIfStmt(IfStmtNode* node) {
    // TODO: implement
}

void RCodeGen::visitWhileStmt(WhileStmtNode* node) {
    // TODO: implement
}

void RCodeGen::visitRepeatStmt(RepeatStmtNode* node) {
    // TODO: implement
}

void RCodeGen::visitForStmt(ForStmtNode* node) {
    // TODO: implement
}

void RCodeGen::visitForInStmt(ForInStmtNode* node) {
    // TODO: implement
}

void RCodeGen::visitFunctionDecl(FunctionDeclNode* node) {
    // TODO: implement
}

void RCodeGen::visitReturn(ReturnStmtNode* node) {
    // TODO: implement
}

void RCodeGen::visitBreak(BreakStmtNode* node) {
    // TODO: implement
}

void RCodeGen::visitGoto(GotoStmtNode* node) {
    // TODO: implement
}

void RCodeGen::visitLabel(LabelStmtNode* node) {
    // TODO: implement
}

void RCodeGen::visitBlock(BlockStmtNode* node) {
    genBlock(node->statements());
}

void RCodeGen::visitProgram(ProgramNode* node) {
    genBlock(node->statements());
}

FunctionObject* RCodeGen::compileFunction(FunctionExprNode* func) {
    // TODO: implement
    return nullptr;
}

FunctionObject* RCodeGen::compileFunction(FunctionDeclNode* func) {
    // TODO: implement
    return nullptr;
}
