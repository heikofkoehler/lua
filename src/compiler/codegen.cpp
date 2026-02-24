#include "compiler/codegen.hpp"
#include "value/function.hpp"
#include "value/string.hpp"

CodeGenerator::CodeGenerator()
    : chunk_(nullptr), currentLine_(1), scopeDepth_(0), localCount_(0), enclosingCompiler_(nullptr), expectedRetCount_(1) {}

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

        case TokenType::PERCENT:
            emitOpCode(OpCode::OP_MOD);
            break;

        case TokenType::CARET:
            emitOpCode(OpCode::OP_POW);
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
        std::cout << "DEBUG codegen GET_LOCAL: " << name << " slot=" << slot << std::endl;
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

    // 3. Fall back to global variable
    size_t nameIndex = currentChunk()->addIdentifier(node->name());
    if (nameIndex > UINT8_MAX) {
        throw CompileError("Too many identifiers in one chunk", currentLine_);
    }
    emitOpCode(OpCode::OP_GET_GLOBAL);
    emitByte(static_cast<uint8_t>(nameIndex));
}

void CodeGenerator::visitVararg(VarargExprNode* node) {
    setLine(node->line());

    // Emit opcode to get all varargs
    emitOpCode(OpCode::OP_GET_VARARG);
}

void CodeGenerator::visitPrintStmt(PrintStmtNode* node) {
    setLine(node->line());

    // Compile each expression and emit print instruction
    for (const auto& arg : node->args()) {
        arg->accept(*this);
        emitOpCode(OpCode::OP_PRINT);
    }
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

void CodeGenerator::visitMultipleLocalDeclStmt(MultipleLocalDeclStmtNode* node) {
    setLine(node->line());

    const auto& names = node->names();
    const auto& initializers = node->initializers();

    size_t varCount = names.size();
    size_t initCount = initializers.size();

    uint8_t oldRetCount = expectedRetCount_;

    // 1. Evaluate all initializers except the last one
    for (size_t i = 0; i < (initCount > 0 ? initCount - 1 : 0); i++) {
        expectedRetCount_ = 1;
        initializers[i]->accept(*this);
    }

    // 2. Evaluate the last initializer with multires if needed
    if (initCount > 0) {
        if (varCount > initCount) {
            // Last expression needs to provide multiple values
            expectedRetCount_ = static_cast<uint8_t>(varCount - (initCount - 1));
        } else {
            expectedRetCount_ = 1;
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
        expectedRetCount_ = 1;
        values[i]->accept(*this);
    }

    // 2. Evaluate the last value with multires if needed
    if (valCount > 0) {
        if (varCount > valCount) {
            expectedRetCount_ = static_cast<uint8_t>(varCount - (valCount - 1));
        } else {
            expectedRetCount_ = 1;
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

        // Global variable
        size_t nameIndex = currentChunk()->addIdentifier(name);
        if (nameIndex > UINT8_MAX) {
            throw CompileError("Too many identifiers in one chunk", currentLine_);
        }
        emitOpCode(OpCode::OP_SET_GLOBAL);
        emitByte(static_cast<uint8_t>(nameIndex));
        emitOpCode(OpCode::OP_POP);
    }
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
    expectedRetCount_ = 3;
    node->iterator()->accept(*this);
    expectedRetCount_ = oldRetCount; // Restore

    // Pad if not a call
    auto* callExpr = dynamic_cast<CallExprNode*>(node->iterator());
    if (!callExpr) {
        // Only 1 value was pushed. Push 2 nils for state and control
        emitOpCode(OpCode::OP_NIL);
        emitOpCode(OpCode::OP_NIL);
    }

    // Store iterator state in hidden locals
    // Stack top is control, then state, then iterator
    // We need to store them in locals to access them efficiently
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
        emitByte(retCount); 
     

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

    // Check for built-in IO functions (only if callee is a simple variable)
    auto* varExpr = dynamic_cast<VariableExprNode*>(node->callee());
    if (varExpr) {
        const std::string& name = varExpr->name();

        if (name == "io_open" && node->args().size() == 2) {
            // Compile arguments (filename, mode)
            node->args()[0]->accept(*this);  // filename
            node->args()[1]->accept(*this);  // mode
            emitOpCode(OpCode::OP_IO_OPEN);
            return;
        }
        if (name == "io_write" && node->args().size() == 2) {
            // Compile arguments (file, data)
            node->args()[0]->accept(*this);  // file handle
            node->args()[1]->accept(*this);  // data
            emitOpCode(OpCode::OP_IO_WRITE);
            return;
        }
        if (name == "io_read" && node->args().size() == 1) {
            // Compile argument (file)
            node->args()[0]->accept(*this);  // file handle
            emitOpCode(OpCode::OP_IO_READ);
            return;
        }
        if (name == "io_close" && node->args().size() == 1) {
            // Compile argument (file)
            node->args()[0]->accept(*this);  // file handle
            emitOpCode(OpCode::OP_IO_CLOSE);
            return;
        }
    }

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
    expectedRetCount_ = 1; // Arguments expect 1 value
    
    node->callee()->accept(*this);

    // Compile arguments and push onto stack
    for (const auto& arg : node->args()) {
        arg->accept(*this);
    }
    
    // Restore expected return count for OP_CALL
    expectedRetCount_ = oldRetCount;

    // Emit call instruction with argument count and return count
    size_t argCount = node->args().size();
    if (argCount > UINT8_MAX) {
        throw CompileError("Too many arguments in function call", currentLine_);
    }
    emitOpCode(OpCode::OP_CALL);
    emitByte(static_cast<uint8_t>(argCount));
    // Use expectedRetCount_
    emitByte(expectedRetCount_);
}

void CodeGenerator::visitTableConstructor(TableConstructorNode* node) {
    setLine(node->line());

    // Emit OP_NEW_TABLE to create a new empty table
    emitOpCode(OpCode::OP_NEW_TABLE);

    // Table is now on top of stack
    // For each entry, we: duplicate table, compile key, compile value, then OP_SET_TABLE

    int arrayIndex = 1;  // Lua arrays start at 1

    for (const auto& entry : node->entries()) {
        // Duplicate the table reference for OP_SET_TABLE
        // Stack: [table] -> [table, table]
        emitOpCode(OpCode::OP_DUP);

        // Compile key
        if (entry.key == nullptr) {
            // Array-style entry: use implicit numeric index
            emitConstant(Value::number(arrayIndex++));
        } else {
            // Record-style or computed key
            entry.key->accept(*this);
        }

        // Compile value
        entry.value->accept(*this);

        // Set table[key] = value
        // Stack before: [table, table, key, value]
        // Stack after: [table]
        emitOpCode(OpCode::OP_SET_TABLE);
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
    emitByte(1);  // Return count: 1 value (nil)

    // Get compiled function chunk (don't call endScope - cleanup is handled by OP_RETURN_VALUE)
    auto functionChunk = std::move(chunk_);

#ifdef PRINT_CODE
    functionChunk->disassemble(node->name());
#endif

    // capturedUpvalues contains the upvalues for the current function.
    // upvalues_ was populated during this function's body compilation via resolveUpvalue/
    // resolveUpvalueHelper. After popCompilerState(), the parent's upvalues_ will be restored
    // (including any entries added by resolveUpvalueHelper for grandparent captures).
    std::vector<Upvalue> capturedUpvalues = upvalues_;

    // Restore outer compiler state
    popCompilerState();

    // Create FunctionObject with upvalue count and varargs flag
    auto func = new FunctionObject(
        node->name(),
        node->params().size(),
        std::move(functionChunk),
        capturedUpvalues.size(),
        node->hasVarargs()
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

    // Compile all return values
    const auto& values = node->values();
    if (values.empty()) {
        // Return with no values - just return nil
        emitOpCode(OpCode::OP_NIL);
        emitOpCode(OpCode::OP_RETURN_VALUE);
        emitByte(1);  // Returning 1 value (nil)
    } else {
        // Compile each return value
        for (const auto& value : values) {
            value->accept(*this);
        }

        // Emit return instruction with count
        emitOpCode(OpCode::OP_RETURN_VALUE);
        size_t count = values.size();
        if (count > UINT8_MAX) {
            throw CompileError("Too many return values", currentLine_);
        }
        emitByte(static_cast<uint8_t>(count));
    }
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
    state.expectedRetCount = expectedRetCount_;
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
    expectedRetCount_ = 1; // Default for function body
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
    expectedRetCount_ = state.expectedRetCount;
    enclosingCompiler_ = state.enclosing;
}
