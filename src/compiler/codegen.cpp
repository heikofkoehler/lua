#include "compiler/codegen.hpp"

CodeGenerator::CodeGenerator() : chunk_(nullptr), currentLine_(1) {}

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
}

void CodeGenerator::visitRepeatStmt(RepeatStmtNode* node) {
    setLine(node->line());

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
}
