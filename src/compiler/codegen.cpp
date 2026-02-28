#include "compiler/codegen.hpp"
#include "value/function.hpp"
#include "value/string.hpp"

CodeGenerator::CodeGenerator()
    : chunk_(nullptr), currentLine_(1), scopeDepth_(0), localCount_(0), enclosingCompiler_(nullptr), expectedRetCount_(2) {}

std::unique_ptr<FunctionObject> CodeGenerator::generate(ProgramNode* program, const std::string& name) {
    chunk_ = std::make_unique<Chunk>();
    upvalues_.clear();
    locals_.clear();
    scopeDepth_ = 0;
    localCount_ = 0;
    enclosingCompiler_ = nullptr; // Reset to top level

    // Root compiler state initialization
    // _ENV is the first upvalue by convention in Lua 5.2+ for the top-level chunk.
    // When compiling any chunk (top-level or via load/require), we ensure _ENV is upvalue 0.
    Upvalue env;
    env.name = "_ENV";
    env.index = 0; 
    env.isLocal = false; 
    upvalues_.push_back(env);

    // Generate code for the program

    program->accept(*this);

    // Emit return at end
    emitReturn();

    // Close remaining locals (top level)
    size_t endPC = currentChunk()->size();
    for (Local& l : locals_) {
        finishedLocals_.push_back({l.name, l.startPC, endPC, l.slot});
    }

    auto function = std::make_unique<FunctionObject>(name, 0, std::move(chunk_), static_cast<int>(upvalues_.size()), true);
    for (const auto& l : finishedLocals_) {
        function->addLocalVar(l.name, l.startPC, l.endPC, l.slot);
    }
    return function;
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

    // Handle short-circuiting logical operators
    if (node->op() == TokenType::AND) {
        node->left()->accept(*this);
        size_t endJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
        emitOpCode(OpCode::OP_POP); // Pop left value
        node->right()->accept(*this);
        patchJump(endJump);
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
        return;
    }

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
    int upvalue = resolveUpvalue(name);
    if (upvalue != -1) {
        emitOpCode(OpCode::OP_GET_UPVALUE);
        emitByte(static_cast<uint8_t>(upvalue));
        return;
    }

    // 3. Fall back to global variable (resolved via _ENV)
    size_t nameIndex = currentChunk()->addConstant(Value::string(internString(name)));
    if (nameIndex > UINT8_MAX) {
        throw CompileError("Too many constants in one chunk", currentLine_);
    }

    int envSlot = resolveLocal("_ENV");
    if (envSlot != -1) {
        // _ENV is a local variable
        emitOpCode(OpCode::OP_GET_LOCAL);
        emitByte(static_cast<uint8_t>(envSlot));
        emitOpCode(OpCode::OP_CONSTANT);
        emitByte(static_cast<uint8_t>(nameIndex));
        emitOpCode(OpCode::OP_GET_TABLE);
        return;
    }

    int envUpvalue = resolveUpvalue("_ENV");
    if (envUpvalue == -1) {
        envUpvalue = 0; 
    }
    
    emitOpCode(OpCode::OP_GET_TABUP);
    emitByte(static_cast<uint8_t>(envUpvalue));
    emitByte(static_cast<uint8_t>(nameIndex));
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
    if (dynamic_cast<CallExprNode*>(node->expr()) == nullptr) {
        emitOpCode(OpCode::OP_POP);
    }
}

