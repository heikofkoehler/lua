#include "compiler/rcodegen.hpp"
#include "value/string.hpp"
#include "vm/rinstruction.hpp"
#include <algorithm>

RCodeGen::RCodeGen() {
    pushScope();
}

FunctionObject* RCodeGen::compile(ProgramNode* program, const std::string& sourceName) {
    auto chunk = std::make_unique<Chunk>();
    chunk->setSourceName(sourceName);
    func_ = new FunctionObject("", 0, std::move(chunk), 1, true);
    func_->addUpvalueName("_ENV");
    upvalues_.clear();
    upvalues_.push_back({"_ENV", 0, false, false});

    scopes_.clear();
    pushScope();
    nextReg_ = 0;
    maxReg_ = 0;
    code_.clear();
    lines_.clear();
    currentLine_ = 1;
    labels_.clear();
    pendingGotos_.clear();

    program->accept(*this);

    // Ensure return at end
    emitAB(ROpCode::ROP_RETURN, 0, 1);  // return 0 values

    // Check for unresolved gotos and goto scoping
    for (const auto& g : pendingGotos_) {
        auto it = labels_.find(g.name);
        if (it != labels_.end()) {
            for (const auto& var : it->second.activeLocals) {
                if (std::find(g.activeLocals.begin(), g.activeLocals.end(), var) == g.activeLocals.end()) {
                    throw CompileError("<goto " + g.name + "> jumps into the scope of '" + var + "'", g.line);
                }
            }
            patchJump(g.jumpPc, it->second.targetPc);
        } else {
            throw CompileError("no visible label '" + g.name + "' for <goto>", g.line);
        }
    }
    auto& lvars = const_cast<std::vector<LocalVarInfo>&>(func_->localVars());
    for (auto& lv : lvars) {
        if (lv.endPC == (size_t)-1) {
            lv.endPC = code_.size();
        }
    }

    // Install register bytecode and line numbers into the chunk
    func_->chunk()->setRCode(std::move(code_), maxReg_, std::move(lines_));

    return func_;
}

int RCodeGen::allocReg() {
    while (!freeRegs_.empty()) {
        int r = freeRegs_.back();
        freeRegs_.pop_back();
        if (!isLocalReg(r) && !isReservedReg(r)) {
            maxReg_ = std::max(maxReg_, std::max(nextReg_, r + 1));
            return r;
        }
    }
    while (isLocalReg(nextReg_) || isReservedReg(nextReg_)) {
        nextReg_++;
    }
    int r = nextReg_++;
    maxReg_ = std::max(maxReg_, nextReg_);
    return r;
}

bool RCodeGen::isLocalReg(int reg) const {
    for (const auto& scope : scopes_) {
        for (const auto& pair : scope.locals) {
            if (pair.second.reg == reg) return true;
        }
    }
    return false;
}

void RCodeGen::reserveReg(int reg) {
    if (reg < 0) return;
    if (static_cast<size_t>(reg) >= reservedCounts_.size()) {
        reservedCounts_.resize(reg + 16, 0);
    }
    reservedCounts_[reg]++;
    freeRegs_.erase(
        std::remove(freeRegs_.begin(), freeRegs_.end(), reg),
        freeRegs_.end()
    );
}

void RCodeGen::unreserveReg(int reg) {
    if (reg < 0 || static_cast<size_t>(reg) >= reservedCounts_.size()) return;
    if (reservedCounts_[reg] > 0) {
        reservedCounts_[reg]--;
    }
}

bool RCodeGen::isReservedReg(int reg) const {
    if (reg < 0 || static_cast<size_t>(reg) >= reservedCounts_.size()) return false;
    return reservedCounts_[reg] > 0;
}

void RCodeGen::freeReg(int reg) {
    if (reg < 0 || isLocalReg(reg) || isReservedReg(reg)) return;
    if (std::find(freeRegs_.begin(), freeRegs_.end(), reg) != freeRegs_.end()) return;
    freeRegs_.push_back(reg);
}

void RCodeGen::resetTemps(int base) {
    // Clear stale temporary registers to prevent GC from seeing dead references.
    // This is critical for weak tables: a temp holding a dead object will
    // keep it alive if not cleared.
    // Use line 0 so these cleanup instructions don't affect debug line hooks.
    int savedLine = currentLine_;
    setLine(0);
    for (int r = base; r < nextReg_; ++r) {
        emitAB(ROpCode::ROP_LOADNIL, r, 0);
    }
    setLine(savedLine);
    nextReg_ = base;
    freeRegs_.clear();
    reservedCounts_.clear();
}

void RCodeGen::activateLocal(int reg, const std::string& name, bool isConst, bool isClose) {
    LocalVar var;
    var.name = name;
    var.reg = reg;
    var.isConst = isConst;
    var.isClose = isClose;
    var.startPC = code_.size();
    scopes_.back().locals[name] = var;
    if (isClose) {
        scopes_.back().hasClose = true;
        if (scopes_.back().minCloseReg < 0 || reg < scopes_.back().minCloseReg) {
            scopes_.back().minCloseReg = reg;
        }
    }
    if (func_) {
        func_->addLocalVar(name, var.startPC, (size_t)-1, reg);
    }
}

int RCodeGen::allocLocal(const std::string& name, bool isConst, bool isClose) {
    int reg = nextReg_++;
    maxReg_ = std::max(maxReg_, nextReg_);
    activateLocal(reg, name, isConst, isClose);
    return reg;
}

const RCodeGen::LocalVar* RCodeGen::findLocalVar(const std::string& name) const {
    for (int i = (int)scopes_.size() - 1; i >= 0; --i) {
        auto it = scopes_[i].locals.find(name);
        if (it != scopes_[i].locals.end()) return &it->second;
    }
    return nullptr;
}

int RCodeGen::findLocal(const std::string& name) {
    const LocalVar* var = findLocalVar(name);
    return var ? var->reg : -1;
}

int RCodeGen::addUpvalue(const std::string& name, int index, bool isLocal, bool isConst) {
    for (size_t i = 0; i < upvalues_.size(); ++i) {
        if (upvalues_[i].name == name) return static_cast<int>(i);
    }
    upvalues_.push_back({name, index, isLocal, isConst});
    return static_cast<int>(upvalues_.size() - 1);
}

int RCodeGen::resolveUpvalue(const std::string& name) {
    for (size_t i = 0; i < upvalues_.size(); ++i) {
        if (upvalues_[i].name == name) return static_cast<int>(i);
    }
    if (!parent_) return -1;

    // Check parent's locals
    const LocalVar* parentLocal = parent_->findLocalVar(name);
    if (parentLocal) {
        return addUpvalue(name, parentLocal->reg, true, parentLocal->isConst);
    }

    // Check parent's upvalues recursively
    int parentUpval = parent_->resolveUpvalue(name);
    if (parentUpval >= 0) {
        return addUpvalue(name, parentUpval, false, parent_->upvalues_[parentUpval].isConst);
    }

    return -1;
}

