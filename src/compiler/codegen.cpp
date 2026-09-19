#include "compiler/codegen.hpp"
#include "value/function.hpp"
#include "value/string.hpp"

namespace {
class InnerRefChecker : public ASTVisitor {
public:
    explicit InnerRefChecker(const std::string& name) : name_(name) {}
    bool referenced = false;

    void check(const std::vector<std::unique_ptr<StmtNode>>& body) {
        for (const auto& stmt : body) {
            if (stmt) stmt->accept(*this);
            if (referenced) return;
        }
    }

    void visitLiteral(LiteralNode*) override {}
    void visitStringLiteral(StringLiteralNode*) override {}
    void visitVariable(VariableExprNode* node) override {
        if (node->name() == name_) referenced = true;
    }
    void visitVararg(VarargExprNode*) override {}
    void visitUnary(UnaryNode* n) override { if (n->operand()) n->operand()->accept(*this); }
    void visitBinary(BinaryNode* n) override { if (n->left()) n->left()->accept(*this); if (n->right()) n->right()->accept(*this); }
    void visitGroupExpr(GroupExprNode* n) override { if (n->expr()) n->expr()->accept(*this); }
    void visitCall(CallExprNode* n) override { if (n->callee()) n->callee()->accept(*this); for (const auto& a : n->args()) if (a) a->accept(*this); }
    void visitMethodCall(MethodCallExprNode* n) override { if (n->object()) n->object()->accept(*this); for (const auto& a : n->args()) if (a) a->accept(*this); }
    void visitTableConstructor(TableConstructorNode* n) override { for (const auto& e : n->entries()) { if (e.key) e.key->accept(*this); if (e.value) e.value->accept(*this); } }
    void visitIndexExpr(IndexExprNode* n) override { if (n->table()) n->table()->accept(*this); if (n->key()) n->key()->accept(*this); }
    void visitExprStmt(ExprStmtNode* n) override { if (n->expr()) n->expr()->accept(*this); }
    void visitAssignmentStmt(AssignmentStmtNode* n) override { if (n->name() == name_) referenced = true; if (n->value()) n->value()->accept(*this); }
    void visitIndexAssignmentStmt(IndexAssignmentStmtNode* n) override { if (n->table()) n->table()->accept(*this); if (n->key()) n->key()->accept(*this); if (n->value()) n->value()->accept(*this); }
    void visitMultipleAssignmentStmt(MultipleAssignmentStmtNode* n) override { for (const auto& t : n->targets()) if (t) t->accept(*this); for (const auto& v : n->values()) if (v) v->accept(*this); }
    void visitLocalDeclStmt(LocalDeclStmtNode* n) override { if (n->initializer()) n->initializer()->accept(*this); }
    void visitMultipleLocalDeclStmt(MultipleLocalDeclStmtNode* n) override { for (const auto& init : n->initializers()) if (init) init->accept(*this); }
    void visitGlobalDeclStmt(GlobalDeclStmtNode* n) override { if (n->initializer()) n->initializer()->accept(*this); }
    void visitMultipleGlobalDeclStmt(MultipleGlobalDeclStmtNode* n) override { for (const auto& init : n->initializers()) if (init) init->accept(*this); }
    void visitBlock(BlockStmtNode* n) override { for (const auto& s : n->statements()) if (s) s->accept(*this); }
    void visitIfStmt(IfStmtNode* n) override { if (n->condition()) n->condition()->accept(*this); for (const auto& s : n->thenBranch()) if (s) s->accept(*this); for (const auto& eb : n->elseIfBranches()) { if (eb.condition) eb.condition->accept(*this); for (const auto& s : eb.body) if (s) s->accept(*this); } for (const auto& s : n->elseBranch()) if (s) s->accept(*this); }
    void visitWhileStmt(WhileStmtNode* n) override { if (n->condition()) n->condition()->accept(*this); for (const auto& s : n->body()) if (s) s->accept(*this); }
    void visitRepeatStmt(RepeatStmtNode* n) override { for (const auto& s : n->body()) if (s) s->accept(*this); if (n->condition()) n->condition()->accept(*this); }
    void visitForStmt(ForStmtNode* n) override { if (n->start()) n->start()->accept(*this); if (n->end()) n->end()->accept(*this); if (n->step()) n->step()->accept(*this); for (const auto& s : n->body()) if (s) s->accept(*this); }
    void visitForInStmt(ForInStmtNode* n) override { for (const auto& it : n->iterators()) if (it) it->accept(*this); for (const auto& s : n->body()) if (s) s->accept(*this); }
    void visitFunctionDecl(FunctionDeclNode* n) override { check(n->body()); }
    void visitFunctionExpr(FunctionExprNode* n) override { check(n->body()); }
    void visitReturn(ReturnStmtNode* n) override { for (const auto& v : n->values()) if (v) v->accept(*this); }
    void visitBreak(BreakStmtNode*) override {}
    void visitGoto(GotoStmtNode*) override {}
    void visitLabel(LabelStmtNode*) override {}
    void visitProgram(ProgramNode* n) override { check(n->statements()); }
private:
    std::string name_;
};

class VarargEscapeChecker : public ASTVisitor {
public:
    explicit VarargEscapeChecker(const std::string& name) : name_(name) {
        if (name_ == "_ENV") {
            escapes = true;
        }
    }

    bool escapes = false;

    void check(const std::vector<std::unique_ptr<StmtNode>>& body) {
        for (const auto& stmt : body) {
            if (stmt) stmt->accept(*this);
            if (escapes) return;
        }
    }

    void visitLiteral(LiteralNode*) override {}
    void visitStringLiteral(StringLiteralNode*) override {}

    void visitVariable(VariableExprNode* node) override {
        if (node->name() == name_) {
            escapes = true;
        }
    }

    void visitVararg(VarargExprNode*) override {}

    void visitUnary(UnaryNode* node) override {
        if (node->operand()) node->operand()->accept(*this);
    }

    void visitBinary(BinaryNode* node) override {
        if (node->left()) node->left()->accept(*this);
        if (node->right()) node->right()->accept(*this);
    }

    void visitGroupExpr(GroupExprNode* node) override {
        if (node->expr()) node->expr()->accept(*this);
    }

    void visitCall(CallExprNode* node) override {
        if (auto* var = dynamic_cast<VariableExprNode*>(node->callee())) {
            if (var->name() == name_) escapes = true;
        }
        if (node->callee()) node->callee()->accept(*this);
        for (const auto& arg : node->args()) {
            if (arg) arg->accept(*this);
        }
    }

    void visitMethodCall(MethodCallExprNode* node) override {
        if (auto* var = dynamic_cast<VariableExprNode*>(node->object())) {
            if (var->name() == name_) escapes = true;
        }
        if (node->object()) node->object()->accept(*this);
        for (const auto& arg : node->args()) {
            if (arg) arg->accept(*this);
        }
    }

    void visitTableConstructor(TableConstructorNode* node) override {
        for (const auto& entry : node->entries()) {
            if (entry.key) entry.key->accept(*this);
            if (entry.value) entry.value->accept(*this);
        }
    }

    void visitIndexExpr(IndexExprNode* node) override {
        if (node->key()) node->key()->accept(*this);
        if (auto* var = dynamic_cast<VariableExprNode*>(node->table())) {
            if (var->name() == name_) {
                // Allowed read: t[k]
                return;
            }
        }
        if (node->table()) node->table()->accept(*this);
    }

    void visitExprStmt(ExprStmtNode* node) override {
        if (node->expr()) node->expr()->accept(*this);
    }

    void visitAssignmentStmt(AssignmentStmtNode* node) override {
        if (node->name() == name_) escapes = true;
        if (node->value()) node->value()->accept(*this);
    }

    void visitMultipleAssignmentStmt(MultipleAssignmentStmtNode* node) override {
        for (const auto& target : node->targets()) {
            if (auto* var = dynamic_cast<VariableExprNode*>(target.get())) {
                if (var->name() == name_) escapes = true;
            } else if (auto* idx = dynamic_cast<IndexExprNode*>(target.get())) {
                if (auto* tVar = dynamic_cast<VariableExprNode*>(idx->table())) {
                    if (tVar->name() == name_) escapes = true;
                }
            }
            if (target) target->accept(*this);
        }
        for (const auto& val : node->values()) {
            if (val) val->accept(*this);
        }
    }

    void visitIndexAssignmentStmt(IndexAssignmentStmtNode* node) override {
        if (auto* var = dynamic_cast<VariableExprNode*>(node->table())) {
            if (var->name() == name_) escapes = true;
        }
        if (node->table()) node->table()->accept(*this);
        if (node->key()) node->key()->accept(*this);
        if (node->value()) node->value()->accept(*this);
    }

    void visitLocalDeclStmt(LocalDeclStmtNode* node) override {
        if (node->initializer()) node->initializer()->accept(*this);
    }

    void visitMultipleLocalDeclStmt(MultipleLocalDeclStmtNode* node) override {
        for (const auto& init : node->initializers()) {
            if (init) init->accept(*this);
        }
    }

    void visitGlobalDeclStmt(GlobalDeclStmtNode* node) override {
        if (node->initializer()) node->initializer()->accept(*this);
    }

    void visitMultipleGlobalDeclStmt(MultipleGlobalDeclStmtNode* node) override {
        for (const auto& init : node->initializers()) {
            if (init) init->accept(*this);
        }
    }

    void visitBlock(BlockStmtNode* node) override {
        for (const auto& stmt : node->statements()) {
            if (stmt) stmt->accept(*this);
        }
    }

    void visitIfStmt(IfStmtNode* node) override {
        if (node->condition()) node->condition()->accept(*this);
        for (const auto& s : node->thenBranch()) if (s) s->accept(*this);
        for (const auto& eb : node->elseIfBranches()) {
            if (eb.condition) eb.condition->accept(*this);
            for (const auto& s : eb.body) if (s) s->accept(*this);
        }
        for (const auto& s : node->elseBranch()) if (s) s->accept(*this);
    }

    void visitWhileStmt(WhileStmtNode* node) override {
        if (node->condition()) node->condition()->accept(*this);
        for (const auto& s : node->body()) if (s) s->accept(*this);
    }

    void visitRepeatStmt(RepeatStmtNode* node) override {
        for (const auto& s : node->body()) if (s) s->accept(*this);
        if (node->condition()) node->condition()->accept(*this);
    }

    void visitForStmt(ForStmtNode* node) override {
        if (node->start()) node->start()->accept(*this);
        if (node->end()) node->end()->accept(*this);
        if (node->step()) node->step()->accept(*this);
        for (const auto& s : node->body()) if (s) s->accept(*this);
    }

    void visitForInStmt(ForInStmtNode* node) override {
        for (const auto& it : node->iterators()) if (it) it->accept(*this);
        for (const auto& s : node->body()) if (s) s->accept(*this);
    }

    void visitFunctionDecl(FunctionDeclNode* node) override {
        InnerRefChecker inner(name_);
        inner.check(node->body());
        if (inner.referenced) escapes = true;
    }

    void visitFunctionExpr(FunctionExprNode* node) override {
        InnerRefChecker inner(name_);
        inner.check(node->body());
        if (inner.referenced) escapes = true;
    }

    void visitReturn(ReturnStmtNode* node) override {
        for (const auto& val : node->values()) {
            if (val) val->accept(*this);
        }
    }

    void visitBreak(BreakStmtNode*) override {}
    void visitGoto(GotoStmtNode*) override {}
    void visitLabel(LabelStmtNode*) override {}
    void visitProgram(ProgramNode* node) override {
        check(node->statements());
    }

private:
    std::string name_;
};
} // anonymous namespace

CodeGenerator::CodeGenerator()
    : chunk_(nullptr), currentLine_(1), scopeDepth_(0), localCount_(0), enclosingCompiler_(nullptr),
      globalMode_(GlobalMode::ALL), envDeclaredGlobal_(false), expectedRetCount_(2) {}