void CodeGenerator::visitAssignmentStmt(AssignmentStmtNode* node) {
    setLine(node->line());

    // Compile the value
    expectedName_ = node->name();
    node->value()->accept(*this);
    expectedName_ = "";

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

    // 3. Fall back to global variable (via _ENV)
    size_t nameIndex = currentChunk()->addConstant(Value::string(internString(node->name())));
    if (nameIndex > UINT8_MAX) {
        throw CompileError("Too many constants in one chunk", currentLine_);
    }

    int envSlot = resolveLocal("_ENV");
    if (envSlot != -1) {
        // _ENV is a local variable
        emitOpCode(OpCode::OP_GET_LOCAL);
        emitByte(static_cast<uint8_t>(envSlot));
        // Stack: [value, env]
        emitOpCode(OpCode::OP_CONSTANT);
        emitByte(static_cast<uint8_t>(nameIndex));
        // Stack: [value, env, key]
        emitOpCode(OpCode::OP_ROTATE);
        emitByte(3); 
        // Stack: [env, key, value]
        emitOpCode(OpCode::OP_SET_TABLE);
        return;
    }

    int envUpvalue = resolveUpvalue("_ENV");
    if (envUpvalue == -1) {
        envUpvalue = 0;
    }
    
    emitOpCode(OpCode::OP_SET_TABUP);
    emitByte(static_cast<uint8_t>(envUpvalue));
    emitByte(static_cast<uint8_t>(nameIndex));
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
        emitOpCode(OpCode::OP_POP); // Set local doesn't pop in our VM?
        // Wait, OP_SET_LOCAL in our VM DOES NOT pop. Let's check.
    } else {
        // Compile initializer
        if (node->initializer()) {
            node->initializer()->accept(*this);
        } else {
            emitOpCode(OpCode::OP_NIL);
        }

        // Add local variable (value is already on stack)
        addLocal(node->name());
    }
}

void CodeGenerator::visitMultipleLocalDeclStmt(MultipleLocalDeclStmtNode* node) {
    setLine(node->line());

    const auto& names = node->names();
    const auto& initializers = node->initializers();

    size_t varCount = names.size();
    size_t initCount = initializers.size();

    uint8_t oldRetCount = expectedRetCount_;

    // 1. Evaluate all initializers except the last one
    for (size_t i = 0; i < (initCount > 0 ? initCount - 1 : 0); i++) {
        expectedRetCount_ = 2; // ONE
        initializers[i]->accept(*this);
    }

    // 2. Evaluate the last initializer with multires if needed
    if (initCount > 0) {
        if (varCount > initCount) {
            // Last expression needs to provide multiple values
            expectedRetCount_ = static_cast<uint8_t>(varCount - (initCount - 1) + 1);
        } else {
            expectedRetCount_ = 2; // ONE
        }
        initializers[initCount - 1]->accept(*this);
    }

    expectedRetCount_ = oldRetCount;

    // 3. Pad with nil if fewer values than variables (but not covered by multires)
    if (initCount < varCount) {
        auto* lastInit = initCount > 0 ? initializers[initCount - 1].get() : nullptr;
        bool lastIsCall = lastInit && dynamic_cast<CallExprNode*>(lastInit);
        
        if (!lastIsCall) {
            for (size_t i = initCount; i < varCount; i++) {
                emitOpCode(OpCode::OP_NIL);
            }
        }
    } else if (initCount > varCount) {
        // 4. Discard excess values if more values than variables
        for (size_t i = varCount; i < initCount; i++) {
            emitOpCode(OpCode::OP_POP);
        }
    }

    // 5. Add local variables (values are already on stack in correct order)
    for (const auto& name : names) {
        addLocal(name);
    }
}