void RCodeGen::pushScope() {
    Scope s;
    s.baseReg = nextReg_;
    scopes_.push_back(s);
}

void RCodeGen::popScope() {
    if (!scopes_.empty()) {
        Scope s = scopes_.back();
        // Close upvalues for locals in this scope that were captured by closures.
        // This must happen before the registers are freed (nextReg_ reset below).
        // ROP_CLOSE with A=baseReg closes upvalues with stackIndex >= base + baseReg.
        // Skip for the outermost scope (baseReg==0) as there are no enclosing upvalues to close
        // at function exit (RETURN handles it).
        if (!s.locals.empty() && s.baseReg > 0) {
            emitAB(ROpCode::ROP_CLOSE, s.baseReg, 0);
        }
        if (s.hasClose && s.minCloseReg >= 0) {
            emitAB(ROpCode::ROP_CLOSE, s.minCloseReg, 0);
        }
        // Clear dead local registers to prevent GC from seeing stale references.
        // This is surgical: only clear the specific registers for locals in this
        // scope, not the entire range, to avoid breaking live temporaries.
        // Use line 0 so cleanup doesn't affect debug line hooks.
        if (s.baseReg > 0) {
            int savedLine = currentLine_;
            setLine(0);
            for (const auto& pair : s.locals) {
                int reg = pair.second.reg;
                emitAB(ROpCode::ROP_LOADNIL, reg, 0);
            }
            setLine(savedLine);
        }
        if (func_) {
            auto& lvars = const_cast<std::vector<LocalVarInfo>&>(func_->localVars());
            for (auto& lv : lvars) {
                if (s.locals.find(lv.name) != s.locals.end() && lv.endPC == (size_t)-1) {
                    lv.endPC = code_.size();
                }
            }
        }
        nextReg_ = s.baseReg;
        freeRegs_.clear();
        scopes_.pop_back();
    }
}

std::vector<std::string> RCodeGen::getActiveLocals() const {
    std::vector<std::string> res;
    for (const auto& s : scopes_) {
        for (const auto& pair : s.locals) {
            res.push_back(pair.first);
        }
    }
    return res;
}

void RCodeGen::emitABC(ROpCode op, int a, int b, int c) {
    code_.push_back(ropEncodeABC(op, (uint8_t)a, (uint8_t)b, (uint8_t)c));
    lines_.push_back(currentLine_);
}

void RCodeGen::emitAB(ROpCode op, int a, int b) {
    code_.push_back(ropEncodeABC(op, (uint8_t)a, (uint8_t)b, 0));
    lines_.push_back(currentLine_);
}

void RCodeGen::emitABx(ROpCode op, int a, int bx) {
    code_.push_back(ropEncodeABx(op, (uint8_t)a, (uint16_t)bx));
    lines_.push_back(currentLine_);
}

void RCodeGen::emitGetTabUp(int destReg, int upIdx, int constIdx) {
    if (constIdx < 256) {
        emitABC(ROpCode::ROP_GETTABUP, destReg, upIdx, constIdx);
    } else {
        int kReg = allocReg();
        emitABx(ROpCode::ROP_LOADK, kReg, constIdx);
        int tabReg = allocReg();
        emitAB(ROpCode::ROP_GETUPVAL, tabReg, upIdx);
        emitABC(ROpCode::ROP_GETTABLE, destReg, tabReg, kReg);
        freeReg(tabReg);
        freeReg(kReg);
    }
}

void RCodeGen::emitSetTabUp(int valReg, int upIdx, int constIdx) {
    if (constIdx < 256) {
        emitABC(ROpCode::ROP_SETTABUP, valReg, upIdx, constIdx);
    } else {
        int kReg = allocReg();
        emitABx(ROpCode::ROP_LOADK, kReg, constIdx);
        int tabReg = allocReg();
        emitAB(ROpCode::ROP_GETUPVAL, tabReg, upIdx);
        emitABC(ROpCode::ROP_SETTABLE, tabReg, kReg, valReg);
        freeReg(tabReg);
        freeReg(kReg);
    }
}

size_t RCodeGen::emitJump(ROpCode op, int a) {
    size_t pc = code_.size();
    code_.push_back(ropEncodeAsBx(op, (uint8_t)a, 0));
    lines_.push_back(currentLine_);
    return pc;
}

void RCodeGen::patchJump(size_t pc, size_t target) {
    int offset = (int)target - (int)(pc + 1);
    RInstruction old = code_[pc];
    ROpCode op = (ROpCode)(old & 0xFF);
    uint8_t a = (old >> 8) & 0xFF;
    code_[pc] = ropEncodeAsBx(op, a, (int16_t)offset);
}

int RCodeGen::addConstant(const Value& v) {
    return (int)func_->chunk()->addConstant(v);
}

bool RCodeGen::isMultiRes(ExprNode* expr) const {
    return dynamic_cast<CallExprNode*>(expr) != nullptr ||
           dynamic_cast<MethodCallExprNode*>(expr) != nullptr ||
           dynamic_cast<VarargExprNode*>(expr) != nullptr;
}

int RCodeGen::genMultiRes(ExprNode* expr, int wantedResults, int destReg) {
    if (auto* call = dynamic_cast<CallExprNode*>(expr)) {
        return genCall(call->callee(), call->args(), wantedResults, destReg, -1, -1);
    } else if (auto* mcall = dynamic_cast<MethodCallExprNode*>(expr)) {
        int obj = genExpr(mcall->object());
        reserveReg(obj);
        size_t strIdx = func_->chunk()->addString(mcall->method());
        int c = addConstant(Value::string(strIdx));
        int key = allocReg();
        emitABx(ROpCode::ROP_LOADK, key, c);
        int methodReg = allocReg();
        emitABC(ROpCode::ROP_GETTABLE, methodReg, obj, key);
        freeReg(key);
        unreserveReg(obj);
        return genCall(nullptr, mcall->args(), wantedResults, destReg, methodReg, obj);
    } else if (auto* varg = dynamic_cast<VarargExprNode*>(expr)) {
        (void)varg;
        int reg = (destReg >= 0) ? destReg : allocReg();
        int bParam = (wantedResults < 0) ? 0 : (wantedResults + 1);
        emitAB(ROpCode::ROP_VARARG, reg, bParam);
        return reg;
    }
    return genExpr(expr, destReg);
}

int RCodeGen::genExpr(ExprNode* expr, int destReg) {
    if (isMultiRes(expr)) {
        int r = genMultiRes(expr, 1, destReg);
        exprReg_ = r;
        return r;
    }
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
    if (!stmt) return;
    setLine(stmt->line());
    int baseBefore = nextReg_;
    stmt->accept(*this);
    if (!dynamic_cast<LocalDeclStmtNode*>(stmt) && 
        !dynamic_cast<MultipleLocalDeclStmtNode*>(stmt)) {
        resetTemps(baseBefore);
    }
}