std::unique_ptr<FunctionObject> CodeGenerator::generate(ProgramNode* program, const std::string& name) {
    chunk_ = std::make_unique<Chunk>();
    chunk_->setSourceName(name);
    upvalues_.clear();
    locals_.clear();
    scopeDepth_ = 0;
    localCount_ = 0;
    enclosingCompiler_ = nullptr; // Reset to top level
    globalMode_ = GlobalMode::ALL;
    declaredGlobals_.clear();
    shadowedLocals_.clear();
    envDeclaredGlobal_ = false;
    varSequence_ = 0;
    currentVarargName_ = "";
    namedVarargSlot_ = -1;
    isVarargOptimized_ = false;
    visibleLabels_.clear();
    pendingGotos_.clear();
    activeVars_.clear();
    blockScopes_.clear();
    gotosNeedingStubs_.clear();

    // Root compiler state initialization
    // _ENV is the first upvalue by convention in Lua 5.2+ for the top-level chunk.
    // When compiling any chunk (top-level or via load/require), we ensure _ENV is upvalue 0.
    Upvalue env;
    env.name = "_ENV";
    env.index = 0; 
    env.isLocal = false; 
    env.isConstant = false;
    upvalues_.push_back(env);

    // Generate code for the program

    program->accept(*this);

    // Emit return at end of normal execution
    emitReturn();

    // Resolve forward gotos (top level) - stubs are emitted here, after return
    emitGotoStubs();

    // Close remaining locals (top level)
    size_t endPC = currentChunk()->size();
    for (Local& l : locals_) {
        finishedLocals_.push_back({l.name, l.startPC, endPC, l.slot});
    }

    auto function = std::make_unique<FunctionObject>("", 0, std::move(chunk_), static_cast<int>(upvalues_.size()), true);
    for (const auto& uv : upvalues_) {
        function->addUpvalueName(uv.name);
    }
    for (const auto& l : finishedLocals_) {
        function->addLocalVar(l.name, l.startPC, l.endPC, l.slot);
    }
    return function;
}

void CodeGenerator::visitLiteral(LiteralNode* node) {
    setLine(node->line());

    if (node->isLargeInt()) {
        size_t idx = currentChunk()->addInt64(node->largeInt());
        emitConstant(Value::compileTimeInt64(idx));
        return;
    }

    const Value& value = node->value();

    // Use dedicated opcodes for common constants
    if (value.isNil()) {
        emitOpCode(OpCode::OP_NIL);
    } else if (value.isBool()) {
        emitOpCode(value.asBool() ? OpCode::OP_TRUE : OpCode::OP_FALSE);
    } else {
        emitConstant(value);
    }
}

void CodeGenerator::visitStringLiteral(StringLiteralNode* node) {
    setLine(node->line());

    // Intern the string in the chunk's string pool
    size_t stringIndex = currentChunk()->addString(node->content());

    // Create a value with the string index
    Value stringValue = Value::string(stringIndex);

    // Emit as constant
    emitConstant(stringValue);
}

void CodeGenerator::visitUnary(UnaryNode* node) {
    setLine(node->line());

    uint8_t oldRetCount = expectedRetCount_;
    bool oldTailCall = isTailCall_;

    // Unary operators evaluate their operand to exactly one value
    expectedRetCount_ = 2; // ONE
    isTailCall_ = false;

    // Compile operand first
    node->operand()->accept(*this);

    expectedRetCount_ = oldRetCount;
    isTailCall_ = oldTailCall;

    // Emit operator instruction
    switch (node->op()) {
        case TokenType::MINUS:
            emitOpCode(OpCode::OP_NEG);
            break;

        case TokenType::NOT:
            emitOpCode(OpCode::OP_NOT);
            break;

        case TokenType::TILDE:
            emitOpCode(OpCode::OP_BNOT);
            break;

        case TokenType::HASH:
            emitOpCode(OpCode::OP_LEN);
            break;

        default:
            throw CompileError("Unknown unary operator", node->line());
    }
}

void CodeGenerator::visitBinary(BinaryNode* node) {
    setLine(node->line());

    uint8_t oldRetCount = expectedRetCount_;
    bool oldTailCall = isTailCall_;

    // Binary operators always evaluate their operands to exactly one value
    expectedRetCount_ = 2; // ONE
    isTailCall_ = false;

    // Handle short-circuiting logical operators
    if (node->op() == TokenType::AND) {
        node->left()->accept(*this);
        size_t endJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
        emitOpCode(OpCode::OP_POP); // Pop left value
        node->right()->accept(*this);
        patchJump(endJump);
        expectedRetCount_ = oldRetCount;
        isTailCall_ = oldTailCall;
        return;
    }
    if (node->op() == TokenType::OR) {
        node->left()->accept(*this);
        // Jump to end if true (a or b -> if a is true, result is a)
        size_t elseJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
        size_t endJump = emitJump(OpCode::OP_JUMP);
        patchJump(elseJump);
        emitOpCode(OpCode::OP_POP); // Pop left value (was falsey)
        node->right()->accept(*this);
        patchJump(endJump);
        expectedRetCount_ = oldRetCount;
        isTailCall_ = oldTailCall;
        return;
    }

    // Compile left operand
    node->left()->accept(*this);

    // Compile right operand
    node->right()->accept(*this);

    expectedRetCount_ = oldRetCount;
    isTailCall_ = oldTailCall;

    // Emit operator instruction
    switch (node->op()) {
        case TokenType::PLUS:
            emitOpCode(OpCode::OP_ADD);
            break;

        case TokenType::MINUS:
            emitOpCode(OpCode::OP_SUB);
            break;

        case TokenType::STAR:
            emitOpCode(OpCode::OP_MUL);
            break;

        case TokenType::SLASH:
            emitOpCode(OpCode::OP_DIV);
            break;

        case TokenType::SLASH_SLASH:
            emitOpCode(OpCode::OP_IDIV);
            break;

        case TokenType::PERCENT:
            emitOpCode(OpCode::OP_MOD);
            break;

        case TokenType::CARET:
            emitOpCode(OpCode::OP_POW);
            break;

        case TokenType::AMPERSAND:
            emitOpCode(OpCode::OP_BAND);
            break;

        case TokenType::PIPE:
            emitOpCode(OpCode::OP_BOR);
            break;

        case TokenType::TILDE:
            emitOpCode(OpCode::OP_BXOR);
            break;

        case TokenType::LESS_LESS:
            emitOpCode(OpCode::OP_SHL);
            break;

        case TokenType::GREATER_GREATER:
            emitOpCode(OpCode::OP_SHR);
            break;

        case TokenType::DOT_DOT:
            emitOpCode(OpCode::OP_CONCAT);
            break;

        case TokenType::EQUAL_EQUAL:
            emitOpCode(OpCode::OP_EQUAL);
            break;

        case TokenType::BANG_EQUAL:
        case TokenType::TILDE_EQUAL:
            emitOpCode(OpCode::OP_EQUAL);
            emitOpCode(OpCode::OP_NOT);
            break;

        case TokenType::LESS:
            emitOpCode(OpCode::OP_LESS);
            break;

        case TokenType::LESS_EQUAL:
            emitOpCode(OpCode::OP_LESS_EQUAL);
            break;

        case TokenType::GREATER:
            emitOpCode(OpCode::OP_GREATER);
            break;

        case TokenType::GREATER_EQUAL:
            emitOpCode(OpCode::OP_GREATER_EQUAL);
            break;

        default:
            throw CompileError("Unknown binary operator", node->line());
    }
}

void CodeGenerator::visitVariable(VariableExprNode* node) {
    setLine(node->line());

    const std::string& name = node->name();

    // Three-level resolution: local → upvalue → global

    // 1. Try to resolve as local variable
    int slot = resolveLocal(name);
    if (slot != -1) {
#ifdef DEBUG
        std::cout << "DEBUG codegen GET_LOCAL: " << name << " slot=" << slot << std::endl;
#endif
        emitOpCode(OpCode::OP_GET_LOCAL);
        emitByte(static_cast<uint8_t>(slot));
        return;
    }

    // 2. Try to resolve as upvalue (captured from enclosing scope)
    if (!isDeclaredGlobal(name)) {
        int upvalue = resolveUpvalue(name);
        if (upvalue != -1) {
            emitOpCode(OpCode::OP_GET_UPVALUE);
            emitByte(static_cast<uint8_t>(upvalue));
            return;
        }
    }

    // 3. Fall back to global variable (resolved via _ENV)
    if (envDeclaredGlobal_) {
        throw CompileError("_ENV is global when accessing variable '" + name + "'", node->line());
    }
    if (globalMode_ == GlobalMode::NONE) {
        if (!isDeclaredGlobal(name)) {
            throw CompileError("variable '" + name + "' is not declared", node->line());
        }
    }

    int envSlot = resolveLocal("_ENV");
    if (envSlot != -1) {
        // _ENV is a local variable
        emitOpCode(OpCode::OP_GET_LOCAL);
        emitByte(static_cast<uint8_t>(envSlot));
        emitConstant(Value::string(internString(name)));
        emitOpCode(OpCode::OP_GET_TABLE);
        return;
    }

    int envUpvalue = resolveUpvalue("_ENV");
    if (envUpvalue == -1) {
        // Fallback to upvalue 0 if _ENV not explicitly found
        envUpvalue = 0;
    }

    size_t nameIndex = currentChunk()->addConstant(Value::string(internString(name)));
    emitGetTabUp(static_cast<uint8_t>(envUpvalue), nameIndex);
}

void CodeGenerator::visitVararg(VarargExprNode* node) {
    setLine(node->line());

    // Emit opcode to get varargs with expected count
    emitOpCode(OpCode::OP_GET_VARARG);
    emitByte(expectedRetCount_);
}

void CodeGenerator::visitExprStmt(ExprStmtNode* node) {
    setLine(node->line());

    uint8_t oldRetCount = expectedRetCount_;
    expectedRetCount_ = 1; // Zero results expected (0 + 1 = 1)
    node->expr()->accept(*this);
    expectedRetCount_ = oldRetCount;

    // If it was NOT a call, it pushed 1 value, so we still need to pop.
    bool isCall = dynamic_cast<CallExprNode*>(node->expr()) != nullptr ||
                  dynamic_cast<MethodCallExprNode*>(node->expr()) != nullptr;
    if (!isCall) {
        emitOpCode(OpCode::OP_POP);
    }
}

void CodeGenerator::visitAssignmentStmt(AssignmentStmtNode* node) {
    setLine(node->line());

    // Pre-resolve target variable as upvalue if it is not a local and not global
    if (resolveLocal(node->name()) == -1 && !isDeclaredGlobal(node->name())) {
        resolveUpvalue(node->name());
    }

    // Compile the value
    expectedName_ = node->name();
    node->value()->accept(*this);
    expectedName_ = "";

    // Three-level resolution: local → upvalue → global

    // 1. Try to resolve as local variable
    checkConstantAssign(node->name(), node->line());
    int slot = resolveLocal(node->name());
    if (slot != -1) {
        emitOpCode(OpCode::OP_SET_LOCAL);
        emitByte(static_cast<uint8_t>(slot));
        emitOpCode(OpCode::OP_POP);
        return;
    }

    // 2. Try to resolve as upvalue
    if (!isDeclaredGlobal(node->name())) {
        int upvalue = resolveUpvalue(node->name());
        if (upvalue != -1) {
            if (upvalues_[upvalue].isConstant) {
                throw CompileError("attempt to assign to const variable '" + node->name() + "'", node->line());
            }
            emitOpCode(OpCode::OP_SET_UPVALUE);
            emitByte(static_cast<uint8_t>(upvalue));
            emitOpCode(OpCode::OP_POP);
            return;
        }
    }

    // 3. Fall back to global variable (via _ENV)
    if (envDeclaredGlobal_) {
        throw CompileError("_ENV is global when accessing variable '" + node->name() + "'", node->line());
    }
    if (globalMode_ == GlobalMode::NONE) {
        if (!isDeclaredGlobal(node->name())) {
            throw CompileError("variable '" + node->name() + "' is not declared", node->line());
        }
    }
    int envSlot = resolveLocal("_ENV");
    if (envSlot != -1) {
        // _ENV is a local variable
        emitOpCode(OpCode::OP_GET_LOCAL);
        emitByte(static_cast<uint8_t>(envSlot));
        // Stack: [value, env]
        emitConstant(Value::string(internString(node->name())));
        // Stack: [value, env, key]
        emitOpCode(OpCode::OP_ROTATE);
        emitByte(3); 
        // Stack: [env, key, value]
        emitOpCode(OpCode::OP_SET_TABLE);
        return;
    }

    int envUpvalue = resolveUpvalue("_ENV");
    if (envUpvalue == -1) {
        // Fallback to upvalue 0 if _ENV not explicitly found
        envUpvalue = 0;
    }
    
    size_t nameIndex = currentChunk()->addConstant(Value::string(internString(node->name())));
    emitSetTabUp(static_cast<uint8_t>(envUpvalue), nameIndex);
}

void CodeGenerator::visitLocalDeclStmt(LocalDeclStmtNode* node) {
    setLine(node->line());

    if (node->isFunction()) {
        // For 'local function f', we need to add the local BEFORE compiling the body
        // so that the function can refer to itself (recursion).
        emitOpCode(OpCode::OP_NIL); // Placeholder for the function object
        addLocal(node->name());
        
        // Compile the function expression
        expectedName_ = node->name();
        node->initializer()->accept(*this);
        expectedName_ = "";
        
        // Update the local slot with the compiled function
        int slot = resolveLocal(node->name());
        emitOpCode(OpCode::OP_SET_LOCAL);
        emitByte(static_cast<uint8_t>(slot));
        emitOpCode(OpCode::OP_POP); 
    } else {
        // Compile initializer
        if (node->initializer()) {
            expectedName_ = node->name();
            node->initializer()->accept(*this);
            expectedName_ = "";
        } else {
            emitOpCode(OpCode::OP_NIL);
        }

        // Add local variable (value is already on stack)
        addLocal(node->name(), node->isConstant(), node->isClose());
    }
}

