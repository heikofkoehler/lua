#include "compiler/codegen.hpp"
#include "value/function.hpp"

CodeGenerator::CodeGenerator()
    : chunk_(nullptr), currentLine_(1), scopeDepth_(0), localCount_(0), enclosingCompiler_(nullptr) {}

std::unique_ptr<Chunk> CodeGenerator::generate(ProgramNode* program) {
    chunk_ = std::make_unique<Chunk>();

    // Generate code for the program
    program->accept(*this);

    // Emit return at end
    emitReturn();

    return std::move(chunk_);
}

void CodeGenerator::visitLiteral(LiteralNode* node) {
    setLine(node->line());

    const Value& value = node->value();

    // Use dedicated opcodes for common constants
    if (value.isNil()) {
        emitOpCode(OpCode::OP_NIL);
    } else if (value.isBool()) {
        emitOpCode(value.asBool() ? OpCode::OP_TRUE : OpCode::OP_FALSE);
    } else if (value.isNumber()) {
        emitConstant(value);
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

    // Compile operand first
    node->operand()->accept(*this);

    // Emit operator instruction
    switch (node->op()) {
        case TokenType::MINUS:
            emitOpCode(OpCode::OP_NEG);
            break;

        case TokenType::NOT:
            emitOpCode(OpCode::OP_NOT);
            break;

        default:
            throw CompileError("Unknown unary operator", node->line());
    }
}

void CodeGenerator::visitBinary(BinaryNode* node) {
    setLine(node->line());

    // Compile left operand
    node->left()->accept(*this);

    // Compile right operand
    node->right()->accept(*this);

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

        case TokenType::PERCENT:
            emitOpCode(OpCode::OP_MOD);
            break;

        case TokenType::CARET:
            emitOpCode(OpCode::OP_POW);
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

        case TokenType::AND:
        case TokenType::OR:
            // For MVP, treat as simple boolean operations
            // TODO: Implement short-circuit evaluation in future
            if (node->op() == TokenType::AND) {
                // a and b: if both truthy, result is b, otherwise false
                emitOpCode(OpCode::OP_NOT);
                emitOpCode(OpCode::OP_NOT);
            }
            break;

        default:
            throw CompileError("Unknown binary operator", node->line());
    }
}

void CodeGenerator::visitVariable(VariableExprNode* node) {
    setLine(node->line());

    // Three-level resolution: local → upvalue → global

    // 1. Try to resolve as local variable
    int slot = resolveLocal(node->name());
    if (slot != -1) {
        emitOpCode(OpCode::OP_GET_LOCAL);
        emitByte(static_cast<uint8_t>(slot));
        return;
    }

    // 2. Try to resolve as upvalue (captured from enclosing scope)
    int upvalue = resolveUpvalue(node->name());
    if (upvalue != -1) {
        emitOpCode(OpCode::OP_GET_UPVALUE);
        emitByte(static_cast<uint8_t>(upvalue));
        return;
    }

    // 3. Fall back to global variable
    size_t nameIndex = currentChunk()->addIdentifier(node->name());
    if (nameIndex > UINT8_MAX) {
        throw CompileError("Too many identifiers in one chunk", currentLine_);
    }
    emitOpCode(OpCode::OP_GET_GLOBAL);
    emitByte(static_cast<uint8_t>(nameIndex));
}

void CodeGenerator::visitPrintStmt(PrintStmtNode* node) {
    setLine(node->line());

    // Compile expression
    node->expr()->accept(*this);

    // Emit print instruction
    emitOpCode(OpCode::OP_PRINT);
}

void CodeGenerator::visitExprStmt(ExprStmtNode* node) {
    setLine(node->line());

    // Compile expression
    node->expr()->accept(*this);

    // Pop the result (expression statements discard their value)
    emitOpCode(OpCode::OP_POP);
}