void RCodeGen::genBlock(const std::vector<std::unique_ptr<StmtNode>>& stmts) {
    pushScope();
    for (const auto& s : stmts) genStmt(s.get());
    popScope();
}

FunctionObject* RCodeGen::compileFunctionBody(
    const std::string& name,
    const std::vector<std::string>& params,
    const std::vector<std::unique_ptr<StmtNode>>& body,
    bool hasVarargs,
    const std::string& varargName,
    std::vector<UpvalueInfo>& outUpvalues,
    int lineDefined,
    int lastLineDefined) {

    auto childChunk = std::make_unique<Chunk>();
    if (func_ && func_->chunk()) {
        childChunk->setSourceName(func_->chunk()->sourceName());
    }
    int nparams = static_cast<int>(params.size());
    FunctionObject* child = new FunctionObject(name, nparams, std::move(childChunk), 0, hasVarargs);
    child->setLines(lineDefined, lastLineDefined);

    RCodeGen childGen;
    childGen.func_ = child;
    childGen.parent_ = this;
    childGen.nextReg_ = 0;
    childGen.maxReg_ = nparams;
    childGen.code_.clear();
    childGen.lines_.clear();
    childGen.currentLine_ = lineDefined > 0 ? lineDefined : currentLine_;
    childGen.scopes_.clear();
    childGen.pushScope();
    for (int i = 0; i < nparams; ++i) {
        childGen.allocLocal(params[i]);
    }

    // Handle named vararg parameter (e.g., function f(...t))
    if (!varargName.empty()) {
        int varargReg = childGen.allocLocal(varargName);
        childGen.emitAB(ROpCode::ROP_PACKVARARG, varargReg, 0);
        // Mark the function as having a named vararg slot
        child->setNamedVarargs(varargReg, false);
    }

    childGen.genBlock(body);
    // Ensure the final RETURN is attributed to the function's end line,
    // so debug.getinfo().activelines includes the last line.
    childGen.setLine(lastLineDefined);
    childGen.emitAB(ROpCode::ROP_RETURN, 0, 1);

    int nup = static_cast<int>(childGen.upvalues_.size());
    child->setUpvalueCount(nup);
    for (const auto& uv : childGen.upvalues_) {
        child->addUpvalueName(uv.name);
    }
    child->chunk()->setRCode(std::move(childGen.code_), childGen.maxReg_, std::move(childGen.lines_));

    auto& lvars = const_cast<std::vector<LocalVarInfo>&>(child->localVars());
    for (auto& lv : lvars) {
        if (lv.endPC == (size_t)-1) {
            lv.endPC = child->chunk()->rcode().size();
        }
    }

    outUpvalues = std::move(childGen.upvalues_);
    return child;
}

// === Expression visitors ===

void RCodeGen::visitLiteral(LiteralNode* node) {
    setLine(node->line());
    if (node->isLargeInt()) {
        size_t idx = func_->chunk()->addInt64(node->largeInt());
        int c = addConstant(Value::compileTimeInt64(idx));
        int reg = allocReg();
        emitABx(ROpCode::ROP_LOADK, reg, c);
        exprReg_ = reg;
        return;
    }

    int reg = allocReg();
    const Value& v = node->value();
    if (v.isNil()) {
        emitAB(ROpCode::ROP_LOADNIL, reg, 0);
    } else if (v.isBool()) {
        emitAB(ROpCode::ROP_LOADBOOL, reg, v.asBool() ? 1 : 0);
    } else {
        int c = addConstant(v);
        emitABx(ROpCode::ROP_LOADK, reg, c);
    }
    exprReg_ = reg;
}

void RCodeGen::visitStringLiteral(StringLiteralNode* node) {
    setLine(node->line());
    int reg = allocReg();
    size_t strIdx = func_->chunk()->addString(node->content());
    Value strVal = Value::string(strIdx);
    int c = addConstant(strVal);
    emitABx(ROpCode::ROP_LOADK, reg, c);
    exprReg_ = reg;
}

void RCodeGen::visitUnary(UnaryNode* node) {
    setLine(node->line());
    int operand = genExpr(node->operand());
    int reg = allocReg();
    switch (node->op()) {
        case TokenType::MINUS:
            emitAB(ROpCode::ROP_NEG, reg, operand);
            break;
        case TokenType::NOT:
            emitAB(ROpCode::ROP_NOT, reg, operand);
            break;
        case TokenType::HASH:
            emitAB(ROpCode::ROP_LEN, reg, operand);
            break;
        case TokenType::TILDE:
            emitAB(ROpCode::ROP_BNOT, reg, operand);
            break;
        default:
            emitAB(ROpCode::ROP_MOVE, reg, operand);
            break;
    }
    freeReg(operand);
    exprReg_ = reg;
}