void CodeGenerator::visitMultipleLocalDeclStmt(MultipleLocalDeclStmtNode* node) {
    setLine(node->line());

    const auto& vars = node->vars();
    const auto& initializers = node->initializers();

    size_t varCount = vars.size();
    size_t initCount = initializers.size();

    uint8_t oldRetCount = expectedRetCount_;

    // 1. Evaluate all initializers except the last one
    for (size_t i = 0; i < (initCount > 0 ? initCount - 1 : 0); i++) {
        expectedRetCount_ = 2; // ONE
        if (i < varCount) expectedName_ = vars[i].name;
        initializers[i]->accept(*this);
        expectedName_ = "";
    }

    // 2. Evaluate the last initializer with multires if needed
    if (initCount > 0) {
        if (varCount > initCount) {
            // Last expression needs to provide multiple values
            expectedRetCount_ = static_cast<uint8_t>(varCount - (initCount - 1) + 1);
        } else {
            expectedRetCount_ = 2; // ONE
        }
        if (initCount - 1 < varCount) expectedName_ = vars[initCount - 1].name;
        initializers[initCount - 1]->accept(*this);
        expectedName_ = "";
    }

    expectedRetCount_ = oldRetCount;

    // 3. Pad with nil if fewer values than variables (but not covered by multires)
    if (initCount < varCount) {
        auto* lastInit = initCount > 0 ? initializers[initCount - 1].get() : nullptr;
        bool lastIsCall = lastInit && (dynamic_cast<CallExprNode*>(lastInit) != nullptr || 
                                   dynamic_cast<MethodCallExprNode*>(lastInit) != nullptr ||
                                   dynamic_cast<VarargExprNode*>(lastInit) != nullptr);

        if (!lastIsCall) {
            for (size_t i = initCount; i < varCount; i++) {
                emitOpCode(OpCode::OP_NIL);
            }
        }
    } else if (initCount > varCount) {        // 4. Discard excess values if more values than variables
        for (size_t i = varCount; i < initCount; i++) {
            emitOpCode(OpCode::OP_POP);
        }
    }

    // 5. Add local variables (values are already on stack in correct order)
    for (const auto& var : vars) {
        addLocal(var.name, var.isConstant, var.isClose);
    }
}

void CodeGenerator::visitMultipleAssignmentStmt(MultipleAssignmentStmtNode* node) {
    setLine(node->line());

    const auto& targets = node->targets();
    const auto& values = node->values();

    size_t varCount = targets.size();
    size_t valCount = values.size();

    // Pre-resolve target variables as upvalues in order of appearance (left to right)
    for (size_t i = 0; i < varCount; i++) {
        if (auto* varExpr = dynamic_cast<VariableExprNode*>(targets[i].get())) {
            const std::string& name = varExpr->name();
            if (resolveLocal(name) == -1 && !isDeclaredGlobal(name)) {
                resolveUpvalue(name);
            }
        }
    }

    struct IndexTargetInfo {
        int tableSlot = -1;
        int keySlot = -1;
    };
    std::vector<IndexTargetInfo> indexInfo(varCount);
    size_t tempLocalsCount = 0;

    // Pre-evaluate table and key expressions for all IndexExprNode targets (left to right)
    for (size_t i = 0; i < varCount; i++) {
        if (auto* indexExpr = dynamic_cast<IndexExprNode*>(targets[i].get())) {
            uint8_t oldRet = expectedRetCount_;
            expectedRetCount_ = 2; // ONE
            indexExpr->table()->accept(*this);
            addLocal("(temp table)");
            indexInfo[i].tableSlot = locals_.back().slot;
            tempLocalsCount++;

            expectedRetCount_ = 2; // ONE
            indexExpr->key()->accept(*this);
            addLocal("(temp key)");
            indexInfo[i].keySlot = locals_.back().slot;
            tempLocalsCount++;
            expectedRetCount_ = oldRet;
        }
    }

    uint8_t oldRetCount = expectedRetCount_;

    // 1. Evaluate all values except the last one
    for (size_t i = 0; i < (valCount > 0 ? valCount - 1 : 0); i++) {
        expectedRetCount_ = 2;
        if (i < varCount) {
            if (auto* var = dynamic_cast<VariableExprNode*>(targets[i].get())) {
                expectedName_ = var->name();
            }
        }
        values[i]->accept(*this);
        expectedName_ = "";
    }

    // 2. Evaluate the last value with multires if needed
    if (valCount > 0) {
        if (varCount > valCount) {
            expectedRetCount_ = static_cast<uint8_t>(varCount - (valCount - 1) + 1);
        } else {
            expectedRetCount_ = 2;
        }
        if (valCount - 1 < varCount) {
            if (auto* var = dynamic_cast<VariableExprNode*>(targets[valCount - 1].get())) {
                expectedName_ = var->name();
            }
        }
        values[valCount - 1]->accept(*this);
        expectedName_ = "";
    }

    expectedRetCount_ = oldRetCount;

    // 3. Pad with nil if fewer values than variables
    if (valCount < varCount) {
        auto* lastVal = valCount > 0 ? values[valCount - 1].get() : nullptr;
        bool lastIsCall = lastVal && (dynamic_cast<CallExprNode*>(lastVal) != nullptr || 
                                  dynamic_cast<MethodCallExprNode*>(lastVal) != nullptr ||
                                  dynamic_cast<VarargExprNode*>(lastVal) != nullptr);

        if (!lastIsCall) {
            for (size_t i = valCount; i < varCount; i++) {
                emitOpCode(OpCode::OP_NIL);
            }
        }
    } else if (valCount > varCount) {
        for (size_t i = varCount; i < valCount; i++) {
            emitOpCode(OpCode::OP_POP);
        }
    }

    // Now stack has exactly varCount values (bottom to top: v1, v2, ..., vN)

    // 4. Assign to variables in REVERSE order (pop from stack)
    for (int i = static_cast<int>(varCount) - 1; i >= 0; i--) {
        auto* target = targets[i].get();

        if (auto* varExpr = dynamic_cast<VariableExprNode*>(target)) {
            const std::string& name = varExpr->name();
            checkConstantAssign(name, node->line());

            // Three-level resolution: local -> upvalue -> global
            int slot = resolveLocal(name);
            if (slot != -1) {
                emitOpCode(OpCode::OP_SET_LOCAL);
                emitByte(static_cast<uint8_t>(slot));
                emitOpCode(OpCode::OP_POP);
                continue;
            }

            if (!isDeclaredGlobal(name)) {
                int upvalue = resolveUpvalue(name);
                if (upvalue != -1) {
                    if (upvalues_[upvalue].isConstant) {
                        throw CompileError("attempt to assign to const variable '" + name + "'", node->line());
                    }
                    emitOpCode(OpCode::OP_SET_UPVALUE);
                    emitByte(static_cast<uint8_t>(upvalue));
                    emitOpCode(OpCode::OP_POP);
                    continue;
                }
            }

            // Global variable (via _ENV)
            if (envDeclaredGlobal_) {
                throw CompileError("_ENV is global when accessing variable '" + name + "'", node->line());
            }
            if (globalMode_ == GlobalMode::NONE) {
                if (!isDeclaredGlobal(name)) {
                    throw CompileError("variable '" + name + "' is not declared", node->line());
                }
            }
            int envSlot = resolveLocal("_ENV");
            if (envSlot != -1) {
                emitOpCode(OpCode::OP_GET_LOCAL);
                emitByte(static_cast<uint8_t>(envSlot));
                emitConstant(Value::string(internString(name)));
                emitOpCode(OpCode::OP_ROTATE);
                emitByte(3);
                emitOpCode(OpCode::OP_SET_TABLE);
                continue;
            }

            int envUpvalue = resolveUpvalue("_ENV");
            if (envUpvalue == -1) envUpvalue = 0;

            size_t nameIndex = currentChunk()->addConstant(Value::string(internString(name)));
            emitSetTabUp(static_cast<uint8_t>(envUpvalue), nameIndex);
        } else if (dynamic_cast<IndexExprNode*>(target)) {
            int tSlot = indexInfo[i].tableSlot;
            int kSlot = indexInfo[i].keySlot;
            emitOpCode(OpCode::OP_GET_LOCAL);
            emitByte(static_cast<uint8_t>(tSlot));
            emitOpCode(OpCode::OP_GET_LOCAL);
            emitByte(static_cast<uint8_t>(kSlot));

            // We need [..., table, key, value] for OP_SET_TABLE
            // Rotate top 3: [value, table, key] -> [table, key, value]
            emitOpCode(OpCode::OP_ROTATE);
            emitByte(3);

            emitOpCode(OpCode::OP_SET_TABLE);
        }
    }

    // 5. Pop temporary table/key locals in reverse order of addition
    for (size_t k = 0; k < tempLocalsCount; k++) {
        emitOpCode(OpCode::OP_POP);
        locals_.pop_back();
        activeVars_.pop_back();
        localCount_--;
    }
}
void CodeGenerator::visitGlobalDeclStmt(GlobalDeclStmtNode* node) {
    setLine(node->line());
    const std::string& name = node->name();

    if (name == "*") {
        if (node->isConstant()) {
            globalMode_ = GlobalMode::CONST_ALL;
        } else {
            globalMode_ = GlobalMode::ALL;
        }
        activeVars_.push_back({"*", true, scopeDepth_});
        return;
    }

    if (name == "none") {
        globalMode_ = GlobalMode::NONE;
        activeVars_.push_back({"none", true, scopeDepth_});
        return;
    }

    if (node->isFunction()) {
        if (name == "_ENV") {
            envDeclaredGlobal_ = true;
        } else {
            declaredGlobals_.push_back({name, node->isConstant(), scopeDepth_, ++varSequence_});
            shadowedLocals_.insert(name);
        }
        activeVars_.push_back({name, true, scopeDepth_});

        if (node->initializer()) {
            expectedName_ = name;
            node->initializer()->accept(*this);
            expectedName_ = "";
        }
    } else {
        // Evaluate initializer FIRST before registering shadow
        if (node->initializer()) {
            expectedName_ = name;
            node->initializer()->accept(*this);
            expectedName_ = "";
        }

        if (name == "_ENV") {
            envDeclaredGlobal_ = true;
        } else {
            declaredGlobals_.push_back({name, node->isConstant(), scopeDepth_, ++varSequence_});
            shadowedLocals_.insert(name);
        }
        activeVars_.push_back({name, true, scopeDepth_});
    }

    if (node->initializer()) {
        int envSlot = resolveLocal("_ENV");
        if (envSlot != -1) {
            emitOpCode(OpCode::OP_GET_LOCAL);
            emitByte(static_cast<uint8_t>(envSlot));
            emitConstant(Value::string(internString(name)));
            emitOpCode(OpCode::OP_DEF_GLOBAL_TABLE);
        } else {
            int envUpvalue = resolveUpvalue("_ENV");
            if (envUpvalue == -1) envUpvalue = 0;
            size_t nameIndex = currentChunk()->addConstant(Value::string(internString(name)));
            if (nameIndex < 256) {
                emitOpCode(OpCode::OP_DEF_GLOBAL);
                emitByte(static_cast<uint8_t>(envUpvalue));
                emitByte(static_cast<uint8_t>(nameIndex));
            } else {
                emitOpCode(OpCode::OP_DEF_GLOBAL_LONG);
                emitByte(static_cast<uint8_t>(envUpvalue));
                emitByte(nameIndex & 0xff);
                emitByte((nameIndex >> 8) & 0xff);
                emitByte((nameIndex >> 16) & 0xff);
            }
        }
    }
}