void CodeGenerator::visitAssignmentStmt(AssignmentStmtNode* node) {
    setLine(node->line());

    // Compile the value
    node->value()->accept(*this);

    // Three-level resolution: local → upvalue → global

    // 1. Try to resolve as local variable
    int slot = resolveLocal(node->name());
    if (slot != -1) {
        emitOpCode(OpCode::OP_SET_LOCAL);
        emitByte(static_cast<uint8_t>(slot));
        emitOpCode(OpCode::OP_POP);
        return;
    }

    // 2. Try to resolve as upvalue
    int upvalue = resolveUpvalue(node->name());
    if (upvalue != -1) {
        emitOpCode(OpCode::OP_SET_UPVALUE);
        emitByte(static_cast<uint8_t>(upvalue));
        emitOpCode(OpCode::OP_POP);
        return;
    }

    // 3. Fall back to global variable
    size_t nameIndex = currentChunk()->addIdentifier(node->name());
    if (nameIndex > UINT8_MAX) {
        throw CompileError("Too many identifiers in one chunk", currentLine_);
    }
    emitOpCode(OpCode::OP_SET_GLOBAL);
    emitByte(static_cast<uint8_t>(nameIndex));
    emitOpCode(OpCode::OP_POP);
}

void CodeGenerator::visitLocalDeclStmt(LocalDeclStmtNode* node) {
    setLine(node->line());

    // Compile initializer
    if (node->initializer()) {
        node->initializer()->accept(*this);
    } else {
        emitOpCode(OpCode::OP_NIL);
    }

    // Add local variable (value is already on stack)
    addLocal(node->name());
}

void CodeGenerator::visitProgram(ProgramNode* node) {
    setLine(node->line());

    // Generate code for each statement
    for (const auto& stmt : node->statements()) {
        stmt->accept(*this);
    }
}

void CodeGenerator::emitByte(uint8_t byte) {
    currentChunk()->write(byte, currentLine_);
}

void CodeGenerator::emitBytes(uint8_t byte1, uint8_t byte2) {
    emitByte(byte1);
    emitByte(byte2);
}

void CodeGenerator::emitOpCode(OpCode op) {
    emitByte(static_cast<uint8_t>(op));
}

void CodeGenerator::emitConstant(const Value& value) {
    size_t index = currentChunk()->addConstant(value);

    if (index > UINT8_MAX) {
        throw CompileError("Too many constants in one chunk", currentLine_);
    }

    emitBytes(static_cast<uint8_t>(OpCode::OP_CONSTANT), static_cast<uint8_t>(index));
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
    for (const auto& stmt : node->thenBranch()) {
        stmt->accept(*this);
    }

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
        for (const auto& stmt : elseIfBranch.body) {
            stmt->accept(*this);
        }

        // Jump to end
        endJumps.push_back(emitJump(OpCode::OP_JUMP));

        // Patch elseif jump
        patchJump(elseIfJump);
        emitOpCode(OpCode::OP_POP);  // Pop condition
    }

    // Compile else branch
    for (const auto& stmt : node->elseBranch()) {
        stmt->accept(*this);
    }

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

    // Compile body
    for (const auto& stmt : node->body()) {
        stmt->accept(*this);
    }

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

    // Compile body
    for (const auto& stmt : node->body()) {
        stmt->accept(*this);
    }

    // Compile condition
    node->condition()->accept(*this);

    // Emit OP_NOT to invert condition (repeat UNTIL becomes repeat WHILE NOT)
    emitOpCode(OpCode::OP_NOT);

    // If inverted condition is false (original was true), exit
    // If inverted condition is true (original was false), loop back
    size_t exitJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
    emitOpCode(OpCode::OP_POP);  // Pop condition

    // Loop back to start
    emitLoop(loopStart);

    // Exit point
    patchJump(exitJump);
    emitOpCode(OpCode::OP_POP);  // Pop condition

    endLoop();  // End loop context and patch all break jumps
}