void RCodeGen::visitBinary(BinaryNode* node) {
    setLine(node->line());
    // Short-circuit logical operators
    if (node->op() == TokenType::AND) {
        int reg = allocReg();
        reserveReg(reg);
        genExpr(node->left(), reg);
        emitABC(ROpCode::ROP_TEST, reg, 0, 0); // if falsey, skip jump
        size_t jmpFalse = emitJump(ROpCode::ROP_JMP, 0);
        genExpr(node->right(), reg);
        patchJump(jmpFalse, code_.size());
        unreserveReg(reg);
        exprReg_ = reg;
        return;
    }
    if (node->op() == TokenType::OR) {
        int reg = allocReg();
        reserveReg(reg);
        genExpr(node->left(), reg);
        emitABC(ROpCode::ROP_TEST, reg, 0, 1); // if truthy, skip jump
        size_t jmpTrue = emitJump(ROpCode::ROP_JMP, 0);
        genExpr(node->right(), reg);
        patchJump(jmpTrue, code_.size());
        unreserveReg(reg);
        exprReg_ = reg;
        return;
    }

    int left = genExpr(node->left());
    reserveReg(left);
    int right = genExpr(node->right());
    unreserveReg(left);
    reserveReg(left);
    reserveReg(right);
    int reg = allocReg();
    unreserveReg(right);
    unreserveReg(left);

    switch (node->op()) {
        case TokenType::PLUS: emitABC(ROpCode::ROP_ADD, reg, left, right); break;
        case TokenType::MINUS: emitABC(ROpCode::ROP_SUB, reg, left, right); break;
        case TokenType::STAR: emitABC(ROpCode::ROP_MUL, reg, left, right); break;
        case TokenType::SLASH: emitABC(ROpCode::ROP_DIV, reg, left, right); break;
        case TokenType::SLASH_SLASH: emitABC(ROpCode::ROP_IDIV, reg, left, right); break;
        case TokenType::PERCENT: emitABC(ROpCode::ROP_MOD, reg, left, right); break;
        case TokenType::CARET: emitABC(ROpCode::ROP_POW, reg, left, right); break;
        case TokenType::AMPERSAND: emitABC(ROpCode::ROP_BAND, reg, left, right); break;
        case TokenType::PIPE: emitABC(ROpCode::ROP_BOR, reg, left, right); break;
        case TokenType::TILDE: emitABC(ROpCode::ROP_BXOR, reg, left, right); break;
        case TokenType::LESS_LESS: emitABC(ROpCode::ROP_SHL, reg, left, right); break;
        case TokenType::GREATER_GREATER: emitABC(ROpCode::ROP_SHR, reg, left, right); break;
        case TokenType::DOT_DOT: emitABC(ROpCode::ROP_CONCAT, reg, left, right); break;

        // Comparisons
        case TokenType::EQUAL_EQUAL:
            emitABC(ROpCode::ROP_EQ, 0, left, right);
            emitABC(ROpCode::ROP_LOADBOOL, reg, 0, 1);
            emitABC(ROpCode::ROP_LOADBOOL, reg, 1, 0);
            break;
        case TokenType::TILDE_EQUAL:
            emitABC(ROpCode::ROP_EQ, 1, left, right);
            emitABC(ROpCode::ROP_LOADBOOL, reg, 0, 1);
            emitABC(ROpCode::ROP_LOADBOOL, reg, 1, 0);
            break;
        case TokenType::LESS:
            emitABC(ROpCode::ROP_LT, 0, left, right);
            emitABC(ROpCode::ROP_LOADBOOL, reg, 0, 1);
            emitABC(ROpCode::ROP_LOADBOOL, reg, 1, 0);
            break;
        case TokenType::GREATER:
            emitABC(ROpCode::ROP_LT, 0, right, left);
            emitABC(ROpCode::ROP_LOADBOOL, reg, 0, 1);
            emitABC(ROpCode::ROP_LOADBOOL, reg, 1, 0);
            break;
        case TokenType::LESS_EQUAL:
            emitABC(ROpCode::ROP_LE, 0, left, right);
            emitABC(ROpCode::ROP_LOADBOOL, reg, 0, 1);
            emitABC(ROpCode::ROP_LOADBOOL, reg, 1, 0);
            break;
        case TokenType::GREATER_EQUAL:
            emitABC(ROpCode::ROP_LE, 0, right, left);
            emitABC(ROpCode::ROP_LOADBOOL, reg, 0, 1);
            emitABC(ROpCode::ROP_LOADBOOL, reg, 1, 0);
            break;

        default:
            emitABC(ROpCode::ROP_ADD, reg, left, right);
            break;
    }
    exprReg_ = reg;
    freeReg(left);
    freeReg(right);
}

void RCodeGen::visitVariable(VariableExprNode* node) {
    setLine(node->line());
    int reg = allocReg();
    int local = findLocal(node->name());
    if (local >= 0) {
        emitAB(ROpCode::ROP_MOVE, reg, local);
    } else {
        int uv = resolveUpvalue(node->name());
        if (uv >= 0) {
            emitAB(ROpCode::ROP_GETUPVAL, reg, uv);
        } else {
            // Global: _ENV[name]
            int envIdx = resolveUpvalue("_ENV");
            if (envIdx == -1) envIdx = 0;
            size_t strIdx = func_->chunk()->addString(node->name());
            Value nameVal = Value::string(strIdx);
            int c = addConstant(nameVal);
            emitGetTabUp(reg, envIdx, c);
        }
    }
    exprReg_ = reg;
}

void RCodeGen::visitVararg(VarargExprNode* node) {
    setLine(node->line());
    exprReg_ = genMultiRes(node, 1, -1);
}

int RCodeGen::genCall(ExprNode* calleeNode, const std::vector<std::unique_ptr<ExprNode>>& args, int wantedResults, int destReg, int methodReg, int selfReg) {
    int prefixArgs = (selfReg >= 0 ? 1 : 0);
    int argc = static_cast<int>(args.size());
    int totalArgs = prefixArgs + argc;
    bool hasMultires = (argc > 0 && isMultiRes(args.back().get()));

    int funcReg;
    bool needMoveToDest = false;
    if (destReg >= 0) {
        bool canUseDest = !isLocalReg(destReg);
        if (canUseDest) {
            for (int r = destReg + 1; r <= destReg + totalArgs; ++r) {
                if (isLocalReg(r) || isReservedReg(r)) {
                    canUseDest = false;
                    break;
                }
            }
        }
        if (canUseDest) {
            funcReg = destReg;
            if (funcReg + 1 + totalArgs > nextReg_) {
                nextReg_ = funcReg + 1 + totalArgs;
            }
        } else {
            while (isLocalReg(nextReg_) || isReservedReg(nextReg_)) {
                nextReg_++;
            }
            funcReg = nextReg_;
            nextReg_ += 1 + totalArgs;
            needMoveToDest = true;
        }
    } else {
        while (isLocalReg(nextReg_) || isReservedReg(nextReg_)) {
            nextReg_++;
        }
        funcReg = nextReg_;
        nextReg_ += 1 + totalArgs;
    }
    maxReg_ = std::max(maxReg_, nextReg_);

    for (int r = funcReg; r <= funcReg + totalArgs; ++r) {
        reserveReg(r);
    }

    if (methodReg >= 0) {
        if (methodReg != funcReg) {
            emitAB(ROpCode::ROP_MOVE, funcReg, methodReg);
            freeReg(methodReg);
        }
    } else if (calleeNode) {
        int callee = genExpr(calleeNode);
        if (callee != funcReg) {
            emitAB(ROpCode::ROP_MOVE, funcReg, callee);
            freeReg(callee);
        }
    }

    if (selfReg >= 0) {
        int targetSelf = funcReg + 1;
        if (selfReg != targetSelf) {
            emitAB(ROpCode::ROP_MOVE, targetSelf, selfReg);
            freeReg(selfReg);
        }
    }

    int fixedArgCount = hasMultires ? (argc - 1) : argc;
    for (int i = 0; i < fixedArgCount; ++i) {
        int target = funcReg + 1 + prefixArgs + i;
        int argR = genExpr(args[i].get(), target);
        if (argR != target) {
            emitAB(ROpCode::ROP_MOVE, target, argR);
            freeReg(argR);
        }
    }

    int cParam = (wantedResults < 0) ? 0 : (wantedResults + 1);

    if (hasMultires) {
        int target = funcReg + 1 + prefixArgs + fixedArgCount;
        genMultiRes(args.back().get(), -1, target);
        emitABC(ROpCode::ROP_CALL, funcReg, 0, cParam); // B=0: multires args
    } else {
        emitABC(ROpCode::ROP_CALL, funcReg, totalArgs + 1, cParam);
    }

    for (int i = 1; i <= totalArgs; ++i) {
        unreserveReg(funcReg + i);
        freeReg(funcReg + i);
    }
    unreserveReg(funcReg);

    if (needMoveToDest) {
        if (destReg != funcReg) {
            int numResults = (wantedResults > 0) ? wantedResults : 1;
            for (int k = 0; k < numResults; ++k) {
                emitAB(ROpCode::ROP_MOVE, destReg + k, funcReg + k);
                freeReg(funcReg + k);
            }
            return destReg;
        }
    }

    if (destReg < 0 && wantedResults == 0) {
        freeReg(funcReg);
    }

    return funcReg;
}