void CodeGenerator::visitMultipleGlobalDeclStmt(MultipleGlobalDeclStmtNode* node) {
    setLine(node->line());
    const auto& vars = node->vars();
    const auto& inits = node->initializers();

    if (inits.empty()) {
        for (const auto& var : vars) {
            if (var.name == "*") {
                globalMode_ = var.isConstant ? GlobalMode::CONST_ALL : GlobalMode::ALL;
            } else if (var.name == "none") {
                globalMode_ = GlobalMode::NONE;
            } else if (var.name == "_ENV") {
                envDeclaredGlobal_ = true;
            } else {
                declaredGlobals_.push_back({var.name, var.isConstant, scopeDepth_, ++varSequence_});
                shadowedLocals_.insert(var.name);
            }
            activeVars_.push_back({var.name, true, scopeDepth_});
        }
        return;
    }

    size_t varCount = vars.size();
    size_t initCount = inits.size();
    uint8_t oldRetCount = expectedRetCount_;

    // 1. Evaluate all initializers except the last one
    for (size_t i = 0; i < (initCount > 0 ? initCount - 1 : 0); i++) {
        expectedRetCount_ = 2; // ONE
        if (i < varCount) expectedName_ = vars[i].name;
        inits[i]->accept(*this);
        expectedName_ = "";
    }

    // 2. Evaluate the last initializer with multires if needed
    if (initCount > 0) {
        if (varCount > initCount) {
            expectedRetCount_ = static_cast<uint8_t>(varCount - (initCount - 1) + 1);
        } else {
            expectedRetCount_ = 2; // ONE
        }
        if (initCount - 1 < varCount) expectedName_ = vars[initCount - 1].name;
        inits[initCount - 1]->accept(*this);
        expectedName_ = "";
    }

    expectedRetCount_ = oldRetCount;

    if (initCount < varCount) {
        auto* lastVal = initCount > 0 ? inits[initCount - 1].get() : nullptr;
        bool lastIsCall = lastVal && (dynamic_cast<CallExprNode*>(lastVal) != nullptr ||
                                      dynamic_cast<MethodCallExprNode*>(lastVal) != nullptr ||
                                      dynamic_cast<VarargExprNode*>(lastVal) != nullptr);
        if (!lastIsCall) {
            for (size_t i = initCount; i < varCount; i++) {
                emitOpCode(OpCode::OP_NIL);
            }
        }
    } else if (initCount > varCount) {
        for (size_t i = varCount; i < initCount; i++) {
            emitOpCode(OpCode::OP_POP);
        }
    }

    for (const auto& var : vars) {
        if (var.name == "_ENV") {
            envDeclaredGlobal_ = true;
        } else {
            declaredGlobals_.push_back({var.name, var.isConstant, scopeDepth_, ++varSequence_});
            shadowedLocals_.insert(var.name);
        }
        activeVars_.push_back({var.name, true, scopeDepth_});
    }

    for (int i = static_cast<int>(varCount) - 1; i >= 0; i--) {
        const std::string& name = vars[i].name;
        int envSlot = resolveLocal("_ENV");
        if (envSlot != -1) {
            emitOpCode(OpCode::OP_GET_LOCAL);
            emitByte(static_cast<uint8_t>(envSlot));
            emitConstant(Value::string(internString(name)));
            emitOpCode(OpCode::OP_DEF_GLOBAL_TABLE);
        } else {
            int envUpvalue = resolveUpvalue("_ENV");
            if (envUpvalue == -1) envUpvalue = 0;
            size_t nameIndex = currentChunk()->addConstant(Value::string(internString(name)));
            if (nameIndex < 256) {
                emitOpCode(OpCode::OP_DEF_GLOBAL);
                emitByte(static_cast<uint8_t>(envUpvalue));
                emitByte(static_cast<uint8_t>(nameIndex));
            } else {
                emitOpCode(OpCode::OP_DEF_GLOBAL_LONG);
                emitByte(static_cast<uint8_t>(envUpvalue));
                emitByte(nameIndex & 0xff);
                emitByte((nameIndex >> 8) & 0xff);
                emitByte((nameIndex >> 16) & 0xff);
            }
        }
    }
}

void CodeGenerator::checkGotoScoping(const Goto& g, const Label& lbl) {
    for (const auto& var : lbl.activeVars) {
        bool found = false;
        for (const auto& gvar : g.activeVars) {
            if (gvar.name == var.name && gvar.isGlobal == var.isGlobal && gvar.depth == var.depth) {
                found = true;
                break;
            }
        }
        if (!found) {
            throw CompileError("<goto " + g.name + "> at line " + std::to_string(g.line) +
                               " jumps into the scope of '" + var.name + "'", g.line);
        }
    }
}

void CodeGenerator::resolveBlockGotos(size_t firstGoto, size_t firstLabel) {
    for (size_t i = firstGoto; i < pendingGotos_.size(); ) {
        Goto& g = pendingGotos_[i];
        auto it = std::find_if(visibleLabels_.begin() + firstLabel, visibleLabels_.end(),
                               [&](const Label& lbl) { return lbl.name == g.name; });
        if (it != visibleLabels_.end()) {
            // Check scoping
            checkGotoScoping(g, *it);
            
            if (g.localCount == it->localCount) {
                // Direct jump
                size_t jumpDist = it->offset - g.instructionOffset - 2;
                currentChunk()->code()[g.instructionOffset] = jumpDist & 0xff;
                currentChunk()->code()[g.instructionOffset + 1] = (jumpDist >> 8) & 0xff;
            } else {
                // Needs cleanup stub
                gotosNeedingStubs_.push_back({g, *it});
            }
            pendingGotos_.erase(pendingGotos_.begin() + i);
        } else {
            i++;
        }
    }
}

void CodeGenerator::compileBlock(const std::vector<std::unique_ptr<StmtNode>>& stmts, bool hasEndScope) {
    size_t firstLabel = visibleLabels_.size();
    size_t firstGoto = pendingGotos_.size();
    size_t firstActiveVar = activeVars_.size();
    int entryLocalCount = localCount_;
    std::vector<ActiveVar> entryActiveVars = activeVars_;

    blockScopes_.push_back({firstLabel, firstGoto, firstActiveVar, entryLocalCount, entryActiveVars});

    size_t trailingLabelStart = stmts.size();
    while (trailingLabelStart > 0 && dynamic_cast<LabelStmtNode*>(stmts[trailingLabelStart - 1].get()) != nullptr) {
        trailingLabelStart--;
    }

    for (size_t i = 0; i < trailingLabelStart; i++) {
        if (stmts[i]) stmts[i]->accept(*this);
    }

    if (hasEndScope) {
        endScope();
    }

    for (size_t i = trailingLabelStart; i < stmts.size(); i++) {
        if (stmts[i]) {
            LabelStmtNode* labelNode = dynamic_cast<LabelStmtNode*>(stmts[i].get());
            if (labelNode) {
                setLine(labelNode->line());
                const std::string& name = labelNode->label();
                for (const auto& l : visibleLabels_) {
                    if (l.name == name) {
                        throw CompileError("label '" + name + "' already defined", currentLine_);
                    }
                }
                Label lbl;
                lbl.name = name;
                lbl.offset = currentChunk()->size();
                lbl.localCount = entryLocalCount;
                lbl.activeVars = entryActiveVars;
                lbl.activeVarCount = static_cast<int>(entryActiveVars.size());
                lbl.blockDepth = scopeDepth_;
                visibleLabels_.push_back(lbl);
            } else {
                stmts[i]->accept(*this);
            }
        }
    }

    resolveBlockGotos(firstGoto, firstLabel);

    visibleLabels_.resize(firstLabel);

    if (!hasEndScope) {
        activeVars_.resize(firstActiveVar);
    }

    blockScopes_.pop_back();
}

void CodeGenerator::visitGoto(GotoStmtNode* node) {
    setLine(node->line());
    const std::string& name = node->label();
    
    // Check if label was already defined (backward jump)
    auto it = std::find_if(visibleLabels_.rbegin(), visibleLabels_.rend(),
                           [&](const Label& lbl) { return lbl.name == name; });
    if (it != visibleLabels_.rend()) {
        // Backward jump
        checkGotoScoping({name, 0, localCount_, static_cast<int>(activeVars_.size()), activeVars_, scopeDepth_, currentLine_}, *it);
        
        for (int i = localCount_ - 1; i >= it->localCount; i--) {
            emitOpCode(OpCode::OP_CLOSE_UPVALUE);
        }
        
        emitLoop(it->offset);
    } else {
        // Forward jump - unresolved
        size_t jump = emitJump(OpCode::OP_JUMP);
        pendingGotos_.push_back({name, jump, localCount_, static_cast<int>(activeVars_.size()), activeVars_, scopeDepth_, currentLine_});
    }
}

void CodeGenerator::visitLabel(LabelStmtNode* node) {
    setLine(node->line());
    const std::string& name = node->label();
    
    for (const auto& l : visibleLabels_) {
        if (l.name == name) {
            throw CompileError("label '" + name + "' already defined", currentLine_);
        }
    }
    
    Label lbl;
    lbl.name = name;
    lbl.offset = currentChunk()->size();
    lbl.localCount = localCount_;
    lbl.activeVars = activeVars_;
    lbl.activeVarCount = static_cast<int>(activeVars_.size());
    lbl.blockDepth = scopeDepth_;
    visibleLabels_.push_back(lbl);

    size_t firstGoto = blockScopes_.empty() ? 0 : blockScopes_.back().firstGoto;
    size_t labelIdx = visibleLabels_.size() - 1;
    resolveBlockGotos(firstGoto, labelIdx);
}

void CodeGenerator::emitGotoStubs() {
    if (!pendingGotos_.empty()) {
        throw CompileError("no visible label '" + pendingGotos_[0].name + "' for <goto> at line " + std::to_string(pendingGotos_[0].line), pendingGotos_[0].line);
    }

    for (const auto& item : gotosNeedingStubs_) {
        const Goto& g = item.first;
        const Label& lbl = item.second;
        
        size_t stubOffset = currentChunk()->size();
        
        // Patch original JUMP to jump to this stub
        size_t jumpToStub = stubOffset - g.instructionOffset - 2;
        currentChunk()->code()[g.instructionOffset] = jumpToStub & 0xff;
        currentChunk()->code()[g.instructionOffset + 1] = (jumpToStub >> 8) & 0xff;
        
        // Generate cleanup stub
        for (int i = g.localCount - 1; i >= lbl.localCount; i--) {
            emitOpCode(OpCode::OP_CLOSE_UPVALUE);
        }
        
        // Jump from stub to label
        emitLoop(lbl.offset);
    }
    gotosNeedingStubs_.clear();
}

void CodeGenerator::visitBlock(BlockStmtNode* node) {
    setLine(node->line());
    beginScope();
    compileBlock(node->statements(), true);
}

void CodeGenerator::visitProgram(ProgramNode* node) {
    setLine(node->line());
    compileBlock(node->statements(), false);
}

void CodeGenerator::emitByte(uint8_t byte) {
    currentChunk()->write(byte, currentLine_);
}

void CodeGenerator::emitBytes(uint8_t byte1, uint8_t byte2) {
    emitByte(byte1);
    emitByte(byte2);
}

void CodeGenerator::emitBytes(uint8_t byte1, uint8_t byte2, uint8_t byte3, uint8_t byte4) {
    emitByte(byte1);
    emitByte(byte2);
    emitByte(byte3);
    emitByte(byte4);
}

void CodeGenerator::emitOpCode(OpCode op) {
    emitByte(static_cast<uint8_t>(op));
}

void CodeGenerator::emitConstant(const Value& value) {
    size_t index = currentChunk()->addConstant(value);

    if (index <= UINT8_MAX) {
        emitBytes(static_cast<uint8_t>(OpCode::OP_CONSTANT), static_cast<uint8_t>(index));
    } else if (index <= 0xFFFFFF) {
        emitBytes(static_cast<uint8_t>(OpCode::OP_CONSTANT_LONG),
                  static_cast<uint8_t>(index & 0xFF),
                  static_cast<uint8_t>((index >> 8) & 0xFF),
                  static_cast<uint8_t>((index >> 16) & 0xFF));
    } else {
        throw CompileError("Too many constants in one chunk", currentLine_);
    }
}

void CodeGenerator::emitGetTabUp(uint8_t upvalue, size_t nameIndex) {
    if (nameIndex <= UINT8_MAX) {
        emitBytes(static_cast<uint8_t>(OpCode::OP_GET_TABUP), upvalue);
        emitByte(static_cast<uint8_t>(nameIndex));
    } else if (nameIndex <= 0xFFFFFF) {
        emitOpCode(OpCode::OP_GET_TABUP_LONG);
        emitByte(upvalue);
        emitByte(static_cast<uint8_t>(nameIndex & 0xFF));
        emitByte(static_cast<uint8_t>((nameIndex >> 8) & 0xFF));
        emitByte(static_cast<uint8_t>((nameIndex >> 16) & 0xFF));
    } else {
        throw CompileError("Too many constants in one chunk", currentLine_);
    }
}

void CodeGenerator::emitSetTabUp(uint8_t upvalue, size_t nameIndex) {
    if (nameIndex <= UINT8_MAX) {
        emitBytes(static_cast<uint8_t>(OpCode::OP_SET_TABUP), upvalue);
        emitByte(static_cast<uint8_t>(nameIndex));
    } else if (nameIndex <= 0xFFFFFF) {
        emitOpCode(OpCode::OP_SET_TABUP_LONG);
        emitByte(upvalue);
        emitByte(static_cast<uint8_t>(nameIndex & 0xFF));
        emitByte(static_cast<uint8_t>((nameIndex >> 8) & 0xFF));
        emitByte(static_cast<uint8_t>((nameIndex >> 16) & 0xFF));
    } else {
        throw CompileError("Too many constants in one chunk", currentLine_);
    }
}