void CodeGenerator::visitForStmt(ForStmtNode* node) {
    setLine(node->line());

    // Begin scope for loop variables (loop var + hidden limit/step locals)
    beginScope();

    beginLoop();  // Start loop context for break statements

    // Evaluate start expression and create loop variable
    node->start()->accept(*this);
    addLocal(node->varName());

    // Evaluate end expression and store in hidden local
    node->end()->accept(*this);
    addLocal("(for limit)");

    // Evaluate step expression (or default to 1) and store in hidden local
    if (node->step()) {
        node->step()->accept(*this);
    } else {
        emitConstant(Value::number(1.0));
    }
    addLocal("(for step)");

    size_t loopStart = currentChunk()->size();

    // Determine which comparison to use based on step sign
    // For positive step: var <= end
    // For negative step: var >= end

    int varSlot = resolveLocal(node->varName());
    int endSlot = resolveLocal("(for limit)");
    int stepSlot = resolveLocal("(for step)");

    // Get step
    emitOpCode(OpCode::OP_GET_LOCAL);
    emitByte(static_cast<uint8_t>(stepSlot));

    // Push 0
    emitConstant(Value::number(0.0));

    // Check step >= 0
    emitOpCode(OpCode::OP_GREATER_EQUAL);

    // If step >= 0 (true), jump to positive comparison
    // If step < 0 (false), fall through to negative comparison
    size_t positiveJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
    emitOpCode(OpCode::OP_POP);  // Pop step >= 0 result

    // Positive step path: check var <= end
    emitOpCode(OpCode::OP_GET_LOCAL);
    emitByte(static_cast<uint8_t>(varSlot));
    emitOpCode(OpCode::OP_GET_LOCAL);
    emitByte(static_cast<uint8_t>(endSlot));
    emitOpCode(OpCode::OP_LESS_EQUAL);

    // Jump over negative path
    size_t skipNegative = emitJump(OpCode::OP_JUMP);

    // Negative step path: check var >= end
    patchJump(positiveJump);
    emitOpCode(OpCode::OP_POP);  // Pop step >= 0 result

    emitOpCode(OpCode::OP_GET_LOCAL);
    emitByte(static_cast<uint8_t>(varSlot));
    emitOpCode(OpCode::OP_GET_LOCAL);
    emitByte(static_cast<uint8_t>(endSlot));
    emitOpCode(OpCode::OP_GREATER_EQUAL);

    // Both paths converge here with condition result on stack
    patchJump(skipNegative);

    // Exit loop if condition is false
    size_t exitJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
    emitOpCode(OpCode::OP_POP);  // Pop condition

    // Compile body
    for (const auto& stmt : node->body()) {
        stmt->accept(*this);
    }

    // Increment loop variable: var = var + step
    // Get var
    emitOpCode(OpCode::OP_GET_LOCAL);
    emitByte(static_cast<uint8_t>(varSlot));

    // Get step
    emitOpCode(OpCode::OP_GET_LOCAL);
    emitByte(static_cast<uint8_t>(stepSlot));

    // Add
    emitOpCode(OpCode::OP_ADD);

    // Store back to var
    emitOpCode(OpCode::OP_SET_LOCAL);
    emitByte(static_cast<uint8_t>(varSlot));
    emitOpCode(OpCode::OP_POP);  // Pop result of assignment

    // Loop back
    emitLoop(loopStart);

    // Exit point
    patchJump(exitJump);
    emitOpCode(OpCode::OP_POP);  // Pop condition

    endLoop();  // End loop context and patch all break jumps

    // End scope (cleans up loop variable and hidden locals)
    endScope();
}