void CodeGenerator::visitMultipleAssignmentStmt(MultipleAssignmentStmtNode* node) {
    setLine(node->line());

    const auto& names = node->names();
    const auto& values = node->values();

    size_t varCount = names.size();
    size_t valCount = values.size();

    uint8_t oldRetCount = expectedRetCount_;

    // 1. Evaluate all values except the last one
    for (size_t i = 0; i < (valCount > 0 ? valCount - 1 : 0); i++) {
        expectedRetCount_ = 2;
        values[i]->accept(*this);
    }

    // 2. Evaluate the last value with multires if needed
    if (valCount > 0) {
        if (varCount > valCount) {
            expectedRetCount_ = static_cast<uint8_t>(varCount - (valCount - 1) + 1);
        } else {
            expectedRetCount_ = 2;
        }
        values[valCount - 1]->accept(*this);
    }

    expectedRetCount_ = oldRetCount;

    // 3. Pad with nil or discard excess
    if (valCount < varCount) {
        auto* lastVal = valCount > 0 ? values[valCount - 1].get() : nullptr;
        bool lastIsCall = lastVal && dynamic_cast<CallExprNode*>(lastVal);
        
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
    for (int i = varCount - 1; i >= 0; i--) {
        const std::string& name = names[i];

        // Three-level resolution: local → upvalue → global
        int slot = resolveLocal(name);
        if (slot != -1) {
            emitOpCode(OpCode::OP_SET_LOCAL);
            emitByte(static_cast<uint8_t>(slot));
            emitOpCode(OpCode::OP_POP);
            continue;
        }

        int upvalue = resolveUpvalue(name);
        if (upvalue != -1) {
            emitOpCode(OpCode::OP_SET_UPVALUE);
            emitByte(static_cast<uint8_t>(upvalue));
            emitOpCode(OpCode::OP_POP);
            continue;
        }

        // Global variable (via _ENV)
        size_t nameIndex = currentChunk()->addConstant(Value::string(internString(name)));
        if (nameIndex > UINT8_MAX) {
            throw CompileError("Too many constants in one chunk", currentLine_);
        }

        int envSlot = resolveLocal("_ENV");
        if (envSlot != -1) {
            // _ENV is a local variable
            emitOpCode(OpCode::OP_GET_LOCAL);
            emitByte(static_cast<uint8_t>(envSlot));
            // Stack: [value, env]
            emitOpCode(OpCode::OP_CONSTANT);
            emitByte(static_cast<uint8_t>(nameIndex));
            // Stack: [value, env, key]
            emitOpCode(OpCode::OP_ROTATE);
            emitByte(3);
            // Stack: [env, key, value]
            emitOpCode(OpCode::OP_SET_TABLE);
            continue;
        }

        int envUpvalue = resolveUpvalue("_ENV");
        if (envUpvalue == -1) {
            envUpvalue = 0;
        }
        
        emitOpCode(OpCode::OP_SET_TABUP);
        emitByte(static_cast<uint8_t>(envUpvalue));
        emitByte(static_cast<uint8_t>(nameIndex));
    }
}

void CodeGenerator::visitGoto(GotoStmtNode* node) {
    setLine(node->line());
    const std::string& name = node->label();
    
    auto it = labels_.find(name);
    if (it != labels_.end()) {
        // Backward jump
        if (localCount_ < it->second.localCount) {
            throw CompileError("<goto " + name + "> jumps into the scope of local variables", currentLine_);
        }
        
        // Pop locals to reach label's local count
        // For simplicity and safety, we use OP_CLOSE_UPVALUE which also pops
        for (int i = localCount_ - 1; i >= it->second.localCount; i--) {
            emitOpCode(OpCode::OP_CLOSE_UPVALUE);
        }
        
        // Our OP_LOOP jumps backward. The offset is relative to the instruction after OP_LOOP
        emitLoop(it->second.offset);
    } else {
        // Forward jump - unresolved
        size_t jump = emitJump(OpCode::OP_JUMP);
        unresolvedGotos_.push_back({name, jump, localCount_, scopeDepth_, currentLine_});
    }
}

void CodeGenerator::visitLabel(LabelStmtNode* node) {
    setLine(node->line());
    const std::string& name = node->label();
    
    if (labels_.find(name) != labels_.end()) {
        throw CompileError("label '" + name + "' already defined", currentLine_);
    }
    
    Label lbl = {currentChunk()->size(), localCount_, scopeDepth_};
    labels_[name] = lbl;
}

void CodeGenerator::visitBlock(BlockStmtNode* node) {
    setLine(node->line());
    beginScope();
    for (const auto& stmt : node->statements()) {
        stmt->accept(*this);
    }
    endScope();
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
    beginScope();
    for (const auto& stmt : node->thenBranch()) {
        stmt->accept(*this);
    }
    endScope();

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
        for (const auto& stmt : elseIfBranch.body) {
            stmt->accept(*this);
        }
        endScope();

        // Jump to end
        endJumps.push_back(emitJump(OpCode::OP_JUMP));

        // Patch elseif jump
        patchJump(elseIfJump);
        emitOpCode(OpCode::OP_POP);  // Pop condition
    }

    // Compile else branch
    beginScope();
    for (const auto& stmt : node->elseBranch()) {
        stmt->accept(*this);
    }
    endScope();

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
    for (const auto& stmt : node->body()) {
        stmt->accept(*this);
    }
    endScope();

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
        stmt->accept(*this);
    }
    endScope();

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

    beginLoop();  // Start loop context for break statements

    size_t loopStart = currentChunk()->size();

    int varSlot = resolveLocal(node->varName());
    int endSlot = resolveLocal("(for limit)");
    int stepSlot = resolveLocal("(for step)");

    // Check step >= 0
    emitOpCode(OpCode::OP_GET_LOCAL);
    emitByte(static_cast<uint8_t>(stepSlot));
    emitConstant(Value::number(0.0));
    emitOpCode(OpCode::OP_GREATER_EQUAL);

    // If step >= 0 (true), jump to positive comparison
    // If step < 0 (false), fall through to negative comparison
    size_t positiveJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
    
    // Positive step path: check var <= end
    emitOpCode(OpCode::OP_POP);  // Pop step >= 0 result (true)
    emitOpCode(OpCode::OP_GET_LOCAL);
    emitByte(static_cast<uint8_t>(varSlot));
    emitOpCode(OpCode::OP_GET_LOCAL);
    emitByte(static_cast<uint8_t>(endSlot));
    emitOpCode(OpCode::OP_LESS_EQUAL);
    size_t skipNegative = emitJump(OpCode::OP_JUMP);

    // Negative step path: check var >= end
    patchJump(positiveJump);
    emitOpCode(OpCode::OP_POP);  // Pop step >= 0 result (false)
    emitOpCode(OpCode::OP_GET_LOCAL);
    emitByte(static_cast<uint8_t>(varSlot));
    emitOpCode(OpCode::OP_GET_LOCAL);
    emitByte(static_cast<uint8_t>(endSlot));
    emitOpCode(OpCode::OP_GREATER_EQUAL);

    // Both paths converge here with condition result on stack
    patchJump(skipNegative);

    // Exit loop if condition is false
    size_t exitJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
    emitOpCode(OpCode::OP_POP);  // Pop condition (true case)

    // Compile body
    beginScope();
    for (const auto& stmt : node->body()) {
        stmt->accept(*this);
    }
    endScope();

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
    emitOpCode(OpCode::OP_POP);  // Pop condition (false)

    endLoop();  // End loop context and patch all break jumps

    // End scope (cleans up loop variable and hidden locals)
    endScope();
}