void CodeGenerator::emitReturn() {
    emitOpCode(OpCode::OP_RETURN);
}

size_t CodeGenerator::emitJump(OpCode op) {
    emitOpCode(op);
    emitByte(0xff);  // Placeholder for jump offset
    emitByte(0xff);
    return currentChunk()->size() - 2;
}

void CodeGenerator::patchJump(size_t offset) {
    // Calculate jump distance (-2 for the jump offset itself)
    size_t jump = currentChunk()->size() - offset - 2;

    if (jump > UINT16_MAX) {
        throw CompileError("Too much code to jump over", currentLine_);
    }

    // Patch the jump offset
    currentChunk()->code()[offset] = (jump) & 0xff;
    currentChunk()->code()[offset + 1] = (jump >> 8) & 0xff;
}

void CodeGenerator::emitLoop(size_t loopStart) {
    emitOpCode(OpCode::OP_LOOP);

    size_t offset = currentChunk()->size() - loopStart + 2;
    if (offset > UINT16_MAX) {
        throw CompileError("Loop body too large", currentLine_);
    }

    emitByte(offset & 0xff);
    emitByte((offset >> 8) & 0xff);
}

void CodeGenerator::visitIfStmt(IfStmtNode* node) {
    setLine(node->line());

    // Compile condition
    node->condition()->accept(*this);

    // Jump to else/end if condition is false
    size_t thenJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
    emitOpCode(OpCode::OP_POP);  // Pop condition

    // Compile then branch
    beginScope();
    compileBlock(node->thenBranch(), true);

    // Jump over else branch
    size_t elseJump = emitJump(OpCode::OP_JUMP);

    // Patch then jump to here (else/end)
    patchJump(thenJump);
    emitOpCode(OpCode::OP_POP);  // Pop condition

    // Compile elseif branches
    std::vector<size_t> endJumps;
    for (const auto& elseIfBranch : node->elseIfBranches()) {
        // Compile elseif condition
        elseIfBranch.condition->accept(*this);

        size_t elseIfJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
        emitOpCode(OpCode::OP_POP);  // Pop condition

        // Compile elseif body
        beginScope();
        compileBlock(elseIfBranch.body, true);

        // Jump to end
        endJumps.push_back(emitJump(OpCode::OP_JUMP));

        // Patch elseif jump
        patchJump(elseIfJump);
        emitOpCode(OpCode::OP_POP);  // Pop condition
    }

    // Compile else branch
    beginScope();
    compileBlock(node->elseBranch(), true);

    // Patch else jump
    patchJump(elseJump);

    // Patch all end jumps
    for (size_t jump : endJumps) {
        patchJump(jump);
    }
}

void CodeGenerator::visitWhileStmt(WhileStmtNode* node) {
    setLine(node->line());

    beginLoop();  // Start loop context for break statements

    size_t loopStart = currentChunk()->size();

    // Compile condition
    node->condition()->accept(*this);

    // Jump out of loop if condition is false
    size_t exitJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
    emitOpCode(OpCode::OP_POP);  // Pop condition

    // Compile body in its own scope
    beginScope();
    compileBlock(node->body(), true);

    // Loop back to condition
    emitLoop(loopStart);

    // Patch exit jump
    patchJump(exitJump);
    emitOpCode(OpCode::OP_POP);  // Pop condition

    endLoop();  // End loop context and patch all break jumps
}

void CodeGenerator::visitRepeatStmt(RepeatStmtNode* node) {
    setLine(node->line());

    beginLoop();  // Start loop context for break statements

    size_t loopStart = currentChunk()->size();

    // Compile body in its own scope
    beginScope();
    for (const auto& stmt : node->body()) {
        if (stmt) stmt->accept(*this);
    }

    // Condition is evaluated WITHIN the body scope!
    node->condition()->accept(*this);

    // Identify locals in this scope before endScope removes them
    std::vector<Local> scopeLocals;
    for (int i = static_cast<int>(locals_.size()) - 1; i >= 0; i--) {
        if (locals_[i].depth >= scopeDepth_) {
            scopeLocals.push_back(locals_[i]);
        } else {
            break;
        }
    }

    // If condition is falsey (or nil), we loop back.
    // JUMP_IF_FALSE peeks at top of stack (the condition).
    size_t loopBackJump = emitJump(OpCode::OP_JUMP_IF_FALSE);

    // Condition was truthy: normal exit path
    emitOpCode(OpCode::OP_POP);  // Pop condition

    // End scope cleans up locals for the exit path
    endScope();

    // Jump past the loop-back instructions
    size_t exitJump = emitJump(OpCode::OP_JUMP);

    // Loop-back path:
    patchJump(loopBackJump);
    emitOpCode(OpCode::OP_POP);  // Pop condition

    // Pop/close locals for the next iteration
    for (const auto& l : scopeLocals) {
        if (l.isCaptured || l.isClose) {
            emitOpCode(OpCode::OP_CLOSE_UPVALUE);
        } else {
            emitOpCode(OpCode::OP_POP);
        }
    }

    // Loop back to start of loop body
    emitLoop(loopStart);

    // Exit point for normal completion and break jumps
    patchJump(exitJump);

    endLoop();  // End loop context and patch all break jumps
}

void CodeGenerator::visitForStmt(ForStmtNode* node) {
    setLine(node->line());

    // Begin scope for loop variables:
    // slot base:     (for state) - current loop value
    // slot base + 1: (for limit) - limit value
    // slot base + 2: (for step)  - step value
    // slot base + 3: user variable (node->varName()) - initialized to nil, const
    beginScope();

    uint8_t oldRetCount = expectedRetCount_;

    // 1. Evaluate start expression
    expectedRetCount_ = 2; // 1 result
    node->start()->accept(*this);
    int base = localCount_;
    addLocal("(for state)", true);

    // 2. Evaluate limit expression
    expectedRetCount_ = 2;
    node->end()->accept(*this);
    addLocal("(for limit)", true);

    // 3. Evaluate step expression (or default to 1)
    if (node->step()) {
        expectedRetCount_ = 2;
        node->step()->accept(*this);
    } else {
        emitConstant(Value::integer(1));
    }
    addLocal("(for step)", true);

    expectedRetCount_ = oldRetCount;

    // 4. User loop variable
    emitOpCode(OpCode::OP_NIL);
    addLocal(node->varName(), true);

    // Emit OP_FORPREP [base: uint8_t] [offset: uint16_t]
    emitOpCode(OpCode::OP_FORPREP);
    emitByte(static_cast<uint8_t>(base));
    emitByte(0xff);
    emitByte(0xff);
    size_t prepJump = currentChunk()->size() - 2;

    beginLoop();  // Start loop context for break statements

    size_t loopStart = currentChunk()->size();

    // Compile body
    beginScope();
    compileBlock(node->body(), true);

    // Emit OP_FORLOOP [base: uint8_t] [offset: uint16_t]
    emitOpCode(OpCode::OP_FORLOOP);
    emitByte(static_cast<uint8_t>(base));
    size_t loopEnd = currentChunk()->size() + 2;
    size_t backwardJump = loopEnd - loopStart;
    if (backwardJump > 0xFFFF) {
        throw CompileError("Loop body too large", node->line());
    }
    emitByte(static_cast<uint8_t>(backwardJump & 0xff));
    emitByte(static_cast<uint8_t>((backwardJump >> 8) & 0xff));

    // Exit point: patch prep jump to land right after OP_FORLOOP
    patchJump(prepJump);

    endLoop();  // End loop context and patch all break jumps

    // End scope (cleans up loop variable and hidden locals)
    endScope();
}


void CodeGenerator::visitForInStmt(ForInStmtNode* node) {
    setLine(node->line());

    // Begin scope for iterator state and loop variables
    beginScope();

    // Evaluate iterator expressions
    // Generic for loop expects 4 values: iterator, state, control_var, to-be-closed
    const auto& iterators = node->iterators();
    size_t initCount = iterators.size();
    const size_t varCount = 4;

    uint8_t oldRetCount = expectedRetCount_;

    // 1. Evaluate all initializers except the last one
    for (size_t i = 0; i < (initCount > 0 ? initCount - 1 : 0); i++) {
        expectedRetCount_ = 2; // ONE
        iterators[i]->accept(*this);
    }

    // 2. Evaluate the last initializer with multires if needed
    if (initCount > 0) {
        if (varCount > initCount) {
            // Last expression needs to provide multiple values (nresults + 1)
            expectedRetCount_ = static_cast<uint8_t>(varCount - (initCount - 1) + 1);
        } else {
            expectedRetCount_ = 2; // ONE
        }
        iterators[initCount - 1]->accept(*this);
    }

    expectedRetCount_ = oldRetCount;

    // 3. Pad with nil if fewer values than variables (and not covered by multires call/vararg)
    if (initCount < varCount) {
        auto* lastInit = initCount > 0 ? iterators[initCount - 1].get() : nullptr;
        bool lastIsCall = lastInit && (dynamic_cast<CallExprNode*>(lastInit) != nullptr || 
                                       dynamic_cast<MethodCallExprNode*>(lastInit) != nullptr ||
                                       dynamic_cast<VarargExprNode*>(lastInit) != nullptr);

        if (!lastIsCall) {
            for (size_t i = initCount; i < varCount; i++) {
                emitOpCode(OpCode::OP_NIL);
            }
        }
    } else if (initCount > varCount) {
        // Discard excess values
        for (size_t i = varCount; i < initCount; i++) {
            emitOpCode(OpCode::OP_POP);
        }
    }

    // Store iterator state in hidden locals
    // They are already on stack in order [iter, state, control, to-be-closed]
    int iteratorSlot = localCount_;
    addLocal("(for iterator)", true);
    int stateSlot = localCount_;
    addLocal("(for state)", true);
    int controlSlot = localCount_;
    addLocal("(for state)", true);
    addLocal("(for state)", true, true /* isClose */);

    // Add loop variables (initialized to nil)
    const auto& varNames = node->varNames();
    for (const auto& name : varNames) {
        emitOpCode(OpCode::OP_NIL);
        addLocal(name, true);
    }
    
    beginLoop();  // Start loop context for break statements

    size_t loopStart = currentChunk()->size();

    // Call iterator(state, control)
    // Get iterator
    emitOpCode(OpCode::OP_GET_LOCAL);
    emitByte(static_cast<uint8_t>(iteratorSlot));

    // Get state
    emitOpCode(OpCode::OP_GET_LOCAL);
    emitByte(static_cast<uint8_t>(stateSlot));
    
    // Get control variable
    emitOpCode(OpCode::OP_GET_LOCAL);
    emitByte(static_cast<uint8_t>(controlSlot));

        // Call iterator(state, control)
        emitOpCode(OpCode::OP_CALL);
        emitByte(2);  // 2 arguments
        
        // We need varNames.size() return values
        // But we need at least 1 (to check for nil)
        uint8_t retCount = varNames.empty() ? 1 : static_cast<uint8_t>(varNames.size());
        emitByte(retCount + 1); // nresults + 1
     

    // Values are now on stack.
    // Top is varN, bottom is var1.
    // We need to update control variable with var1.
    // And update loop locals.
    
    // Strategy: Store to locals in reverse order
    for (int i = static_cast<int>(varNames.size()) - 1; i >= 0; i--) {
        int slot = resolveLocal(varNames[i]);
        emitOpCode(OpCode::OP_SET_LOCAL);
        emitByte(static_cast<uint8_t>(slot));
        
        // If this is the first variable (i=0), update control variable too
        if (i == 0) {
            // Stack top: v1. (SET_LOCAL doesn't pop in VM but CodeGen usually emits POP?)
            // Wait, standard OP_SET_LOCAL usage in assignment is SET_LOCAL + POP.
            // Here I emitted SET_LOCAL. Value is still on stack.
            
            // Also update control variable
            emitOpCode(OpCode::OP_SET_LOCAL);
            emitByte(static_cast<uint8_t>(controlSlot));
            
            // Value still on stack.
            // Pop it.
            emitOpCode(OpCode::OP_POP);
        } else {
            // Not first variable. Just pop.
            emitOpCode(OpCode::OP_POP);
        }
    }
    
    // Check if control variable is nil
    emitOpCode(OpCode::OP_GET_LOCAL);
    emitByte(static_cast<uint8_t>(controlSlot));
    emitOpCode(OpCode::OP_NIL);
    emitOpCode(OpCode::OP_EQUAL);
    // Stack: [is_nil]
    
    // We want to jump if is_nil is true.
    // JUMP_IF_FALSE jumps if false.
    // So we NOT it.
    emitOpCode(OpCode::OP_NOT);
    // Stack: [not_nil]
    
    // If not_nil is true (value exists), continue.
    // If not_nil is false (value is nil), break.
    
    size_t breakJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
    emitOpCode(OpCode::OP_POP); // Pop boolean result

    // Compile body
    beginScope();
    compileBlock(node->body(), true);

    // Loop back
    emitLoop(loopStart);

    // Exit point (when iterator returns nil)
    patchJump(breakJump);
    emitOpCode(OpCode::OP_POP); // Pop boolean result

    endLoop();  // End loop context and patch all break jumps

    // End scope (cleans up iterator and loop variable)
    endScope();
}