void RCodeGen::visitCall(CallExprNode* node) {
    setLine(node->line());
    exprReg_ = genMultiRes(node, 1, -1);
}

void RCodeGen::visitMethodCall(MethodCallExprNode* node) {
    setLine(node->line());
    exprReg_ = genMultiRes(node, 1, -1);
}

void RCodeGen::visitTableConstructor(TableConstructorNode* node) {
    setLine(node->line());
    int reg = allocReg();
    reserveReg(reg);
    emitABC(ROpCode::ROP_NEWTABLE, reg, 0, 0);
    int arrayIndex = 1;
    const auto& entries = node->entries();
    size_t count = entries.size();
    for (size_t idx = 0; idx < count; ++idx) {
        const auto& entry = entries[idx];
        if (entry.key) {
            int k = genExpr(entry.key.get());
            reserveReg(k);
            int v = genExpr(entry.value.get());
            unreserveReg(k);
            emitABC(ROpCode::ROP_SETTABLE, reg, k, v);
            freeReg(v);
            freeReg(k);
        } else {
            bool isLast = (idx == count - 1);
            bool lastMulti = isLast && isMultiRes(entry.value.get());
            if (lastMulti) {
                int k = nextReg_;
                nextReg_ += 2;
                maxReg_ = std::max(maxReg_, nextReg_);
                reserveReg(k);
                reserveReg(k + 1);
                int c = addConstant(Value::integer(arrayIndex));
                emitABx(ROpCode::ROP_LOADK, k, c);
                int target = k + 1;
                genMultiRes(entry.value.get(), -1, target);
                emitAB(ROpCode::ROP_SETTABLEMULTI, reg, k);
                unreserveReg(target);
                unreserveReg(k);
                freeReg(target);
                freeReg(k);
            } else {
                int v = genExpr(entry.value.get());
                int k = allocReg();
                int c = addConstant(Value::integer(arrayIndex++));
                emitABx(ROpCode::ROP_LOADK, k, c);
                emitABC(ROpCode::ROP_SETTABLE, reg, k, v);
                freeReg(k);
                freeReg(v);
            }
        }
    }
    unreserveReg(reg);
    exprReg_ = reg;
}

void RCodeGen::visitIndexExpr(IndexExprNode* node) {
    setLine(node->line());
    int tbl = genExpr(node->table());
    reserveReg(tbl);
    int key = genExpr(node->key());
    unreserveReg(tbl);
    reserveReg(tbl);
    reserveReg(key);
    int reg = allocReg();
    unreserveReg(key);
    unreserveReg(tbl);
    emitABC(ROpCode::ROP_GETTABLE, reg, tbl, key);
    freeReg(key);
    freeReg(tbl);
    exprReg_ = reg;
}

void RCodeGen::visitFunctionExpr(FunctionExprNode* node) {
    setLine(node->lastLineDefined() > 0 ? node->lastLineDefined() : node->line());
    std::vector<UpvalueInfo> uvs;
    FunctionObject* child = compileFunctionBody("", node->params(), node->body(), node->hasVarargs(), node->varargName(), uvs, node->lineDefined(), node->lastLineDefined());
    size_t funcIdx = func_->chunk()->addFunction(child);
    int constIdx = addConstant(Value::function(funcIdx));
    int reg = allocReg();
    emitABx(ROpCode::ROP_CLOSURE, reg, constIdx);
    for (const auto& uv : uvs) {
        code_.push_back(ropEncodeABx(ROpCode::ROP_CLOSURE, uv.isLocal ? 1 : 0, static_cast<uint16_t>(uv.index)));
        lines_.push_back(currentLine_);
    }
    exprReg_ = reg;
}

void RCodeGen::visitGroupExpr(GroupExprNode* node) {
    setLine(node->line());
    node->expr()->accept(*this);
}

// === Statement visitors ===

void RCodeGen::visitExprStmt(ExprStmtNode* node) {
    setLine(node->line());
    if (isMultiRes(node->expr())) {
        genMultiRes(node->expr(), 0, -1);
    } else {
        int r = genExpr(node->expr());
        freeReg(r);
    }
}

void RCodeGen::visitAssignmentStmt(AssignmentStmtNode* node) {
    setLine(node->line());
    const LocalVar* lvar = findLocalVar(node->name());
    if (lvar) {
        if (lvar->isConst) {
            throw CompileError("attempt to assign to const variable '" + node->name() + "'", node->line());
        }
        int val = genExpr(node->value(), lvar->reg);
        if (val != lvar->reg) {
            emitAB(ROpCode::ROP_MOVE, lvar->reg, val);
            freeReg(val);
        }
        return;
    }

    int uv = resolveUpvalue(node->name());
    if (uv >= 0) {
        if (upvalues_[uv].isConst) {
            throw CompileError("attempt to assign to const variable '" + node->name() + "'", node->line());
        }
        int val = genExpr(node->value());
        emitAB(ROpCode::ROP_SETUPVAL, val, uv);
        freeReg(val);
        return;
    }

    int envIdx = resolveUpvalue("_ENV");
    if (envIdx == -1) envIdx = 0;
    size_t strIdx = func_->chunk()->addString(node->name());
    Value nameVal = Value::string(strIdx);
    int c = addConstant(nameVal);
    int val = genExpr(node->value());
    emitSetTabUp(val, envIdx, c);
    freeReg(val);
}

void RCodeGen::visitIndexAssignmentStmt(IndexAssignmentStmtNode* node) {
    setLine(node->line());
    int tbl = genExpr(node->table());
    reserveReg(tbl);
    int key = genExpr(node->key());
    reserveReg(key);
    int val = genExpr(node->value());
    unreserveReg(key);
    unreserveReg(tbl);
    emitABC(ROpCode::ROP_SETTABLE, tbl, key, val);
    freeReg(val);
    freeReg(key);
    freeReg(tbl);
}