void CodeGenerator::visitForInStmt(ForInStmtNode* node) {
    setLine(node->line());

    // Begin scope for iterator and loop variable
    beginScope();

    beginLoop();  // Start loop context for break statements

    // Evaluate iterator expression and store in hidden local
    node->iterator()->accept(*this);
    addLocal("(for iterator)");
    int iteratorSlot = resolveLocal("(for iterator)");

    // Initialize loop variable to nil
    emitOpCode(OpCode::OP_NIL);
    addLocal(node->varName());
    int varSlot = resolveLocal(node->varName());

    size_t loopStart = currentChunk()->size();

    // Call iterator with current loop variable value
    // Get iterator function
    emitOpCode(OpCode::OP_GET_LOCAL);
    emitByte(static_cast<uint8_t>(iteratorSlot));

    // Get current loop variable value as argument
    emitOpCode(OpCode::OP_GET_LOCAL);
    emitByte(static_cast<uint8_t>(varSlot));

    // Call iterator(loopVar)
    emitOpCode(OpCode::OP_CALL);
    emitByte(1);  // 1 argument

    // Store result back to loop variable
    emitOpCode(OpCode::OP_SET_LOCAL);
    emitByte(static_cast<uint8_t>(varSlot));
    // Result is left on stack after SET_LOCAL

    // Check if result is nil
    emitOpCode(OpCode::OP_NIL);
    emitOpCode(OpCode::OP_EQUAL);

    // If result is nil, exit loop
    size_t exitJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
    emitOpCode(OpCode::OP_POP);  // Pop comparison result

    // Break out of loop (jump to end)
    size_t breakJump = emitJump(OpCode::OP_JUMP);

    // Not nil, continue with loop body
    patchJump(exitJump);
    emitOpCode(OpCode::OP_POP);  // Pop comparison result

    // Compile body
    for (const auto& stmt : node->body()) {
        stmt->accept(*this);
    }

    // Loop back
    emitLoop(loopStart);

    // Exit point (when iterator returns nil)
    patchJump(breakJump);

    endLoop();  // End loop context and patch all break jumps

    // End scope (cleans up iterator and loop variable)
    endScope();
}

void CodeGenerator::visitCall(CallExprNode* node) {
    setLine(node->line());

    // Load function onto stack using three-level resolution (local → upvalue → global)

    // 1. Try to resolve as local variable
    int slot = resolveLocal(node->name());
    if (slot != -1) {
        emitOpCode(OpCode::OP_GET_LOCAL);
        emitByte(static_cast<uint8_t>(slot));
    }
    // 2. Try to resolve as upvalue (captured from enclosing scope)
    else {
        int upvalue = resolveUpvalue(node->name());
        if (upvalue != -1) {
            emitOpCode(OpCode::OP_GET_UPVALUE);
            emitByte(static_cast<uint8_t>(upvalue));
        }
        // 3. Fall back to global variable
        else {
            size_t nameIndex = currentChunk()->addIdentifier(node->name());
            if (nameIndex > UINT8_MAX) {
                throw CompileError("Too many identifiers in one chunk", currentLine_);
            }
            emitOpCode(OpCode::OP_GET_GLOBAL);
            emitByte(static_cast<uint8_t>(nameIndex));
        }
    }

    // Compile arguments and push onto stack
    for (const auto& arg : node->args()) {
        arg->accept(*this);
    }

    // Emit call instruction with argument count
    size_t argCount = node->args().size();
    if (argCount > UINT8_MAX) {
        throw CompileError("Too many arguments in function call", currentLine_);
    }
    emitOpCode(OpCode::OP_CALL);
    emitByte(static_cast<uint8_t>(argCount));
}

void CodeGenerator::visitTableConstructor(TableConstructorNode* node) {
    setLine(node->line());

    // Emit OP_NEW_TABLE to create a new empty table
    emitOpCode(OpCode::OP_NEW_TABLE);
}