void CodeGenerator::visitCall(CallExprNode* node) {
    setLine(node->line());

    // Check for coroutine.yield (if callee is coroutine.yield)
    auto* indexExpr = dynamic_cast<IndexExprNode*>(node->callee());
    if (indexExpr) {
        auto* tableVar = dynamic_cast<VariableExprNode*>(indexExpr->table());
        auto* stringKey = dynamic_cast<StringLiteralNode*>(indexExpr->key());
        if (tableVar && tableVar->name() == "coroutine" && 
            stringKey && stringKey->content() == "yield") {
            
            uint8_t yieldRetCount = expectedRetCount_;
            const auto& args = node->args();
            bool isLastMultires = false;
            for (size_t i = 0; i < args.size(); i++) {
                bool canBeMultires = (dynamic_cast<CallExprNode*>(args[i].get()) != nullptr) ||
                                     (dynamic_cast<MethodCallExprNode*>(args[i].get()) != nullptr) ||
                                     (dynamic_cast<VarargExprNode*>(args[i].get()) != nullptr);

                if (i == args.size() - 1 && canBeMultires) {
                    expectedRetCount_ = 0; // Last argument can be multires (0 = ALL)
                    isLastMultires = true;
                } else {
                    expectedRetCount_ = 2; // Fixed args expect 1 result
                }
                args[i]->accept(*this);
            }

            if (isLastMultires) {
                emitOpCode(OpCode::OP_YIELD_MULTI);
                emitByte(static_cast<uint8_t>(args.size() - 1));
            } else {
                emitOpCode(OpCode::OP_YIELD);
                if (args.size() > UINT8_MAX) {
                    throw CompileError("Too many arguments to yield", currentLine_);
                }
                emitByte(static_cast<uint8_t>(args.size()));
            }
            emitByte(yieldRetCount);
            return;
        }
    }

    // Compile the callee expression (evaluates to a function on the stack)
    // Save current expected return count and tail call flag
    uint8_t oldRetCount = expectedRetCount_;
    bool oldTailCall = isTailCall_;

    isTailCall_ = false; // The expression providing the function is NOT a tail call
    expectedRetCount_ = 2; // The callee itself expects 1 result (1 + 1 = 2)
    node->callee()->accept(*this);

    // Compile arguments and push onto stack
    const auto& args = node->args();
    bool isLastMultires = false;

    for (size_t i = 0; i < args.size(); i++) {
        bool canBeMultires = (dynamic_cast<CallExprNode*>(args[i].get()) != nullptr) ||
                             (dynamic_cast<MethodCallExprNode*>(args[i].get()) != nullptr) ||
                             (dynamic_cast<VarargExprNode*>(args[i].get()) != nullptr);

        if (i == args.size() - 1 && canBeMultires) {
            expectedRetCount_ = 0; // Last argument can be multires (0 = ALL)
            isLastMultires = true;
        } else {
            expectedRetCount_ = 2; // ONE (1 + 1 = 2)
        }
        args[i]->accept(*this);
    }

    // Restore flags
    expectedRetCount_ = oldRetCount;
    isTailCall_ = oldTailCall;

    // Emit call instruction
    if (isTailCall_) {        if (isLastMultires) {
            emitOpCode(OpCode::OP_TAILCALL_MULTI);
            emitByte(static_cast<uint8_t>(args.size() - 1)); // Number of FIXED args
        } else {
            emitOpCode(OpCode::OP_TAILCALL);
            emitByte(static_cast<uint8_t>(args.size()));
        }
    } else {
        if (isLastMultires) {
            emitOpCode(OpCode::OP_CALL_MULTI);
            emitByte(static_cast<uint8_t>(args.size() - 1)); // Number of FIXED args
            emitByte(expectedRetCount_);
        } else {
            emitOpCode(OpCode::OP_CALL);
            emitByte(static_cast<uint8_t>(args.size()));
            emitByte(expectedRetCount_);
        }
    }
}

void CodeGenerator::visitMethodCall(MethodCallExprNode* node) {
    setLine(node->line());

    // Compile the object expression (receiver)
    uint8_t oldRetCount = expectedRetCount_;
    bool oldTailCall = isTailCall_;
    
    isTailCall_ = false; // Receiver expression is NOT a tail call
    expectedRetCount_ = 2; // Expect ONE result
    node->object()->accept(*this);
    
    // Duplicate object for OP_GET_TABLE AND for the 'self' argument
    // Stack: [obj] -> [obj, obj]
    emitOpCode(OpCode::OP_DUP);
    
    // Get the method from the object
    // Stack: [obj, obj] -> [obj, obj, "method"] -> [obj, method]
    emitConstant(Value::string(currentChunk()->addString(node->method())));
    emitOpCode(OpCode::OP_GET_TABLE);
    
    // Now stack is: [obj, method]. We need [method, obj] for call.
    // ...
    emitOpCode(OpCode::OP_SWAP);
    
    // Now stack: [method, obj]
    
    // Compile arguments and push onto stack
    const auto& args = node->args();
    bool isLastMultires = false;
    
    for (size_t i = 0; i < args.size(); i++) {
        bool canBeMultires = (dynamic_cast<CallExprNode*>(args[i].get()) != nullptr) ||
                             (dynamic_cast<MethodCallExprNode*>(args[i].get()) != nullptr) ||
                             (dynamic_cast<VarargExprNode*>(args[i].get()) != nullptr);
        
        if (i == args.size() - 1 && canBeMultires) {
            expectedRetCount_ = 0; // Last argument can be multires (0 = ALL)
            isLastMultires = true;
        } else {
            expectedRetCount_ = 2; // ONE (1 + 1 = 2)
        }
        args[i]->accept(*this);
    }
    
    // Restore flags
    expectedRetCount_ = oldRetCount;
    isTailCall_ = oldTailCall;

    // Emit call instruction. Argument count is args.size() + 1 (for self)
    if (isTailCall_) {
        if (isLastMultires) {
            emitOpCode(OpCode::OP_TAILCALL_MULTI);
            emitByte(static_cast<uint8_t>(args.size())); // FIXED args
        } else {
            emitOpCode(OpCode::OP_TAILCALL);
            emitByte(static_cast<uint8_t>(args.size() + 1));
        }
    } else {
        if (isLastMultires) {
            emitOpCode(OpCode::OP_CALL_MULTI);
            emitByte(static_cast<uint8_t>(args.size())); // args.size() FIXED args (including self, minus the multires one)
            // Wait: FIXED args = (args.size() - 1) + 1 = args.size()
            emitByte(expectedRetCount_);
        } else {
            emitOpCode(OpCode::OP_CALL);
            emitByte(static_cast<uint8_t>(args.size() + 1));
            emitByte(expectedRetCount_);
        }
    }
}

void CodeGenerator::visitTableConstructor(TableConstructorNode* node) {
    setLine(node->line());

    // Emit OP_NEW_TABLE to create a new empty table
    emitOpCode(OpCode::OP_NEW_TABLE);

    // Table is now on top of stack
    // For each entry, we: duplicate table, compile key, compile value, then OP_SET_TABLE

    int arrayIndex = 1;  // Lua arrays start at 1

    const auto& entries = node->entries();
    for (size_t i = 0; i < entries.size(); i++) {
        const auto& entry = entries[i];
        // Duplicate the table reference for OP_SET_TABLE
        // Stack: [table] -> [table, table]
        emitOpCode(OpCode::OP_DUP);

        // Compile key
        if (entry.key == nullptr) {
            bool canBeMultires = (dynamic_cast<CallExprNode*>(entry.value.get()) != nullptr) ||
                                 (dynamic_cast<MethodCallExprNode*>(entry.value.get()) != nullptr) ||
                                 (dynamic_cast<VarargExprNode*>(entry.value.get()) != nullptr);
            bool isLast = (i == entries.size() - 1);

            // Array-style entry: use implicit numeric index
            emitConstant(Value::integer(arrayIndex));
            
            uint8_t oldRetCount = expectedRetCount_;
            if (isLast && canBeMultires) {
                expectedRetCount_ = 0; // Multires (0 = ALL)
                entry.value->accept(*this);
                expectedRetCount_ = oldRetCount;
                emitOpCode(OpCode::OP_SET_TABLE_MULTI);
            } else {
                expectedRetCount_ = 2; // ONE (1 + 1 = 2)
                entry.value->accept(*this);
                expectedRetCount_ = oldRetCount;
                emitOpCode(OpCode::OP_SET_TABLE);
                arrayIndex++;
            }
        } else {
            // Record-style or computed key
            entry.key->accept(*this);
            
            uint8_t oldRetCount = expectedRetCount_;
            expectedRetCount_ = 2; // ONE (1 + 1 = 2)
            entry.value->accept(*this);
            expectedRetCount_ = oldRetCount;
            
            emitOpCode(OpCode::OP_SET_TABLE);
        }
    }

    // Table is left on stack as the result of the constructor expression
}

void CodeGenerator::visitIndexExpr(IndexExprNode* node) {
    setLine(node->line());

    if (isVarargOptimized_ && !currentVarargName_.empty()) {
        if (auto* varNode = dynamic_cast<VariableExprNode*>(node->table())) {
            if (varNode->name() == currentVarargName_ && resolveLocal(currentVarargName_) == namedVarargSlot_) {
                if (auto* strLit = dynamic_cast<StringLiteralNode*>(node->key())) {
                    if (strLit->content() == "n") {
                        emitOpCode(OpCode::OP_GET_VARARG_COUNT);
                        return;
                    }
                }
                node->key()->accept(*this);
                emitOpCode(OpCode::OP_GET_VARARG_ITEM);
                return;
            }
        }
    }

    // Compile table expression
    node->table()->accept(*this);

    // Compile key expression
    node->key()->accept(*this);

    // Emit OP_GET_TABLE: pops key and table, pushes value
    emitOpCode(OpCode::OP_GET_TABLE);
}

void CodeGenerator::visitIndexAssignmentStmt(IndexAssignmentStmtNode* node) {
    setLine(node->line());

    // Compile table expression
    node->table()->accept(*this);

    // Compile key expression
    node->key()->accept(*this);

    // Compile value expression
    node->value()->accept(*this);

    // Emit OP_SET_TABLE: pops value, key, and table
    emitOpCode(OpCode::OP_SET_TABLE);
}

void CodeGenerator::visitFunctionDecl(FunctionDeclNode* node) {
    setLine(node->line());

    if (resolveLocal(node->name()) == -1 && !isDeclaredGlobal(node->name())) {
        resolveUpvalue(node->name());
    }

    // Compile function and emit OP_CLOSURE
    compileFunction(node->name(), node->params(), node->body(), node->hasVarargs(), node->varargName(), node->line(), node->line());

    // Three-level resolution: local → upvalue → global
    
    // 1. Try to resolve as local variable
    checkConstantAssign(node->name(), node->line());
    int slot = resolveLocal(node->name());
    if (slot != -1) {
        emitOpCode(OpCode::OP_SET_LOCAL);
        emitByte(static_cast<uint8_t>(slot));
        emitOpCode(OpCode::OP_POP);
        return;
    }

    // 2. Try to resolve as upvalue
    if (!isDeclaredGlobal(node->name())) {
        int upvalue = resolveUpvalue(node->name());
        if (upvalue != -1) {
            if (upvalues_[upvalue].isConstant) {
                throw CompileError("attempt to assign to const variable '" + node->name() + "'", node->line());
            }
            emitOpCode(OpCode::OP_SET_UPVALUE);
            emitByte(static_cast<uint8_t>(upvalue));
            emitOpCode(OpCode::OP_POP);
            return;
        }
    }

    // 3. Fall back to global variable (via _ENV)
    if (envDeclaredGlobal_) {
        throw CompileError("_ENV is global when accessing variable '" + node->name() + "'", node->line());
    }
    if (globalMode_ == GlobalMode::NONE) {
        if (!isDeclaredGlobal(node->name())) {
            throw CompileError("variable '" + node->name() + "' is not declared", node->line());
        }
    }

    int envSlot = resolveLocal("_ENV");
    if (envSlot != -1) {
        // _ENV is a local variable
        emitOpCode(OpCode::OP_GET_LOCAL);
        emitByte(static_cast<uint8_t>(envSlot));
        // Stack: [value, env]
        emitConstant(Value::string(internString(node->name())));
        // Stack: [value, env, key]
        emitOpCode(OpCode::OP_ROTATE);
        emitByte(3);
        // Stack: [env, key, value]
        emitOpCode(OpCode::OP_SET_TABLE);
        return;
    }

    int envUpvalue = resolveUpvalue("_ENV");
    if (envUpvalue == -1) {
        // This should not happen since _ENV is upvalue 0 in the top-level
        envUpvalue = 0; 
    }

    size_t nameIndex = currentChunk()->addConstant(Value::string(internString(node->name())));
    emitSetTabUp(static_cast<uint8_t>(envUpvalue), nameIndex);
}