void RCodeGen::visitLocalDeclStmt(LocalDeclStmtNode* node) {
    setLine(node->line());
    if (node->isFunction()) {
        int reg = allocLocal(node->name(), node->isConstant(), node->isClose());
        if (node->initializer()) {
            int val = genExpr(node->initializer(), reg);
            if (val != reg) {
                emitAB(ROpCode::ROP_MOVE, reg, val);
                freeReg(val);
            }
        } else {
            emitAB(ROpCode::ROP_LOADNIL, reg, 0);
        }
        resetTemps(reg + 1);
    } else {
        while (isLocalReg(nextReg_) || isReservedReg(nextReg_)) {
            nextReg_++;
        }
        int reg = nextReg_++;
        maxReg_ = std::max(maxReg_, nextReg_);
        reserveReg(reg);
        if (node->initializer()) {
            int val = genExpr(node->initializer(), reg);
            if (val != reg) {
                emitAB(ROpCode::ROP_MOVE, reg, val);
                freeReg(val);
            }
        } else {
            emitAB(ROpCode::ROP_LOADNIL, reg, 0);
        }
        unreserveReg(reg);
        activateLocal(reg, node->name(), node->isConstant(), node->isClose());
        if (node->isClose()) {
            size_t strIdx = func_->chunk()->addString(node->name());
            int c = addConstant(Value::string(strIdx));
            emitABx(ROpCode::ROP_TBC, reg, c);
        }
        resetTemps(reg + 1);
    }
}

void RCodeGen::visitMultipleLocalDeclStmt(MultipleLocalDeclStmtNode* node) {
    setLine(node->line());
    const auto& vars = node->vars();
    const auto& inits = node->initializers();
    size_t varCount = vars.size();
    size_t initCount = inits.size();

    std::vector<int> varRegs;
    for (size_t i = 0; i < varCount; ++i) {
        while (isLocalReg(nextReg_) || isReservedReg(nextReg_)) {
            nextReg_++;
        }
        int r = nextReg_++;
        reserveReg(r);
        varRegs.push_back(r);
    }
    maxReg_ = std::max(maxReg_, nextReg_);

    if (initCount == 0) {
        for (size_t i = 0; i < varCount; ++i) {
            emitAB(ROpCode::ROP_LOADNIL, varRegs[i], 0);
        }
    } else {
        bool hasMultires = isMultiRes(inits.back().get());
        for (size_t i = 0; i < varCount; ++i) {
            if (i < initCount - 1 || (i == initCount - 1 && !hasMultires)) {
                int val = genExpr(inits[i].get(), varRegs[i]);
                if (val != varRegs[i]) {
                    emitAB(ROpCode::ROP_MOVE, varRegs[i], val);
                    freeReg(val);
                }
            } else if (i == initCount - 1 && hasMultires) {
                int needed = static_cast<int>(varCount - i);
                genMultiRes(inits.back().get(), needed, varRegs[i]);
                break;
            } else {
                emitAB(ROpCode::ROP_LOADNIL, varRegs[i], 0);
            }
        }
    }

    for (size_t i = 0; i < varCount; ++i) {
        unreserveReg(varRegs[i]);
        activateLocal(varRegs[i], vars[i].name, vars[i].isConstant, vars[i].isClose);
    }

    for (size_t i = 0; i < varCount; ++i) {
        if (vars[i].isClose) {
            size_t strIdx = func_->chunk()->addString(vars[i].name);
            int c = addConstant(Value::string(strIdx));
            emitABx(ROpCode::ROP_TBC, varRegs[i], c);
        }
    }

    if (!varRegs.empty()) {
        resetTemps(varRegs.back() + 1);
    }
}

void RCodeGen::visitMultipleAssignmentStmt(MultipleAssignmentStmtNode* node) {
    setLine(node->line());
    const auto& targets = node->targets();
    const auto& values = node->values();
    size_t targetCount = targets.size();
    size_t valCount = values.size();

    // Check for reassignment to const variables
    for (const auto& tgt : targets) {
        if (auto* varNode = dynamic_cast<VariableExprNode*>(tgt.get())) {
            const LocalVar* lvar = findLocalVar(varNode->name());
            if (lvar && lvar->isConst) {
                throw CompileError("attempt to assign to const variable '" + varNode->name() + "'", node->line());
            }
            int uv = resolveUpvalue(varNode->name());
            if (uv >= 0 && upvalues_[uv].isConst) {
                throw CompileError("attempt to assign to const variable '" + varNode->name() + "'", node->line());
            }
        }
    }

    bool hasMultires = (valCount > 0 && isMultiRes(values.back().get())) && (targetCount >= valCount);

    std::vector<int> valRegs;
    size_t fixedValCount = hasMultires ? (valCount - 1) : valCount;
    for (size_t i = 0; i < fixedValCount; ++i) {
        int v = genExpr(values[i].get());
        reserveReg(v);
        valRegs.push_back(v);
    }

    if (hasMultires) {
        int needed = static_cast<int>(targetCount - fixedValCount);
        while (isLocalReg(nextReg_) || isReservedReg(nextReg_)) {
            nextReg_++;
        }
        int base = nextReg_;
        nextReg_ += needed;
        maxReg_ = std::max(maxReg_, nextReg_);
        for (int k = 0; k < needed; ++k) {
            reserveReg(base + k);
        }
        genMultiRes(values.back().get(), needed, base);
        for (int k = 0; k < needed; ++k) {
            valRegs.push_back(base + k);
        }
    }

    for (size_t i = 0; i < targetCount; ++i) {
        int val = (i < valRegs.size()) ? valRegs[i] : -1;
        if (val == -1) {
            val = allocReg();
            emitAB(ROpCode::ROP_LOADNIL, val, 0);
            valRegs.push_back(val);
        }

        if (auto* varNode = dynamic_cast<VariableExprNode*>(targets[i].get())) {
            int local = findLocal(varNode->name());
            if (local >= 0) {
                emitAB(ROpCode::ROP_MOVE, local, val);
            } else {
                int uv = resolveUpvalue(varNode->name());
                if (uv >= 0) {
                    emitAB(ROpCode::ROP_SETUPVAL, val, uv);
                } else {
                    int envIdx = resolveUpvalue("_ENV");
                    if (envIdx == -1) envIdx = 0;
                    size_t strIdx = func_->chunk()->addString(varNode->name());
                    int c = addConstant(Value::string(strIdx));
                    emitSetTabUp(val, envIdx, c);
                }
            }
        } else if (auto* idxNode = dynamic_cast<IndexExprNode*>(targets[i].get())) {
            int tbl = genExpr(idxNode->table());
            reserveReg(tbl);
            int key = genExpr(idxNode->key());
            unreserveReg(tbl);
            emitABC(ROpCode::ROP_SETTABLE, tbl, key, val);
            freeReg(key);
            freeReg(tbl);
        }
    }

    for (int r : valRegs) {
        unreserveReg(r);
        freeReg(r);
    }
}

void RCodeGen::visitGlobalDeclStmt(GlobalDeclStmtNode* node) {
    setLine(node->line());
    if (node->initializer()) {
        int val = genExpr(node->initializer());
        int envIdx = resolveUpvalue("_ENV");
        if (envIdx == -1) envIdx = 0;
        size_t strIdx = func_->chunk()->addString(node->name());
        int c = addConstant(Value::string(strIdx));
        emitSetTabUp(val, envIdx, c);
        freeReg(val);
    }
}

