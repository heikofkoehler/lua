#ifndef LUA_AST_HPP
#define LUA_AST_HPP

#include "common/common.hpp"
#include "value/value.hpp"
#include "compiler/token.hpp"
#include <memory>
#include <vector>

// Forward declarations
class ASTVisitor;

// Base AST Node
class ASTNode {
public:
    virtual ~ASTNode() = default;
    virtual void accept(ASTVisitor& visitor) = 0;

    int line() const { return line_; }

protected:
    explicit ASTNode(int line) : line_(line) {}

private:
    int line_;  // Source line number for error reporting
};

// Expression Nodes
class ExprNode : public ASTNode {
protected:
    explicit ExprNode(int line) : ASTNode(line) {}
};

// Literal: numbers, booleans, nil
class LiteralNode : public ExprNode {
public:
    LiteralNode(const Value& value, int line)
        : ExprNode(line), value_(value) {}

    void accept(ASTVisitor& visitor) override;

    const Value& value() const { return value_; }

private:
    Value value_;
};

// Unary operation: -x, not x
class UnaryNode : public ExprNode {
public:
    UnaryNode(TokenType op, std::unique_ptr<ExprNode> operand, int line)
        : ExprNode(line), op_(op), operand_(std::move(operand)) {}

    void accept(ASTVisitor& visitor) override;

    TokenType op() const { return op_; }
    ExprNode* operand() const { return operand_.get(); }

private:
    TokenType op_;
    std::unique_ptr<ExprNode> operand_;
};

// Binary operation: a + b, a * b, etc.
class BinaryNode : public ExprNode {
public:
    BinaryNode(std::unique_ptr<ExprNode> left, TokenType op,
               std::unique_ptr<ExprNode> right, int line)
        : ExprNode(line), left_(std::move(left)), op_(op), right_(std::move(right)) {}

    void accept(ASTVisitor& visitor) override;

    ExprNode* left() const { return left_.get(); }
    TokenType op() const { return op_; }
    ExprNode* right() const { return right_.get(); }

private:
    std::unique_ptr<ExprNode> left_;
    TokenType op_;
    std::unique_ptr<ExprNode> right_;
};

// Statement Nodes
class StmtNode : public ASTNode {
protected:
    explicit StmtNode(int line) : ASTNode(line) {}
};

// Print statement: print(expr)
class PrintStmtNode : public StmtNode {
public:
    PrintStmtNode(std::unique_ptr<ExprNode> expr, int line)
        : StmtNode(line), expr_(std::move(expr)) {}

    void accept(ASTVisitor& visitor) override;

    ExprNode* expr() const { return expr_.get(); }

private:
    std::unique_ptr<ExprNode> expr_;
};

// Expression statement: evaluate expression and discard result
class ExprStmtNode : public StmtNode {
public:
    ExprStmtNode(std::unique_ptr<ExprNode> expr, int line)
        : StmtNode(line), expr_(std::move(expr)) {}

    void accept(ASTVisitor& visitor) override;

    ExprNode* expr() const { return expr_.get(); }

private:
    std::unique_ptr<ExprNode> expr_;
};

// If statement: if-then-elseif-else-end
class IfStmtNode : public StmtNode {
public:
    struct ElseIfBranch {
        std::unique_ptr<ExprNode> condition;
        std::vector<std::unique_ptr<StmtNode>> body;
    };

    IfStmtNode(std::unique_ptr<ExprNode> condition,
               std::vector<std::unique_ptr<StmtNode>> thenBranch,
               int line)
        : StmtNode(line), condition_(std::move(condition)),
          thenBranch_(std::move(thenBranch)) {}

    void accept(ASTVisitor& visitor) override;

    ExprNode* condition() const { return condition_.get(); }
    const std::vector<std::unique_ptr<StmtNode>>& thenBranch() const { return thenBranch_; }
    const std::vector<ElseIfBranch>& elseIfBranches() const { return elseIfBranches_; }
    const std::vector<std::unique_ptr<StmtNode>>& elseBranch() const { return elseBranch_; }

    void addElseIfBranch(std::unique_ptr<ExprNode> condition,
                         std::vector<std::unique_ptr<StmtNode>> body) {
        elseIfBranches_.push_back({std::move(condition), std::move(body)});
    }

