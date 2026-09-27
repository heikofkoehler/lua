#include "compiler/rcodegen.hpp"
#include "value/string.hpp"
#include "vm/rinstruction.hpp"

RCodeGen::RCodeGen() {
    pushScope();
}

FunctionObject* RCodeGen::compile(ProgramNode* program) {
    // Create main function with a Chunk
    auto chunk = std::make_unique<Chunk>();
    func_ = new FunctionObject("", 0, std::move(chunk), 0, true);

    nextReg_ = 0;
    code_.clear();

    program->accept(*this);

    // Ensure return at end
    emitAB(ROpCode::ROP_RETURN, 0, 1);  // return 0 values

    // Install register bytecode into the chunk
    func_->chunk()->setRCode(std::move(code_), nextReg_);

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
    code_.push_back(ropEncodeABC(op, (uint8_t)a, (uint8_t)b, (uint8_t)c));
}

void RCodeGen::emitAB(ROpCode op, int a, int b) {
    code_.push_back(ropEncodeABC(op, (uint8_t)a, (uint8_t)b, 0));
}

void RCodeGen::emitABx(ROpCode op, int a, int bx) {
    code_.push_back(ropEncodeABx(op, (uint8_t)a, (uint16_t)bx));
}

size_t RCodeGen::emitJump(ROpCode op, int a) {
    // Emit jump with placeholder offset, return PC for patching
    size_t pc = code_.size();
    code_.push_back(ropEncodeAsBx(op, (uint8_t)a, 0));
    return pc;
}

void RCodeGen::patchJump(size_t pc, size_t target) {
    // Patch a jump instruction at pc to jump to target
    // offset = target - (pc + 1)
    int offset = (int)target - (int)(pc + 1);
    RInstruction old = code_[pc];
    ROpCode op = (ROpCode)(old & 0xFF);
    uint8_t a = (old >> 8) & 0xFF;
    code_[pc] = ropEncodeAsBx(op, a, (int16_t)offset);
}