void CodeGenerator::visitFunctionExpr(FunctionExprNode* node) {
    setLine(node->line());

    // Compile function and emit OP_CLOSURE (leaves closure on stack)
    std::string name = expectedName_.empty() ? "anonymous" : expectedName_;
    compileFunction(name, node->params(), node->body(), node->hasVarargs(), node->varargName(), node->lineDefined(), node->lastLineDefined());
}

void CodeGenerator::visitGroupExpr(GroupExprNode* node) {
    setLine(node->line());

    uint8_t oldRetCount = expectedRetCount_;
    bool oldTailCall = isTailCall_;

    // Grouping ALWAYS forces exactly one return value
    expectedRetCount_ = 2; // ONE (1 + 1 = 2)
    isTailCall_ = false;   // Can't be a tail call if it's grouped

    node->expr()->accept(*this);

    expectedRetCount_ = oldRetCount;
    isTailCall_ = oldTailCall;
}

void CodeGenerator::compileFunction(const std::string& name, const std::vector<std::string>& params,
                                   const std::vector<std::unique_ptr<StmtNode>>& body, bool hasVarargs,
                                   const std::string& varargName, int lineDefined, int lastLineDefined) {
    // Save current compiler state and start new function compilation
    pushCompilerState();

    currentVarargName_ = varargName;
    if (!varargName.empty()) {
        VarargEscapeChecker checker(varargName);
        checker.check(body);
        isVarargOptimized_ = !checker.escapes;
    } else {
        isVarargOptimized_ = false;
    }


    // Begin scope for function body
    beginScope();

    // Add parameters as locals (they occupy slots 0, 1, 2, ...)
    for (const auto& param : params) {
        addLocal(param);
    }

    // If named varargs, add the vararg table as a const local parameter
    if (!varargName.empty()) {
        if (isVarargOptimized_) {
            emitOpCode(OpCode::OP_NIL);
        } else {
            emitOpCode(OpCode::OP_PACK_VARARG_TABLE);
        }
        addLocal(varargName, true /* isConstant */);
        namedVarargSlot_ = locals_.back().slot;
    } else {
        namedVarargSlot_ = -1;
    }

    // Compile function body
    compileBlock(body, false);

    // Emit implicit return nil at end (if no explicit return)
    emitOpCode(OpCode::OP_NIL);
    emitOpCode(OpCode::OP_RETURN_VALUE);
    emitByte(1);  // Return count: 1 value (nil)

    // Resolve forward gotos - stubs are emitted here, after return
    emitGotoStubs();

    // Get compiled function chunk (don't call endScope - cleanup is handled by OP_RETURN_VALUE)
    auto functionChunk = std::move(chunk_);

    // Record remaining locals
    size_t endPC = functionChunk->size();
    for (Local& l : locals_) {
        finishedLocals_.push_back({l.name, l.startPC, endPC, l.slot});
    }

#ifdef PRINT_CODE
    functionChunk->disassemble(name);
#endif

    // capturedUpvalues contains the upvalues for the current function.
    std::vector<Upvalue> capturedUpvalues = upvalues_;
    std::vector<LocalVarInfo> capturedLocals = finishedLocals_;
    int capturedNamedVarargSlot = namedVarargSlot_;
    bool capturedIsVarargOptimized = isVarargOptimized_;

    // Restore outer compiler state
    popCompilerState();

    // Create FunctionObject with upvalue count and varargs flag
    functionChunk->setSourceName(currentChunk()->sourceName());
    auto func = new FunctionObject(
        name,
        params.size(),
        std::move(functionChunk),
        capturedUpvalues.size(),
        hasVarargs
    );
    func->setLines(lineDefined, lastLineDefined);

    if (!varargName.empty()) {
        func->setNamedVarargs(capturedNamedVarargSlot, capturedIsVarargOptimized);
    }

    for (const auto& l : capturedLocals) {
        func->addLocalVar(l.name, l.startPC, l.endPC, l.slot);
    }

    for (const auto& uv : capturedUpvalues) {
        func->addUpvalueName(uv.name);
    }

    // Add function to chunk's function pool and get its index
    size_t funcIndex = currentChunk()->addFunction(func);

    // Store function index in constant pool as a Value
    Value funcValue = Value::function(funcIndex);
    size_t constantIndex = currentChunk()->addConstant(funcValue);

    // Emit code to load function
    if (constantIndex <= UINT8_MAX) {
        emitBytes(static_cast<uint8_t>(OpCode::OP_CLOSURE), static_cast<uint8_t>(constantIndex));
    } else if (constantIndex <= 0xFFFFFF) {
        emitBytes(static_cast<uint8_t>(OpCode::OP_CLOSURE_LONG),
                  static_cast<uint8_t>(constantIndex & 0xFF),
                  static_cast<uint8_t>((constantIndex >> 8) & 0xFF),
                  static_cast<uint8_t>((constantIndex >> 16) & 0xFF));
    } else {
        throw CompileError("Too many constants in one chunk", currentLine_);
    }

    // Emit upvalue descriptors (for runtime closure creation)
    for (const Upvalue& uv : capturedUpvalues) {
        emitByte(uv.isLocal ? 1 : 0);  // 1 = local, 0 = upvalue
        emitByte(uv.index);
    }
}

void CodeGenerator::visitReturn(ReturnStmtNode* node) {
    setLine(node->line());

    // Compile all return values
    const auto& values = node->values();
    if (values.size() > 254) {
        throw CompileError("too many returns", node->line());
    }
    if (values.empty()) {
        // Return with no values - return 0 values
        emitOpCode(OpCode::OP_RETURN_VALUE);
        emitByte(0);
    } else {
        bool isLastMultires = false;
        const auto& args = values;
        for (size_t i = 0; i < args.size(); i++) {
            bool canBeMultires = (dynamic_cast<CallExprNode*>(args[i].get()) != nullptr) ||
                                 (dynamic_cast<MethodCallExprNode*>(args[i].get()) != nullptr) ||
                                 (dynamic_cast<VarargExprNode*>(args[i].get()) != nullptr);
            
            uint8_t oldRetCount = expectedRetCount_;
            bool oldTailCall = isTailCall_;
            if (i == args.size() - 1 && canBeMultires) {
                expectedRetCount_ = 0; // All results (0 = ALL)
                isLastMultires = true;
                bool hasCloseVar = false;
                for (const auto& local : locals_) {
                    if (local.isClose) {
                        hasCloseVar = true;
                        break;
                    }
                }
                if (!hasCloseVar && values.size() == 1 && (dynamic_cast<CallExprNode*>(args[i].get()) != nullptr || 
                                                           dynamic_cast<MethodCallExprNode*>(args[i].get()) != nullptr)) {
                    isTailCall_ = true;
                }
            } else {
                expectedRetCount_ = 2; // ONE (1 + 1 = 2)
            }
            args[i]->accept(*this);
            expectedRetCount_ = oldRetCount;
            isTailCall_ = oldTailCall;
        }

        if (isLastMultires) {
            emitOpCode(OpCode::OP_RETURN_VALUE_MULTI);
            emitByte(static_cast<uint8_t>(values.size() - 1)); // Number of fixed expressions
        } else {
            emitOpCode(OpCode::OP_RETURN_VALUE);
            emitByte(static_cast<uint8_t>(values.size()));
        }
    }
}

void CodeGenerator::visitBreak(BreakStmtNode* node) {
    setLine(node->line());

    // Check if we're inside a loop
    if (loopStack_.empty()) {
        throw CompileError("'break' outside of loop", currentLine_);
    }

    // Pop locals created inside the loop before breaking
    int loopLocalCount = loopStack_.back().localCount;
    for (int i = localCount_ - 1; i >= loopLocalCount; i--) {
        emitOpCode(OpCode::OP_CLOSE_UPVALUE);
    }

    // Emit jump and add to list of jumps to patch
    size_t jump = emitJump(OpCode::OP_JUMP);
    addBreakJump(jump);
}

void CodeGenerator::beginLoop() {
    loopStack_.push_back({std::vector<size_t>(), localCount_});
}

void CodeGenerator::endLoop() {
    if (loopStack_.empty()) {
        throw CompileError("endLoop() called without matching beginLoop()", currentLine_);
    }

    // Patch all break jumps to jump to current position
    for (size_t jump : loopStack_.back().jumps) {
        patchJump(jump);
    }

    loopStack_.pop_back();
}

void CodeGenerator::addBreakJump(size_t jump) {
    if (loopStack_.empty()) {
        throw CompileError("addBreakJump() called outside of loop", currentLine_);
    }
    loopStack_.back().jumps.push_back(jump);
}

void CodeGenerator::addLocal(const std::string& name, bool isConstant, bool isClose) {
    if (localCount_ >= 256) {
        throw CompileError("Too many local variables in scope", currentLine_);
    }

    Local local;
    local.name = name;
    local.depth = scopeDepth_;
    local.slot = localCount_++;
    local.isCaptured = false;  // Not captured by default
    local.isConstant = isConstant;
    local.isClose = isClose;
    local.startPC = currentChunk()->size();
    local.seq = ++varSequence_;
    locals_.push_back(local);
    activeVars_.push_back({name, false, scopeDepth_});

    if (isClose) {
        size_t nameIndex = currentChunk()->addConstant(Value::string(internString(name)));
        emitOpCode(OpCode::OP_TBC);
        emitByte(static_cast<uint8_t>(local.slot));
        emitByte(static_cast<uint8_t>(nameIndex));
    }
}

int CodeGenerator::resolveLocal(const std::string& name) {
    const Local* foundLocal = nullptr;
    // Search backwards to find most recent declaration
    for (int i = static_cast<int>(locals_.size()) - 1; i >= 0; i--) {
        if (locals_[i].name == name) {
            foundLocal = &locals_[i];
            break;
        }
    }

    if (foundLocal == nullptr) {
        return -1;
    }

    // Check if shadowed by a declared global in this scope
    for (int i = static_cast<int>(declaredGlobals_.size()) - 1; i >= 0; i--) {
        if (declaredGlobals_[i].name == name) {
            if (declaredGlobals_[i].seq > foundLocal->seq) {
                return -1; // Shadowed by global
            }
            break;
        }
    }

    return foundLocal->slot;
}

bool CodeGenerator::isDeclaredGlobal(const std::string& name) const {
    const DeclaredGlobal* foundGlobal = nullptr;
    for (int i = static_cast<int>(declaredGlobals_.size()) - 1; i >= 0; i--) {
        if (declaredGlobals_[i].name == name) {
            foundGlobal = &declaredGlobals_[i];
            break;
        }
    }
    if (foundGlobal != nullptr) {
        for (int i = static_cast<int>(locals_.size()) - 1; i >= 0; i--) {
            if (locals_[i].name == name) {
                if (locals_[i].seq > foundGlobal->seq) {
                    return false;
                }
                break;
            }
        }
        return true;
    }

    const CompilerState* curr = enclosingCompiler_;
    while (curr != nullptr) {
        const DeclaredGlobal* currGlobal = nullptr;
        for (int i = static_cast<int>(curr->declaredGlobals.size()) - 1; i >= 0; i--) {
            if (curr->declaredGlobals[i].name == name) {
                currGlobal = &curr->declaredGlobals[i];
                break;
            }
        }
        if (currGlobal != nullptr) {
            bool shadowed = false;
            for (int j = static_cast<int>(curr->locals.size()) - 1; j >= 0; j--) {
                if (curr->locals[j].name == name) {
                    if (curr->locals[j].seq > currGlobal->seq) {
                        shadowed = true;
                    }
                    break;
                }
            }
            if (!shadowed) return true;
            break;
        }
        curr = curr->enclosing;
    }
    return false;
}

bool CodeGenerator::isDeclaredGlobalConst(const std::string& name) const {
    const DeclaredGlobal* foundGlobal = nullptr;
    for (int i = static_cast<int>(declaredGlobals_.size()) - 1; i >= 0; i--) {
        if (declaredGlobals_[i].name == name) {
            foundGlobal = &declaredGlobals_[i];
            break;
        }
    }
    if (foundGlobal != nullptr) {
        for (int i = static_cast<int>(locals_.size()) - 1; i >= 0; i--) {
            if (locals_[i].name == name) {
                if (locals_[i].seq > foundGlobal->seq) {
                    return false;
                }
                break;
            }
        }
        return foundGlobal->isConstant;
    }

    const CompilerState* curr = enclosingCompiler_;
    while (curr != nullptr) {
        const DeclaredGlobal* currGlobal = nullptr;
        for (int i = static_cast<int>(curr->declaredGlobals.size()) - 1; i >= 0; i--) {
            if (curr->declaredGlobals[i].name == name) {
                currGlobal = &curr->declaredGlobals[i];
                break;
            }
        }
        if (currGlobal != nullptr) {
            bool shadowed = false;
            for (int j = static_cast<int>(curr->locals.size()) - 1; j >= 0; j--) {
                if (curr->locals[j].name == name) {
                    if (curr->locals[j].seq > currGlobal->seq) {
                        shadowed = true;
                    }
                    break;
                }
            }
            if (!shadowed) return currGlobal->isConstant;
            break;
        }
        curr = curr->enclosing;
    }
    return false;
}