void CodeGenerator::visitForInStmt(ForInStmtNode* node) {
    setLine(node->line());

    // Begin scope for iterator state and loop variables
    beginScope();

    beginLoop();  // Start loop context for break statements

    // Evaluate iterator expression
    // We expect 3 values: iterator, state, control_var
    // If iterator expression is a call, we can ask for 3 values.
    // If it's not a call (e.g. variable), we get 1 value and pad with 2 nils.
    
    // Save current expected return count and set to 3 for iterator init
    uint8_t oldRetCount = expectedRetCount_;
    expectedRetCount_ = 4; // 3 results (3+1=4)
    node->iterator()->accept(*this);
    expectedRetCount_ = oldRetCount; // Restore
    
    // Store iterator state in hidden locals
    // They are already on stack in order [iter, state, control]
    addLocal("(for iterator)");
    addLocal("(for state)");
    addLocal("(for control)");
    
    int iteratorSlot = resolveLocal("(for iterator)");
    int stateSlot = resolveLocal("(for state)");
    int controlSlot = resolveLocal("(for control)");

    // Add loop variables (initialized to nil)
    const auto& varNames = node->varNames();
    for (const auto& name : varNames) {
        emitOpCode(OpCode::OP_NIL);
        addLocal(name);
    }
    
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
    for (const auto& stmt : node->body()) {
        stmt->accept(*this);
    }
    endScope();

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
            
            // Compile arguments
            for (const auto& arg : node->args()) {
                arg->accept(*this);
            }
            // Emit yield instruction
            emitOpCode(OpCode::OP_YIELD);
            if (node->args().size() > UINT8_MAX) {
                throw CompileError("Too many arguments to yield", currentLine_);
            }
            emitByte(static_cast<uint8_t>(node->args().size()));
            
            uint8_t yieldRetCount = expectedRetCount_;
            // If yield is used as an expression statement, expectedRetCount_ is 1 but it should be popped.
            // Wait, visitExprStmt will pop 1. So we should ask for 1.
            emitByte(yieldRetCount);
            return;
        }
    }

    // Compile the callee expression (evaluates to a function on the stack)
    // Save current expected return count
    uint8_t oldRetCount = expectedRetCount_;
    
    expectedRetCount_ = 2; // The callee itself expects 1 result (1 + 1 = 2)
    node->callee()->accept(*this);
    
    // Compile arguments and push onto stack
    const auto& args = node->args();
    bool isLastMultires = false;
    
    for (size_t i = 0; i < args.size(); i++) {
        bool canBeMultires = (dynamic_cast<CallExprNode*>(args[i].get()) != nullptr) ||
                             (dynamic_cast<VarargExprNode*>(args[i].get()) != nullptr);
        
        if (i == args.size() - 1 && canBeMultires) {
            expectedRetCount_ = 0; // Last argument can be multires (0 = ALL)
            isLastMultires = true;
        } else {
            expectedRetCount_ = 2; // ONE (1 + 1 = 2)
        }
        args[i]->accept(*this);
    }
    
    // Restore expected return count for OP_CALL
    expectedRetCount_ = oldRetCount;

    // Emit call instruction
    if (isTailCall_) {
        if (isLastMultires) {
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
    // Wait, our VM expects [callee, arg1, arg2...].
    // So we need: [method, obj, arg1, arg2...]
    // Our stack currently has [obj, method].
    // We can use a new opcode OP_SWAP or just manage it.
    
    // Let's use a temporary local or just swap?
    // Actually, it's easier to:
    // 1. Evaluate object [obj]
    // 2. Duplicate [obj, obj]
    // 3. Get method [obj, method]
    // 4. Swap [method, obj] <-- 'obj' is now the first argument (self)
    
    // I don't have OP_SWAP. I'll add it or find another way.
    // Alternative:
    // 1. Evaluate object [obj]
    // 2. Duplicate [obj, obj]
    // 3. Constant method name [obj, obj, "method"]
    // 4. OP_GET_TABLE [obj, method]
    
    // If I add OP_SWAP it's easiest.
    emitOpCode(OpCode::OP_SWAP);
    
    // Now stack: [method, obj]
    
    // Compile arguments and push onto stack
    const auto& args = node->args();
    bool isLastMultires = false;
    
    for (size_t i = 0; i < args.size(); i++) {
        bool canBeMultires = (dynamic_cast<CallExprNode*>(args[i].get()) != nullptr) ||
                             (dynamic_cast<VarargExprNode*>(args[i].get()) != nullptr);
        
        if (i == args.size() - 1 && canBeMultires) {
            expectedRetCount_ = 0; // Last argument can be multires (0 = ALL)
            isLastMultires = true;
        } else {
            expectedRetCount_ = 2; // ONE (1 + 1 = 2)
        }
        args[i]->accept(*this);
    }
    
    // Restore expected return count
    expectedRetCount_ = oldRetCount;

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
                                 (dynamic_cast<VarargExprNode*>(entry.value.get()) != nullptr);
            bool isLast = (i == entries.size() - 1);

            // Array-style entry: use implicit numeric index
            emitConstant(Value::number(arrayIndex));
            
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

    // Compile function and emit OP_CLOSURE
    compileFunction(node->name(), node->params(), node->body(), node->hasVarargs());

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

void CodeGenerator::visitFunctionExpr(FunctionExprNode* node) {
    setLine(node->line());
    
    // Compile function and emit OP_CLOSURE (leaves closure on stack)
    std::string name = expectedName_.empty() ? "anonymous" : expectedName_;
    compileFunction(name, node->params(), node->body(), node->hasVarargs());
}

void CodeGenerator::compileFunction(const std::string& name, const std::vector<std::string>& params,
                                   const std::vector<std::unique_ptr<StmtNode>>& body, bool hasVarargs) {
    // Save current compiler state and start new function compilation
    pushCompilerState();

    // Ensure _ENV is the first upvalue (index 0)
    // resolveUpvalue will find it in the parent and add it to current upvalues_
    resolveUpvalue("_ENV");

    // Begin scope for function body
    beginScope();

    // Add parameters as locals (they occupy slots 0, 1, 2, ...)
    for (const auto& param : params) {
        addLocal(param);
    }

    // Compile function body
    for (const auto& stmt : body) {
        stmt->accept(*this);
    }

    // Resolve forward gotos
    for (const Goto& g : unresolvedGotos_) {
        auto it = labels_.find(g.name);
        if (it == labels_.end()) {
            throw CompileError("no visible label '" + g.name + "' for <goto>", g.line);
        }
        
        if (g.localCount < it->second.localCount) {
            throw CompileError("<goto " + g.name + "> jumps into the scope of local variables", g.line);
        }
        
        if (g.localCount == it->second.localCount) {
            // Direct jump
            size_t jumpDist = it->second.offset - g.instructionOffset - 2;
            currentChunk()->code()[g.instructionOffset] = jumpDist & 0xff;
            currentChunk()->code()[g.instructionOffset + 1] = (jumpDist >> 8) & 0xff;
        } else {
            // Need a cleanup stub
            size_t stubOffset = currentChunk()->size();
            
            // Patch original JUMP to jump to the stub
            size_t jumpToStub = stubOffset - g.instructionOffset - 2;
            currentChunk()->code()[g.instructionOffset] = jumpToStub & 0xff;
            currentChunk()->code()[g.instructionOffset + 1] = (jumpToStub >> 8) & 0xff;
            
            // Generate cleanup stub
            for (int i = g.localCount - 1; i >= it->second.localCount; i--) {
                emitOpCode(OpCode::OP_CLOSE_UPVALUE);
            }
            
            // Jump from stub to label (forward or backward depending on where the label is relative to stub)
            // Wait, the label is always BEFORE the stub, because the label was defined in the function body,
            // and the stub is appended at the very end of the function body!
            // So it's ALWAYS a backward jump from the stub to the label.
            emitLoop(it->second.offset);
        }
    }

    // Emit implicit return nil at end (if no explicit return)
    emitOpCode(OpCode::OP_NIL);
    emitOpCode(OpCode::OP_RETURN_VALUE);
    emitByte(1);  // Return count: 1 value (nil)

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

    // Restore outer compiler state
    popCompilerState();

    // Create FunctionObject with upvalue count and varargs flag
    auto func = new FunctionObject(
        name,
        params.size(),
        std::move(functionChunk),
        capturedUpvalues.size(),
        hasVarargs
    );

    for (const auto& l : capturedLocals) {
        func->addLocalVar(l.name, l.startPC, l.endPC, l.slot);
    }

    // Add function to chunk's function pool and get its index
    size_t funcIndex = currentChunk()->addFunction(func);

    // Store function index in constant pool as a Value
    Value funcValue = Value::function(funcIndex);
    size_t constantIndex = currentChunk()->addConstant(funcValue);
    if (constantIndex > UINT8_MAX) {
        throw CompileError("Too many constants in one chunk", currentLine_);
    }

    // Emit code to load function
    emitOpCode(OpCode::OP_CLOSURE);
    emitByte(static_cast<uint8_t>(constantIndex));

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
    if (values.empty()) {
        // Return with no values - just return nil
        emitOpCode(OpCode::OP_NIL);
        emitOpCode(OpCode::OP_RETURN_VALUE);
        emitByte(1);  // Returning 1 value (nil)
    } else {
        bool isLastMultires = false;
        const auto& args = values;
        for (size_t i = 0; i < args.size(); i++) {
            bool canBeMultires = (dynamic_cast<CallExprNode*>(args[i].get()) != nullptr) ||
                                 (dynamic_cast<VarargExprNode*>(args[i].get()) != nullptr);
            
            uint8_t oldRetCount = expectedRetCount_;
            bool oldTailCall = isTailCall_;
            if (i == args.size() - 1 && canBeMultires) {
                expectedRetCount_ = 0; // All results (0 = ALL)
                isLastMultires = true;
                if (values.size() == 1 && (dynamic_cast<CallExprNode*>(args[i].get()) != nullptr || 
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

        // Emit return instruction with count
        emitOpCode(OpCode::OP_RETURN_VALUE);
        if (isLastMultires) {
            emitByte(0); // 0 means use lastResultCount
        } else {
            size_t count = values.size();
            emitByte(static_cast<uint8_t>(count));
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

void CodeGenerator::addLocal(const std::string& name) {
    if (localCount_ >= 256) {
        throw CompileError("Too many local variables in scope", currentLine_);
    }

    Local local;
    local.name = name;
    local.depth = scopeDepth_;
    local.slot = localCount_++;
    local.isCaptured = false;  // Not captured by default
    local.startPC = currentChunk()->size();
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
            enclosingCompiler_->locals[i].isCaptured = true;
            return addUpvalue(name, static_cast<uint8_t>(enclosingCompiler_->locals[i].slot), true);
        }
    }

    // 3. Check parent's upvalues
    for (int i = 0; i < static_cast<int>(enclosingCompiler_->upvalues.size()); i++) {
        if (enclosingCompiler_->upvalues[i].name == name) {
            return addUpvalue(name, static_cast<uint8_t>(i), false);
        }
    }

    // 4. Recursively try to resolve in ancestor (via parent)
    int ancestorUpvalue = resolveUpvalueHelper(enclosingCompiler_, name);
    if (ancestorUpvalue != -1) {
        return addUpvalue(name, static_cast<uint8_t>(ancestorUpvalue), false);
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
            parent->locals[i].isCaptured = true;
            
            // Add upvalue to the intermediate state
            for (size_t j = 0; j < state->upvalues.size(); j++) {
                if (state->upvalues[j].name == name) return static_cast<int>(j);
            }
            Upvalue uv;
            uv.name = name;
            uv.index = static_cast<uint8_t>(parent->locals[i].slot);
            uv.isLocal = true;
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
        state->upvalues.push_back(uv);
        return static_cast<int>(state->upvalues.size() - 1);
    }

    return -1;
}

int CodeGenerator::addUpvalue(const std::string& name, uint8_t index, bool isLocal) {
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
    upvalues_.push_back(uv);

    return static_cast<int>(upvalues_.size() - 1);
}

void CodeGenerator::beginScope() {
    scopeDepth_++;
}

void CodeGenerator::endScope() {
    scopeDepth_--;

    // Pop locals from this scope
    while (!locals_.empty() && locals_.back().depth > scopeDepth_) {
        // Record debug info for finished local
        Local& l = locals_.back();
        finishedLocals_.push_back({l.name, l.startPC, currentChunk()->size(), l.slot});

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
    state.finishedLocals = std::move(finishedLocals_);
    state.scopeDepth = scopeDepth_;
    state.localCount = localCount_;
    state.expectedRetCount = expectedRetCount_;
    state.enclosing = enclosingCompiler_;
    state.labels = std::move(labels_);
    state.unresolvedGotos = std::move(unresolvedGotos_);

    compilerStack_.push_back(std::move(state));

    // Set enclosing compiler for nested function
    enclosingCompiler_ = &compilerStack_.back();

    // Reset for new function
    chunk_ = std::make_unique<Chunk>();
    locals_.clear();
    upvalues_.clear();
    finishedLocals_.clear();
    labels_.clear();
    unresolvedGotos_.clear();
    scopeDepth_ = 0;
    localCount_ = 0;
    expectedRetCount_ = 2; // Default for function body (one result)
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
    labels_ = std::move(state.labels);
    unresolvedGotos_ = std::move(state.unresolvedGotos);
    scopeDepth_ = state.scopeDepth;
    localCount_ = state.localCount;
    expectedRetCount_ = state.expectedRetCount;
    enclosingCompiler_ = state.enclosing;
}

size_t CodeGenerator::internString(const std::string& str) {
    return currentChunk()->addString(str);
}