    void setElseBranch(std::vector<std::unique_ptr<StmtNode>> body) {
        elseBranch_ = std::move(body);
    }

private:
    std::unique_ptr<ExprNode> condition_;
    std::vector<std::unique_ptr<StmtNode>> thenBranch_;
    std::vector<ElseIfBranch> elseIfBranches_;
    std::vector<std::unique_ptr<StmtNode>> elseBranch_;
};

// While loop: while-do-end
class WhileStmtNode : public StmtNode {
public:
    WhileStmtNode(std::unique_ptr<ExprNode> condition,
                  std::vector<std::unique_ptr<StmtNode>> body,
                  int line)
        : StmtNode(line), condition_(std::move(condition)), body_(std::move(body)) {}

    void accept(ASTVisitor& visitor) override;

    ExprNode* condition() const { return condition_.get(); }
    const std::vector<std::unique_ptr<StmtNode>>& body() const { return body_; }

private:
    std::unique_ptr<ExprNode> condition_;
    std::vector<std::unique_ptr<StmtNode>> body_;
};

// Repeat-until loop: repeat-until
class RepeatStmtNode : public StmtNode {
public:
    RepeatStmtNode(std::vector<std::unique_ptr<StmtNode>> body,
                   std::unique_ptr<ExprNode> condition,
                   int line)
        : StmtNode(line), body_(std::move(body)), condition_(std::move(condition)) {}

    void accept(ASTVisitor& visitor) override;

    const std::vector<std::unique_ptr<StmtNode>>& body() const { return body_; }
    ExprNode* condition() const { return condition_.get(); }

private:
    std::vector<std::unique_ptr<StmtNode>> body_;
    std::unique_ptr<ExprNode> condition_;
};

// Program: list of statements
class ProgramNode : public ASTNode {
public:
    explicit ProgramNode(int line = 1) : ASTNode(line) {}

    void accept(ASTVisitor& visitor) override;

    void addStatement(std::unique_ptr<StmtNode> stmt) {
        statements_.push_back(std::move(stmt));
    }

    const std::vector<std::unique_ptr<StmtNode>>& statements() const {
        return statements_;
    }

private:
    std::vector<std::unique_ptr<StmtNode>> statements_;
};

// Visitor interface for traversing AST
class ASTVisitor {
public:
    virtual ~ASTVisitor() = default;

    virtual void visitLiteral(LiteralNode* node) = 0;
    virtual void visitUnary(UnaryNode* node) = 0;
    virtual void visitBinary(BinaryNode* node) = 0;
    virtual void visitPrintStmt(PrintStmtNode* node) = 0;
    virtual void visitExprStmt(ExprStmtNode* node) = 0;
    virtual void visitIfStmt(IfStmtNode* node) = 0;
    virtual void visitWhileStmt(WhileStmtNode* node) = 0;
    virtual void visitRepeatStmt(RepeatStmtNode* node) = 0;
    virtual void visitProgram(ProgramNode* node) = 0;
};

// Accept implementations
inline void LiteralNode::accept(ASTVisitor& visitor) {
    visitor.visitLiteral(this);
}

inline void UnaryNode::accept(ASTVisitor& visitor) {
    visitor.visitUnary(this);
}

inline void BinaryNode::accept(ASTVisitor& visitor) {
    visitor.visitBinary(this);
}

inline void PrintStmtNode::accept(ASTVisitor& visitor) {
    visitor.visitPrintStmt(this);
}

inline void ExprStmtNode::accept(ASTVisitor& visitor) {
    visitor.visitExprStmt(this);
}

inline void IfStmtNode::accept(ASTVisitor& visitor) {
    visitor.visitIfStmt(this);
}

inline void WhileStmtNode::accept(ASTVisitor& visitor) {
    visitor.visitWhileStmt(this);
}

inline void RepeatStmtNode::accept(ASTVisitor& visitor) {
    visitor.visitRepeatStmt(this);
}

inline void ProgramNode::accept(ASTVisitor& visitor) {
    visitor.visitProgram(this);
}

#endif // LUA_AST_HPP