int RCodeGen::addConstant(const Value& v) {
    // Add to function's constant table via chunk
    return (int)func_->chunk()->addConstant(v);
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
    (void)node;
    int reg = allocReg();
    const Value& v = node->value();
    if (v.isNil()) {
        emitAB(ROpCode::ROP_LOADNIL, reg, 1);
    } else if (v.isBool()) {
        emitAB(ROpCode::ROP_LOADBOOL, reg, v.asBool() ? 1 : 0);
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
    (void)node;
    int reg = allocReg();
    // TODO: intern string, add constant
    exprReg_ = reg;
}

void RCodeGen::visitUnary(UnaryNode* node) {
    (void)node;
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
    ROpCode op;
    switch (node->op()) {
        case TokenType::PLUS: op = ROpCode::ROP_ADD; break;
        case TokenType::MINUS: op = ROpCode::ROP_SUB; break;
        case TokenType::STAR: op = ROpCode::ROP_MUL; break;
        case TokenType::SLASH: op = ROpCode::ROP_DIV; break;
        case TokenType::SLASH_SLASH: op = ROpCode::ROP_IDIV; break;
        case TokenType::PERCENT: op = ROpCode::ROP_MOD; break;
        case TokenType::CARET: op = ROpCode::ROP_POW; break;
        case TokenType::AMPERSAND: op = ROpCode::ROP_BAND; break;
        case TokenType::PIPE: op = ROpCode::ROP_BOR; break;
        case TokenType::TILDE: op = ROpCode::ROP_BXOR; break;
        case TokenType::LESS_LESS: op = ROpCode::ROP_SHL; break;
        case TokenType::GREATER_GREATER: op = ROpCode::ROP_SHR; break;
        case TokenType::DOT_DOT: op = ROpCode::ROP_CONCAT; break;
        default: op = ROpCode::ROP_ADD; break;  // TODO: comparisons, and/or
    }
    emitABC(op, reg, left, right);
    exprReg_ = reg;
    freeReg(left);
    freeReg(right);
}

void RCodeGen::visitVariable(VariableExprNode* node) {
    (void)node;
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
    (void)node;
    // TODO: implement
    exprReg_ = allocReg();
}

void RCodeGen::visitCall(CallExprNode* node) {
    (void)node;
    // TODO: implement
    exprReg_ = allocReg();
}

void RCodeGen::visitMethodCall(MethodCallExprNode* node) {
    (void)node;
    // TODO: implement
    exprReg_ = allocReg();
}

void RCodeGen::visitTableConstructor(TableConstructorNode* node) {
    (void)node;
    // TODO: implement
    exprReg_ = allocReg();
}

void RCodeGen::visitIndexExpr(IndexExprNode* node) {
    (void)node;
    // TODO: implement
    exprReg_ = allocReg();
}

void RCodeGen::visitFunctionExpr(FunctionExprNode* node) {
    (void)node;
    // TODO: implement closure
    exprReg_ = allocReg();
}

void RCodeGen::visitGroupExpr(GroupExprNode* node) {
    (void)node;
    node->expr()->accept(*this);
    // exprReg_ already set
}

// === Statement visitors ===

void RCodeGen::visitExprStmt(ExprStmtNode* node) {
    (void)node;
    int r = genExpr(node->expr());
    freeReg(r);
}

void RCodeGen::visitAssignmentStmt(AssignmentStmtNode* node) {
    int val = genExpr(node->value());
    int local = findLocal(node->name());
    if (local >= 0) {
        emitAB(ROpCode::ROP_MOVE, local, val);
    } else {
        // Global: TODO - use SETTABUP with _ENV
        // For now, treat as error
    }
    freeReg(val);
}

void RCodeGen::visitIndexAssignmentStmt(IndexAssignmentStmtNode* node) {
    (void)node;
    // TODO: implement
}

void RCodeGen::visitLocalDeclStmt(LocalDeclStmtNode* node) {
    int reg = allocLocal(node->name());
    if (node->initializer()) {
        int val = genExpr(node->initializer());
        if (val != reg) {
            emitAB(ROpCode::ROP_MOVE, reg, val);
            freeReg(val);
        }
    } else {
        emitAB(ROpCode::ROP_LOADNIL, reg, 1);
    }
}

void RCodeGen::visitMultipleLocalDeclStmt(MultipleLocalDeclStmtNode* node) {
    (void)node;
    // TODO: implement
}

void RCodeGen::visitMultipleAssignmentStmt(MultipleAssignmentStmtNode* node) {
    (void)node;
    // TODO: implement
}

void RCodeGen::visitGlobalDeclStmt(GlobalDeclStmtNode* node) {
    (void)node;
    // TODO: implement
}

void RCodeGen::visitMultipleGlobalDeclStmt(MultipleGlobalDeclStmtNode* node) {
    (void)node;
    // TODO: implement
}

void RCodeGen::visitIfStmt(IfStmtNode* node) {
    (void)node;
    // TODO: implement
}

void RCodeGen::visitWhileStmt(WhileStmtNode* node) {
    (void)node;
    // TODO: implement
}

void RCodeGen::visitRepeatStmt(RepeatStmtNode* node) {
    (void)node;
    // TODO: implement
}

void RCodeGen::visitForStmt(ForStmtNode* node) {
    (void)node;
    // TODO: implement
}

void RCodeGen::visitForInStmt(ForInStmtNode* node) {
    (void)node;
    // TODO: implement
}

void RCodeGen::visitFunctionDecl(FunctionDeclNode* node) {
    (void)node;
    // TODO: implement
}

void RCodeGen::visitReturn(ReturnStmtNode* node) {
    const auto& values = node->values();
    if (values.empty()) {
        emitAB(ROpCode::ROP_RETURN, 0, 1);  // return 0 values
    } else if (values.size() == 1) {
        int val = genExpr(values[0].get());
        emitABC(ROpCode::ROP_RETURN, val, 2, 0);  // return 1 value
        // Don't free: function is exiting
    } else {
        // Multiple values: place in consecutive registers
        int base = allocReg();
        // Ensure consecutive: alloc N-1 more
        for (size_t i = 1; i < values.size(); ++i) allocReg();
        for (size_t i = 0; i < values.size(); ++i) {
            int val = genExpr(values[i].get());
            if (val != base + (int)i) {
                emitAB(ROpCode::ROP_MOVE, base + (int)i, val);
                freeReg(val);
            }
        }
        emitABC(ROpCode::ROP_RETURN, base, (int)values.size() + 1, 0);
    }
}

void RCodeGen::visitBreak(BreakStmtNode* node) {
    (void)node;
    // TODO: implement
}

void RCodeGen::visitGoto(GotoStmtNode* node) {
    (void)node;
    // TODO: implement
}

void RCodeGen::visitLabel(LabelStmtNode* node) {
    (void)node;
    // TODO: implement
}

void RCodeGen::visitBlock(BlockStmtNode* node) {
    (void)node;
    genBlock(node->statements());
}

void RCodeGen::visitProgram(ProgramNode* node) {
    (void)node;
    genBlock(node->statements());
}

FunctionObject* RCodeGen::compileFunction(FunctionExprNode* func) {
    (void)func;
    // TODO: implement
    return nullptr;
}

FunctionObject* RCodeGen::compileFunction(FunctionDeclNode* func) {
    (void)func;
    // TODO: implement
    return nullptr;
}