void RCodeGen::visitMultipleGlobalDeclStmt(MultipleGlobalDeclStmtNode* node) {
    setLine(node->line());
    const auto& vars = node->vars();
    const auto& inits = node->initializers();
    for (size_t i = 0; i < inits.size(); ++i) {
        if (i < vars.size()) {
            int val = genExpr(inits[i].get());
            int envIdx = resolveUpvalue("_ENV");
            if (envIdx == -1) envIdx = 0;
            size_t strIdx = func_->chunk()->addString(vars[i].name);
            int c = addConstant(Value::string(strIdx));
            emitSetTabUp(val, envIdx, c);
            freeReg(val);
        }
    }
}

void RCodeGen::visitIfStmt(IfStmtNode* node) {
    setLine(node->line());
    std::vector<size_t> endJumps;

    int cond = genExpr(node->condition());
    emitAB(ROpCode::ROP_TEST, cond, 0);  // if truthy, skip next JMP
    freeReg(cond);
    size_t elseJump = emitJump(ROpCode::ROP_JMP, 0);
    genBlock(node->thenBranch());
    // The jump to end should be attributed to the 'end' line for correct
    // debug hook tracing.
    if (node->endLine() > 0) {
        setLine(node->endLine());
    }
    endJumps.push_back(emitJump(ROpCode::ROP_JMP, 0));

    for (const auto& eib : node->elseIfBranches()) {
        patchJump(elseJump, code_.size());
        setLine(eib.condition->line());
        int c = genExpr(eib.condition.get());
        emitAB(ROpCode::ROP_TEST, c, 0);
        freeReg(c);
        elseJump = emitJump(ROpCode::ROP_JMP, 0);
        genBlock(eib.body);
        endJumps.push_back(emitJump(ROpCode::ROP_JMP, 0));
    }

    patchJump(elseJump, code_.size());
    genBlock(node->elseBranch());

    for (size_t j : endJumps) patchJump(j, code_.size());
    // Reset line to the 'end' line so following instructions get correct line
    if (node->endLine() > 0) {
        setLine(node->endLine());
    }
}

void RCodeGen::visitWhileStmt(WhileStmtNode* node) {
    setLine(node->line());
    size_t loopStart = code_.size();
    int cond = genExpr(node->condition());
    emitAB(ROpCode::ROP_TEST, cond, 0);
    freeReg(cond);
    if (node->endLine() > 0) {
        setLine(node->endLine());
    }
    size_t endJump = emitJump(ROpCode::ROP_JMP, 0);

    breakJumps_.push_back({});
    genBlock(node->body());

    size_t backJmp = emitJump(ROpCode::ROP_JMP, 0);
    patchJump(backJmp, loopStart);

    patchJump(endJump, code_.size());

    for (size_t b : breakJumps_.back()) patchJump(b, code_.size());
    breakJumps_.pop_back();
    if (node->endLine() > 0) {
        setLine(node->endLine());
    }
}

void RCodeGen::visitRepeatStmt(RepeatStmtNode* node) {
    setLine(node->line());
    size_t loopStart = code_.size();
    breakJumps_.push_back({});

    pushScope();
    for (const auto& s : node->body()) genStmt(s.get());

    int cond = genExpr(node->condition());
    emitAB(ROpCode::ROP_TEST, cond, 0); // if truthy, skip JMP (exits loop)
    freeReg(cond);

    size_t jmp = emitJump(ROpCode::ROP_JMP, 0);
    patchJump(jmp, loopStart);

    // Set line before popScope so cleanup gets the end line
    if (node->endLine() > 0) {
        setLine(node->endLine());
    }
    popScope();

    for (size_t b : breakJumps_.back()) patchJump(b, code_.size());
    breakJumps_.pop_back();
}

void RCodeGen::visitForStmt(ForStmtNode* node) {
    setLine(node->line());
    int base = nextReg_;
    nextReg_ += 3;  // Reserve 3 registers: index, limit, step
    maxReg_ = std::max(maxReg_, nextReg_);

    int s = genExpr(node->start(), base);
    if (s != base) { emitAB(ROpCode::ROP_MOVE, base, s); freeReg(s); }
    int e = genExpr(node->end(), base + 1);
    if (e != base + 1) { emitAB(ROpCode::ROP_MOVE, base + 1, e); freeReg(e); }
    if (node->step()) {
        int st = genExpr(node->step(), base + 2);
        if (st != base + 2) { emitAB(ROpCode::ROP_MOVE, base + 2, st); freeReg(st); }
    } else {
        Value one = Value::integer(1);
        int c = addConstant(one);
        emitABx(ROpCode::ROP_LOADK, base + 2, c);
    }

    resetTemps(base + 3);

    size_t prepPc = emitJump(ROpCode::ROP_FORPREP, base);

    size_t loopStart = code_.size();
    pushScope();
    int loopVar = allocLocal(node->varName());
    (void)loopVar;

    breakJumps_.push_back({});
    for (const auto& s : node->body()) genStmt(s.get());

    popScope();

    setLine(node->line());
    size_t loopPc = emitJump(ROpCode::ROP_FORLOOP, base);
    patchJump(loopPc, loopStart);
    patchJump(prepPc, loopPc);

    for (size_t b : breakJumps_.back()) patchJump(b, code_.size());
    if (node->endLine() > 0) {
        setLine(node->endLine());
    }
    breakJumps_.pop_back();
}

void RCodeGen::visitForInStmt(ForInStmtNode* node) {
    setLine(node->line());
    int base = nextReg_;
    nextReg_ += 3;
    maxReg_ = std::max(maxReg_, nextReg_);

    const auto& iters = node->iterators();
    if (iters.size() == 1 && isMultiRes(iters[0].get())) {
        int callReg = nextReg_;
        nextReg_ += 3;
        maxReg_ = std::max(maxReg_, nextReg_);
        genMultiRes(iters[0].get(), 3, callReg);
        emitAB(ROpCode::ROP_MOVE, base, callReg);
        emitAB(ROpCode::ROP_MOVE, base + 1, callReg + 1);
        emitAB(ROpCode::ROP_MOVE, base + 2, callReg + 2);
    } else {
        for (int i = 0; i < 3; ++i) {
            if (i < static_cast<int>(iters.size())) {
                int r = genExpr(iters[i].get());
                emitAB(ROpCode::ROP_MOVE, base + i, r);
                freeReg(r);
            } else {
                emitAB(ROpCode::ROP_LOADNIL, base + i, 0);
            }
        }
    }

    pushScope();
    int varCount = static_cast<int>(node->varNames().size());
    std::vector<int> varRegs;
    for (int i = 0; i < varCount; ++i) {
        varRegs.push_back(allocLocal(node->varNames()[i]));
    }

    size_t loopStart = code_.size();
    breakJumps_.push_back({});

    int callReg = nextReg_;
    nextReg_ += std::max(3, varCount);
    maxReg_ = std::max(maxReg_, nextReg_);
    emitAB(ROpCode::ROP_MOVE, callReg, base);
    emitAB(ROpCode::ROP_MOVE, callReg + 1, base + 1);
    emitAB(ROpCode::ROP_MOVE, callReg + 2, base + 2);
    emitABC(ROpCode::ROP_CALL, callReg, 3, varCount + 1);

    for (int i = 0; i < varCount; ++i) {
        emitAB(ROpCode::ROP_MOVE, varRegs[i], callReg + i);
    }

    // If var_1 == nil, break!
    int nilReg = allocReg();
    emitAB(ROpCode::ROP_LOADNIL, nilReg, 0);
    emitABC(ROpCode::ROP_EQ, 1, varRegs[0], nilReg); // if var_1 != nil, skip next jump
    freeReg(nilReg);
    size_t breakJump = emitJump(ROpCode::ROP_JMP, 0);

    // Update state var
    emitAB(ROpCode::ROP_MOVE, base + 2, varRegs[0]);

    genBlock(node->body());

    size_t backJmp = emitJump(ROpCode::ROP_JMP, 0);
    patchJump(backJmp, loopStart);

    patchJump(breakJump, code_.size());
    for (size_t b : breakJumps_.back()) patchJump(b, code_.size());
    breakJumps_.pop_back();

    // Set line BEFORE popScope so cleanup instructions get the end line,
    // not the body line. This ensures the debug hook fires for the correct
    // line when breaking out of the loop.
    if (node->endLine() > 0) {
        setLine(node->endLine());
    }
    popScope();
}