void CodeGenerator::visitIndexExpr(IndexExprNode* node) {
    setLine(node->line());

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

    // Save current compiler state and start new function compilation
    pushCompilerState();

    // Begin scope for function body
    beginScope();

    // Add parameters as locals (they occupy slots 0, 1, 2, ...)
    for (const auto& param : node->params()) {
        addLocal(param);
    }

    // Compile function body
    for (const auto& stmt : node->body()) {
        stmt->accept(*this);
    }

    // Emit implicit return nil at end (if no explicit return)
    emitOpCode(OpCode::OP_NIL);
    emitOpCode(OpCode::OP_RETURN_VALUE);

    // Get compiled function chunk (don't call endScope - cleanup is handled by OP_RETURN_VALUE)
    auto functionChunk = std::move(chunk_);

    // Capture upvalues - important: nested functions may have added upvalues to the CompilerState
    // that's on the stack. We need to get those before we pop.
    std::vector<Upvalue> capturedUpvalues;
    if (!compilerStack_.empty()) {
        // The state on top of the stack has our upvalues (potentially modified by nested compilation)
        capturedUpvalues = compilerStack_.back().upvalues;
    }
    // Also include upvalues from upvalues_ (those added directly during this function's compilation)
    for (const auto& uv : upvalues_) {
        capturedUpvalues.push_back(uv);
    }

    // Restore outer compiler state
    popCompilerState();

    // Create FunctionObject with upvalue count
    auto func = new FunctionObject(
        node->name(),
        node->params().size(),
        std::move(functionChunk),
        capturedUpvalues.size()
    );

    // Add function to chunk's function pool and get its index
    size_t funcIndex = currentChunk()->addFunction(func);

    // Store function index in constant pool as a Value
    Value funcValue = Value::function(funcIndex);
    size_t constantIndex = currentChunk()->addConstant(funcValue);
    if (constantIndex > UINT8_MAX) {
        throw CompileError("Too many constants in one chunk", currentLine_);
    }

    // Emit code to load function and store as global
    emitOpCode(OpCode::OP_CLOSURE);
    emitByte(static_cast<uint8_t>(constantIndex));

    // Emit upvalue descriptors (for runtime closure creation)
    for (const Upvalue& uv : capturedUpvalues) {
        emitByte(uv.isLocal ? 1 : 0);  // 1 = local, 0 = upvalue
        emitByte(uv.index);
    }

    // Store in global variable
    size_t nameIndex = currentChunk()->addIdentifier(node->name());
    if (nameIndex > UINT8_MAX) {
        throw CompileError("Too many identifiers in one chunk", currentLine_);
    }
    emitOpCode(OpCode::OP_SET_GLOBAL);
    emitByte(static_cast<uint8_t>(nameIndex));

    // Pop the function value (SET_GLOBAL leaves it on stack)
    emitOpCode(OpCode::OP_POP);
}

void CodeGenerator::visitReturn(ReturnStmtNode* node) {
    setLine(node->line());

    // Compile return value (or nil if none)
    if (node->value()) {
        node->value()->accept(*this);
    } else {
        emitOpCode(OpCode::OP_NIL);
    }

    // Emit return instruction
    emitOpCode(OpCode::OP_RETURN_VALUE);
}

void CodeGenerator::visitBreak(BreakStmtNode* node) {
    setLine(node->line());

    // Check if we're inside a loop
    if (breakJumps_.empty()) {
        throw CompileError("'break' outside of loop", currentLine_);
    }

    // Emit jump and add to list of jumps to patch
    size_t jump = emitJump(OpCode::OP_JUMP);
    addBreakJump(jump);
}

void CodeGenerator::beginLoop() {
    breakJumps_.push_back(std::vector<size_t>());
}

void CodeGenerator::endLoop() {
    if (breakJumps_.empty()) {
        throw CompileError("endLoop() called without matching beginLoop()", currentLine_);
    }

    // Patch all break jumps to jump to current position
    for (size_t jump : breakJumps_.back()) {
        patchJump(jump);
    }

    breakJumps_.pop_back();
}

void CodeGenerator::addBreakJump(size_t jump) {
    if (breakJumps_.empty()) {
        throw CompileError("addBreakJump() called outside of loop", currentLine_);
    }
    breakJumps_.back().push_back(jump);
}

void CodeGenerator::addLocal(const std::string& name) {
    if (localCount_ >= 256) {
        throw CompileError("Too many local variables in scope", currentLine_);
    }

    Local local;
    local.name = name;
    local.depth = scopeDepth_;
    local.slot = localCount_++;
    local.isCaptured = false;  // Not captured by default
    locals_.push_back(local);
}

int CodeGenerator::resolveLocal(const std::string& name) {
    // Search backwards to find most recent declaration
    for (int i = static_cast<int>(locals_.size()) - 1; i >= 0; i--) {
        if (locals_[i].name == name) {
            return locals_[i].slot;
        }
    }
    return -1;  // Not found, must be global
}