void CodeGenerator::checkConstantAssign(const std::string& name, int line) {
    int slot = resolveLocal(name);
    if (slot != -1) {
        for (int i = static_cast<int>(locals_.size()) - 1; i >= 0; i--) {
            if (locals_[i].slot == slot) {
                if (locals_[i].isConstant) {
                    throw CompileError("attempt to assign to const variable '" + name + "'", line);
                }
                return;
            }
        }
    }

    if (!isDeclaredGlobal(name)) {
        CompilerState* curr = enclosingCompiler_;
        while (curr != nullptr) {
            bool currDeclaredGlobal = false;
            for (const auto& g : curr->declaredGlobals) {
                if (g.name == name) {
                    bool shadowed = false;
                    for (int j = static_cast<int>(curr->locals.size()) - 1; j >= 0; j--) {
                        if (curr->locals[j].name == name) {
                            if (curr->locals[j].seq > g.seq) {
                                shadowed = true;
                            }
                            break;
                        }
                    }
                    if (!shadowed) {
                        currDeclaredGlobal = true;
                        if (g.isConstant) {
                            throw CompileError("attempt to assign to const variable '" + name + "'", line);
                        }
                    }
                    break;
                }
            }
            if (currDeclaredGlobal) break;

            for (int i = static_cast<int>(curr->locals.size()) - 1; i >= 0; i--) {
                if (curr->locals[i].name == name) {
                    if (curr->locals[i].isConstant) {
                        throw CompileError("attempt to assign to const variable '" + name + "'", line);
                    }
                    return;
                }
            }
            curr = curr->enclosing;
        }
    }

    if (isDeclaredGlobal(name)) {
        if (isDeclaredGlobalConst(name)) {
            throw CompileError("attempt to assign to const variable '" + name + "'", line);
        }
        return;
    }

    if (globalMode_ == GlobalMode::CONST_ALL) {
        throw CompileError("attempt to assign to const variable '" + name + "'", line);
    }
}

int CodeGenerator::resolveUpvalue(const std::string& name) {
    // 1. Check if it's already an upvalue in the current chunk
    for (int i = 0; i < static_cast<int>(upvalues_.size()); i++) {
        if (upvalues_[i].name == name) {
            return i;
        }
    }

    if (enclosingCompiler_ == nullptr) {
        return -1;
    }

    // 2. Check parent's locals
    for (int i = static_cast<int>(enclosingCompiler_->locals.size()) - 1; i >= 0; i--) {
        if (enclosingCompiler_->locals[i].name == name) {
            bool shadowed = false;
            for (const auto& g : enclosingCompiler_->declaredGlobals) {
                if (g.name == name && g.seq > enclosingCompiler_->locals[i].seq) {
                    shadowed = true;
                    break;
                }
            }
            if (shadowed) break;

            enclosingCompiler_->locals[i].isCaptured = true;
            return addUpvalue(name, static_cast<uint8_t>(enclosingCompiler_->locals[i].slot), true, enclosingCompiler_->locals[i].isConstant);
        }
    }

    // 3. Check parent's upvalues
    for (int i = 0; i < static_cast<int>(enclosingCompiler_->upvalues.size()); i++) {
        if (enclosingCompiler_->upvalues[i].name == name) {
            return addUpvalue(name, static_cast<uint8_t>(i), false, enclosingCompiler_->upvalues[i].isConstant);
        }
    }

    // 4. Recursively try to resolve in ancestor (via parent)
    int ancestorUpvalue = resolveUpvalueHelper(enclosingCompiler_, name);
    if (ancestorUpvalue != -1) {
        bool isConst = enclosingCompiler_->upvalues[ancestorUpvalue].isConstant;
        return addUpvalue(name, static_cast<uint8_t>(ancestorUpvalue), false, isConst);
    }

    return -1;
}

int CodeGenerator::resolveUpvalueHelper(CompilerState* state, const std::string& name) {
    if (state == nullptr || state->enclosing == nullptr) {
        return -1;
    }

    CompilerState* parent = state->enclosing;

    // Check parent's locals
    for (int i = static_cast<int>(parent->locals.size()) - 1; i >= 0; i--) {
        if (parent->locals[i].name == name) {
            bool shadowed = false;
            for (const auto& g : parent->declaredGlobals) {
                if (g.name == name && g.seq > parent->locals[i].seq) {
                    shadowed = true;
                    break;
                }
            }
            if (shadowed) break;

            parent->locals[i].isCaptured = true;
            
            // Add upvalue to the intermediate state
            for (size_t j = 0; j < state->upvalues.size(); j++) {
                if (state->upvalues[j].name == name) return static_cast<int>(j);
            }
            Upvalue uv;
            uv.name = name;
            uv.index = static_cast<uint8_t>(parent->locals[i].slot);
            uv.isLocal = true;
            uv.isConstant = parent->locals[i].isConstant;
            state->upvalues.push_back(uv);
            return static_cast<int>(state->upvalues.size() - 1);
        }
    }

    // Check parent's upvalues
    for (int i = 0; i < static_cast<int>(parent->upvalues.size()); i++) {
        if (parent->upvalues[i].name == name) {
            // Add upvalue to the intermediate state
            for (size_t j = 0; j < state->upvalues.size(); j++) {
                if (state->upvalues[j].name == name) return static_cast<int>(j);
            }
            Upvalue uv;
            uv.name = name;
            uv.index = static_cast<uint8_t>(i);
            uv.isLocal = false;
            uv.isConstant = parent->upvalues[i].isConstant;
            state->upvalues.push_back(uv);
            return static_cast<int>(state->upvalues.size() - 1);
        }
    }

    // Recursively resolve in ancestor
    int ancestorUpvalue = resolveUpvalueHelper(parent, name);
    if (ancestorUpvalue != -1) {
        for (size_t j = 0; j < state->upvalues.size(); j++) {
            if (state->upvalues[j].name == name) return static_cast<int>(j);
        }
        Upvalue uv;
        uv.name = name;
        uv.index = static_cast<uint8_t>(ancestorUpvalue);
        uv.isLocal = false;
        uv.isConstant = parent->upvalues[ancestorUpvalue].isConstant;
        state->upvalues.push_back(uv);
        return static_cast<int>(state->upvalues.size() - 1);
    }

    return -1;
}

int CodeGenerator::addUpvalue(const std::string& name, uint8_t index, bool isLocal, bool isConstant) {
    // Check if we already have this upvalue (deduplicate)
    for (size_t i = 0; i < upvalues_.size(); i++) {
        if (upvalues_[i].name == name) {
            return static_cast<int>(i);
        }
    }

    // Check upvalue limit
    if (upvalues_.size() >= 256) {
        throw CompileError("Too many upvalues in function", currentLine_);
    }

    // Add new upvalue
    Upvalue uv;
    uv.name = name;
    uv.index = index;
    uv.isLocal = isLocal;
    uv.isConstant = isConstant;
    upvalues_.push_back(uv);

    return static_cast<int>(upvalues_.size() - 1);
}

void CodeGenerator::beginScope() {
    scopeDepth_++;
}

void CodeGenerator::endScope() {
    scopeDepth_--;

    bool hasSpecial = false;
    int targetLocalCount = localCount_;
    for (const auto& l : locals_) {
        if (l.depth > scopeDepth_) {
            if (l.slot < targetLocalCount) targetLocalCount = l.slot;
            if (l.isCaptured || l.isClose) {
                hasSpecial = true;
            }
        }
    }

    if (hasSpecial) {
        emitOpCode(OpCode::OP_CLOSE);
        emitByte(static_cast<uint8_t>(targetLocalCount));
        while (!locals_.empty() && locals_.back().depth > scopeDepth_) {
            Local& l = locals_.back();
            finishedLocals_.push_back({l.name, l.startPC, currentChunk()->size(), l.slot});
            locals_.pop_back();
            localCount_--;
        }
    } else {
        while (!locals_.empty() && locals_.back().depth > scopeDepth_) {
            Local& l = locals_.back();
            finishedLocals_.push_back({l.name, l.startPC, currentChunk()->size(), l.slot});
            emitOpCode(OpCode::OP_POP);
            locals_.pop_back();
            localCount_--;
        }
    }

    // Pop declared globals from this scope
    while (!declaredGlobals_.empty() && declaredGlobals_.back().scopeDepth > scopeDepth_) {
        declaredGlobals_.pop_back();
    }

    while (!activeVars_.empty() && activeVars_.back().depth > scopeDepth_) {
        activeVars_.pop_back();
    }
}

void CodeGenerator::pushCompilerState() {
    // Save current compiler state
    CompilerState state;
    state.chunk = std::move(chunk_);
    state.locals = std::move(locals_);
    state.upvalues = std::move(upvalues_);
    state.finishedLocals = std::move(finishedLocals_);
    state.scopeDepth = scopeDepth_;
    state.localCount = localCount_;
    state.expectedRetCount = expectedRetCount_;
    state.enclosing = enclosingCompiler_;
    state.visibleLabels = std::move(visibleLabels_);
    state.pendingGotos = std::move(pendingGotos_);
    state.activeVars = std::move(activeVars_);
    state.blockScopes = std::move(blockScopes_);
    state.gotosNeedingStubs = std::move(gotosNeedingStubs_);
    state.varargName = currentVarargName_;
    state.namedVarargSlot = namedVarargSlot_;
    state.isVarargOptimized = isVarargOptimized_;
    state.globalMode = globalMode_;
    state.declaredGlobals = std::move(declaredGlobals_);
    state.shadowedLocals = std::move(shadowedLocals_);
    state.envDeclaredGlobal = envDeclaredGlobal_;
    state.varSequence = varSequence_;

    GlobalMode inheritedGlobalMode = globalMode_;

    compilerStack_.push_back(std::move(state));

    // Set enclosing compiler for nested function
    enclosingCompiler_ = &compilerStack_.back();

    // Reset for new function
    std::string sourceName = enclosingCompiler_->chunk->sourceName();
    chunk_ = std::make_unique<Chunk>();
    chunk_->setSourceName(sourceName);
    locals_.clear();
    upvalues_.clear();
    finishedLocals_.clear();
    visibleLabels_.clear();
    pendingGotos_.clear();
    activeVars_.clear();
    blockScopes_.clear();
    gotosNeedingStubs_.clear();
    scopeDepth_ = 0;
    localCount_ = 0;
    expectedRetCount_ = 2; // Default for function body (one result)
    currentVarargName_ = "";
    namedVarargSlot_ = -1;
    isVarargOptimized_ = false;
    globalMode_ = inheritedGlobalMode;
    declaredGlobals_.clear();
    shadowedLocals_.clear();
    envDeclaredGlobal_ = false;
    varSequence_ = 0;
}

void CodeGenerator::popCompilerState() {
    // Restore previous compiler state
    if (compilerStack_.empty()) {
        throw CompileError("Compiler stack underflow", currentLine_);
    }

    CompilerState state = std::move(compilerStack_.back());
    compilerStack_.pop_back();

    chunk_ = std::move(state.chunk);
    locals_ = std::move(state.locals);
    upvalues_ = std::move(state.upvalues);
    finishedLocals_ = std::move(state.finishedLocals);
    visibleLabels_ = std::move(state.visibleLabels);
    pendingGotos_ = std::move(state.pendingGotos);
    activeVars_ = std::move(state.activeVars);
    blockScopes_ = std::move(state.blockScopes);
    gotosNeedingStubs_ = std::move(state.gotosNeedingStubs);
    scopeDepth_ = state.scopeDepth;
    localCount_ = state.localCount;
    expectedRetCount_ = state.expectedRetCount;
    enclosingCompiler_ = state.enclosing;
    currentVarargName_ = state.varargName;
    namedVarargSlot_ = state.namedVarargSlot;
    isVarargOptimized_ = state.isVarargOptimized;
    globalMode_ = state.globalMode;
    declaredGlobals_ = std::move(state.declaredGlobals);
    shadowedLocals_ = std::move(state.shadowedLocals);
    envDeclaredGlobal_ = state.envDeclaredGlobal;
    varSequence_ = state.varSequence;
}

size_t CodeGenerator::internString(const std::string& str) {
    return currentChunk()->addString(str);
}