void RCodeGen::visitFunctionDecl(FunctionDeclNode* node) {
    setLine(node->line());
    std::vector<UpvalueInfo> uvs;
    FunctionObject* child = compileFunctionBody(node->name(), node->params(), node->body(), node->hasVarargs(), node->varargName(), uvs, node->line(), node->line());
    size_t funcIdx = func_->chunk()->addFunction(child);
    int constIdx = addConstant(Value::function(funcIdx));
    int reg = allocReg();
    emitABx(ROpCode::ROP_CLOSURE, reg, constIdx);
    for (const auto& uv : uvs) {
        code_.push_back(ropEncodeABx(ROpCode::ROP_CLOSURE, uv.isLocal ? 1 : 0, static_cast<uint16_t>(uv.index)));
        lines_.push_back(currentLine_);
    }

    int local = findLocal(node->name());
    if (local >= 0) {
        emitAB(ROpCode::ROP_MOVE, local, reg);
    } else {
        int envIdx = resolveUpvalue("_ENV");
        if (envIdx == -1) envIdx = 0;
        size_t strIdx = func_->chunk()->addString(node->name());
        Value nameVal = Value::string(strIdx);
        int c = addConstant(nameVal);
        emitSetTabUp(reg, envIdx, c);
    }
    freeReg(reg);
}

void RCodeGen::visitReturn(ReturnStmtNode* node) {
    setLine(node->line());
    const auto& values = node->values();
    if (values.empty()) {
        emitAB(ROpCode::ROP_RETURN, 0, 1);  // return 0 values
        return;
    }

    bool hasMultires = isMultiRes(values.back().get());

    if (values.size() == 1 && !hasMultires) {
        int val = genExpr(values[0].get());
        emitABC(ROpCode::ROP_RETURN, val, 2, 0);  // return 1 value
        freeReg(val);
        return;
    }

    while (isLocalReg(nextReg_) || isReservedReg(nextReg_)) {
        nextReg_++;
    }
    int base = nextReg_;
    nextReg_ += values.size();
    maxReg_ = std::max(maxReg_, nextReg_);

    for (size_t i = 0; i < values.size(); ++i) {
        reserveReg(base + (int)i);
    }

    size_t fixedCount = hasMultires ? (values.size() - 1) : values.size();
    for (size_t i = 0; i < fixedCount; ++i) {
        int target = base + (int)i;
        int val = genExpr(values[i].get(), target);
        if (val != target) {
            emitAB(ROpCode::ROP_MOVE, target, val);
            freeReg(val);
        }
    }

    if (hasMultires) {
        int target = base + (int)fixedCount;
        genMultiRes(values.back().get(), -1, target);
        emitABC(ROpCode::ROP_RETURN, base, 0, 0); // B=0: multires return
    } else {
        emitABC(ROpCode::ROP_RETURN, base, (int)values.size() + 1, 0);
    }

    for (size_t i = 0; i < values.size(); ++i) {
        unreserveReg(base + (int)i);
    }
}

void RCodeGen::visitBreak(BreakStmtNode* node) {
    setLine(node->line());
    if (!breakJumps_.empty()) {
        breakJumps_.back().push_back(emitJump(ROpCode::ROP_JMP, 0));
    }
}

void RCodeGen::visitGoto(GotoStmtNode* node) {
    setLine(node->line());
    auto it = labels_.find(node->label());
    if (it != labels_.end()) {
        size_t jmp = emitJump(ROpCode::ROP_JMP, 0);
        patchJump(jmp, it->second.targetPc);
    } else {
        size_t jmp = emitJump(ROpCode::ROP_JMP, 0);
        pendingGotos_.push_back({node->label(), jmp, getActiveLocals(), node->line()});
    }
}

void RCodeGen::visitLabel(LabelStmtNode* node) {
    setLine(node->line());
    LabelInfo lbl;
    lbl.targetPc = code_.size();
    lbl.activeLocals = getActiveLocals();
    lbl.line = node->line();
    labels_[node->label()] = lbl;

    for (auto it = pendingGotos_.begin(); it != pendingGotos_.end(); ) {
        if (it->name == node->label()) {
            for (const auto& var : lbl.activeLocals) {
                if (std::find(it->activeLocals.begin(), it->activeLocals.end(), var) == it->activeLocals.end()) {
                    throw CompileError("<goto " + it->name + "> jumps into the scope of '" + var + "'", it->line);
                }
            }
            patchJump(it->jumpPc, lbl.targetPc);
            it = pendingGotos_.erase(it);
        } else {
            ++it;
        }
    }
}

void RCodeGen::visitBlock(BlockStmtNode* node) {
    setLine(node->line());
    genBlock(node->statements());
}

void RCodeGen::visitProgram(ProgramNode* node) {
    setLine(node->line());
    genBlock(node->statements());
}

FunctionObject* RCodeGen::compileFunction(FunctionExprNode* func) {
    std::vector<UpvalueInfo> uvs;
    return compileFunctionBody("", func->params(), func->body(), func->hasVarargs(), func->varargName(), uvs, func->lineDefined(), func->lastLineDefined());
}

FunctionObject* RCodeGen::compileFunction(FunctionDeclNode* func) {
    std::vector<UpvalueInfo> uvs;
    return compileFunctionBody(func->name(), func->params(), func->body(), func->hasVarargs(), func->varargName(), uvs, func->line(), func->line());
}