int CodeGenerator::resolveUpvalue(const std::string& name) {
    if (enclosingCompiler_ == nullptr) {
        return -1;
    }

    // Try to find variable in immediate parent's locals
    for (int i = static_cast<int>(enclosingCompiler_->locals.size()) - 1; i >= 0; i--) {
        if (enclosingCompiler_->locals[i].name == name) {
            // Mark the local as captured
            enclosingCompiler_->locals[i].isCaptured = true;
            // Add upvalue to current function that captures this local
            return addUpvalue(enclosingCompiler_->locals[i].slot, true);
        }
    }

    // Not found in parent's locals, check parent's upvalues (grandparent capture)
    // Use helper to recursively search ancestor scopes
    int ancestorUpvalue = resolveUpvalueHelper(enclosingCompiler_, name);
    if (ancestorUpvalue != -1) {
        // Parent (or ancestor) has this as an upvalue, reference it
        return addUpvalue(ancestorUpvalue, false);
    }

    return -1;
}

int CodeGenerator::resolveUpvalueHelper(CompilerState* compiler, const std::string& name) {
    if (compiler == nullptr || compiler->enclosing == nullptr) {
        return -1;
    }

    CompilerState* parent = compiler->enclosing;

    // Check parent's locals
    for (int i = static_cast<int>(parent->locals.size()) - 1; i >= 0; i--) {
        if (parent->locals[i].name == name) {
            // Found in parent's locals
            parent->locals[i].isCaptured = true;

            // Add upvalue to compiler (the intermediate function) that captures this local
            Upvalue uv;
            uv.index = parent->locals[i].slot;
            uv.isLocal = true;
            uv.name = name;
            compiler->upvalues.push_back(uv);

            return compiler->upvalues.size() - 1;
        }
    }

    // Not in parent's locals, search recursively
    int ancestorUpvalue = resolveUpvalueHelper(parent, name);
    if (ancestorUpvalue != -1) {
        // Found in an ancestor, add upvalue to intermediate compiler
        Upvalue uv;
        uv.index = ancestorUpvalue;
        uv.isLocal = false;
        uv.name = name;
        compiler->upvalues.push_back(uv);

        return compiler->upvalues.size() - 1;
    }

    return -1;
}

int CodeGenerator::addUpvalue(uint8_t index, bool isLocal) {
    // Check if we already have this upvalue (deduplicate)
    for (size_t i = 0; i < upvalues_.size(); i++) {
        if (upvalues_[i].index == index && upvalues_[i].isLocal == isLocal) {
            return i;  // Reuse existing upvalue
        }
    }

    // Check upvalue limit
    if (upvalues_.size() >= 256) {
        throw CompileError("Too many upvalues in function", currentLine_);
    }

    // Add new upvalue
    Upvalue uv;
    uv.index = index;
    uv.isLocal = isLocal;
    upvalues_.push_back(uv);

    return upvalues_.size() - 1;
}

void CodeGenerator::beginScope() {
    scopeDepth_++;
}

void CodeGenerator::endScope() {
    scopeDepth_--;

    // Pop locals from this scope
    while (!locals_.empty() && locals_.back().depth > scopeDepth_) {
        if (locals_.back().isCaptured) {
            // Close the upvalue instead of just popping
            emitOpCode(OpCode::OP_CLOSE_UPVALUE);
        } else {
            emitOpCode(OpCode::OP_POP);
        }
        locals_.pop_back();
        localCount_--;
    }
}

void CodeGenerator::pushCompilerState() {
    // Save current compiler state
    CompilerState state;
    state.chunk = std::move(chunk_);
    state.locals = std::move(locals_);
    state.upvalues = std::move(upvalues_);
    state.scopeDepth = scopeDepth_;
    state.localCount = localCount_;
    state.enclosing = enclosingCompiler_;

    compilerStack_.push_back(std::move(state));

    // Set enclosing compiler for nested function
    enclosingCompiler_ = &compilerStack_.back();

    // Reset for new function
    chunk_ = std::make_unique<Chunk>();
    locals_.clear();
    upvalues_.clear();
    scopeDepth_ = 0;
    localCount_ = 0;
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
    scopeDepth_ = state.scopeDepth;
    localCount_ = state.localCount;
    enclosingCompiler_ = state.enclosing;
}
