#ifndef LUA_CODEGEN_HPP
#define LUA_CODEGEN_HPP

#include "common/common.hpp"
#include "compiler/ast.hpp"
#include "compiler/chunk.hpp"
#include <memory>

// CodeGenerator: Walks AST and generates bytecode
// Implements visitor pattern to traverse AST nodes

class CodeGenerator : public ASTVisitor {
public:
    CodeGenerator();

    // Generate bytecode from AST
    std::unique_ptr<Chunk> generate(ProgramNode* program);

    // Visitor methods
    void visitLiteral(LiteralNode* node) override;
    void visitUnary(UnaryNode* node) override;
    void visitBinary(BinaryNode* node) override;
    void visitPrintStmt(PrintStmtNode* node) override;
    void visitExprStmt(ExprStmtNode* node) override;
    void visitProgram(ProgramNode* node) override;

private:
    std::unique_ptr<Chunk> chunk_;
    int currentLine_;

    // Bytecode emission
    void emitByte(uint8_t byte);
    void emitBytes(uint8_t byte1, uint8_t byte2);
    void emitOpCode(OpCode op);
    void emitConstant(const Value& value);
    void emitReturn();

    // Get current chunk
    Chunk* currentChunk() { return chunk_.get(); }

    // Set current line for error reporting
    void setLine(int line) { currentLine_ = line; }
};

#endif // LUA_CODEGEN_HPP
